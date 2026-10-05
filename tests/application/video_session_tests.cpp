// I1: indexed video for the editing target over fake ports — association
// from the Document's own Script Info (C05), seeking to a Line's start frame,
// the overlay at the frame's start, supersession and stale results.

#include "hikari/application/media_association.h"
#include "hikari/application/video_session.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <set>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

using hikari::application::visual::SourceGeometry;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

TEST(MediaAssociation, ComesFromTheDocumentsOwnScriptInfo)
{
    const auto doc = load("[Script Info]\nVideo File: ep1.mkv\nAudio File: /abs/ep1.wav\nKeyframes File: gone.txt\n");
    const std::set<std::string> files{"/subs/ep1.mkv", "/abs/ep1.wav"};
    const auto exists = [&](const std::string &p) { return files.contains(p); };
    const auto a = resolveMediaAssociations(doc, "/subs/ep1.ass", exists, false);
    ASSERT_TRUE(a.video && a.audio && a.keyframes);
    EXPECT_EQ(a.video->resolved, "/subs/ep1.mkv"); // relative to the subtitle file
    EXPECT_EQ(a.audio->resolved, "/abs/ep1.wav");
    EXPECT_EQ(a.keyframes->authored, "gone.txt");
    EXPECT_FALSE(a.keyframes->resolved); // missing: nothing is offered for it
    EXPECT_TRUE(a.offersAnything());
    // A Document without entries has no association at all (never another tab's).
    const auto none = resolveMediaAssociations(load("[Script Info]\nTitle: x\n"), "/subs/b.ass", exists, false);
    EXPECT_FALSE(none.video || none.audio || none.keyframes);
    EXPECT_FALSE(none.offersAnything());
}

TEST(MediaAssociation, WindowsDriveLettersAreAbsolute)
{
    const auto doc = load("[Script Info]\nVideo File: D:\\media\\ep1.mkv\nAudio File: sub\\ep1.wav\n");
    const std::set<std::string> files{"D:\\media\\ep1.mkv", "C:\\subs\\sub\\ep1.wav"};
    const auto a = resolveMediaAssociations(doc, "C:\\subs\\ep1.ass",
                                            [&](const std::string &p) { return files.contains(p); }, true);
    EXPECT_EQ(a.video->resolved, "D:\\media\\ep1.mkv");
    EXPECT_EQ(a.audio->resolved, "C:\\subs\\sub\\ep1.wav");
}

// 10 frames at 25 fps (40 ms), pts in milliseconds.
struct FakeSource : IndexedSourcePort {
    std::uint64_t gen = 0;
    Opened pendingOpen;
    IndexRequest lastRequest;
    std::vector<int> audioTracks; // the timeline's (the first is firstAudioTrack)
    bool newIndex = true;
    std::string handoffIndexFile;
    SourceGeometry geometry; // T1: what the timeline reports (none by default)
    int colorSpace = 2, colorRange = 0; // V4: frame 0's matrix and range (unspecified)
    std::vector<std::pair<int, int>> matrixCalls;
    std::size_t framesBeforeMatrix = 0; // frame requests made before the last matrix call
    bool refuseMatrix = false;
    void setInputMatrix(int cs, int cr, MatrixSet done) override
    {
        matrixCalls.emplace_back(cs, cr);
        framesBeforeMatrix = frames.size();
        if (refuseMatrix)
            return done(std::unexpected(SourceError::BackendFailure));
        done({});
    }
    std::uint64_t openIndexed(const std::string &path, const IndexRequest &request, Progress progress,
                              Opened done) override
    {
        lastRequest = request;
        return open(path, std::move(progress), std::move(done));
    }
    std::deque<std::pair<int, FrameReady>> frames;
    Progress pendingProgress;                // V3
    int cancelled = 0;                       // V3
    std::optional<OpenFailure> failure;      // V3
    std::uint64_t open(const std::string &, Progress progress, Opened done) override
    {
        pendingOpen = std::move(done);
        pendingProgress = std::move(progress);
        return ++gen;
    }
    std::optional<OpenFailure> openFailure() const override { return failure; }
    void finishOpen(bool ok = true)
    {
        if (!ok)
            return pendingOpen(std::unexpected(SourceError::InvalidInput));
        SourceTimeline t;
        t.generation = gen;
        t.timeBaseNumerator = 1;
        t.timeBaseDenominator = 1000;
        for (int i = 0; i < 10; ++i)
            t.pts.push_back(i * 40);
        t.keyframes = {0, 4, 8};
        t.audioTracks = audioTracks;
        t.firstAudioTrack = audioTracks.empty() ? -1 : audioTracks.front();
        t.newIndex = newIndex;
        t.handoffIndexFile = handoffIndexFile;
        t.width = geometry.width;
        t.height = geometry.height;
        t.sarNum = geometry.sarNum;
        t.sarDen = geometry.sarDen;
        t.colorSpace = colorSpace;
        t.colorRange = colorRange;
        pendingOpen(t);
    }
    void fail(std::size_t which = 0)
    {
        auto [index, done] = std::move(frames[which]);
        frames.erase(frames.begin() + static_cast<std::ptrdiff_t>(which));
        done(std::unexpected(SourceError::BackendFailure));
    }
    void answer(std::size_t which = 0)
    {
        auto [index, done] = std::move(frames[which]);
        frames.erase(frames.begin() + static_cast<std::ptrdiff_t>(which));
        IndexedFrame f;
        f.generation = gen;
        f.index = index;
        f.pts = index * 40;
        f.width = 4;
        f.height = 2;
        f.stride = 16;
        f.bgra.resize(32);
        done(std::move(f));
    }
    void cancelOpen() override { ++cancelled; }
    void frame(int index, FrameReady done) override { frames.emplace_back(index, std::move(done)); }
    void openAudio(int, AudioOpened) override {}
    void audio(std::int64_t, std::int64_t, AudioReady) override {}
    void beginPcm(std::int64_t, std::int64_t, int, int, PcmBegun) override {}
    void nextPcm(std::int64_t, PcmReady) override {}
    void cancelReads() override {}
    std::uint64_t generation() const override { return gen; }
};

struct FakeRenderer : SubtitleRendererPort {
    std::vector<std::int64_t> renderedAt;
    std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot) override { return 1; }
    std::expected<OverlayFrame, RenderError> render(core::DocumentTime t, int w, int h) override
    {
        renderedAt.push_back(t.microseconds());
        OverlayFrame o;
        o.width = w;
        o.height = h;
        o.empty = false;
        return o;
    }
};

struct FakePresenter : PresenterPort {
    std::vector<Presentation> shown;
    void present(Presentation p, Done done) override
    {
        shown.push_back(p);
        done(PresentResult{p.generation, PresentOutcome::Accepted, PresentStage::Rendered, {}});
    }
};

struct VideoTest : ::testing::Test {
    FakeSource source;
    FakeRenderer renderer;
    FakePresenter presenter;
    VideoSession video{source, renderer};
};

TEST_F(VideoTest, ASeekWhileIndexingAppliesWhenReady)
{
    video.setPresenter(&presenter);
    video.setSubtitles({std::byte{'x'}});
    video.open("/m/ep1.mkv");
    EXPECT_EQ(video.state(), VideoSession::State::Opening);
    video.seekTo(core::DocumentTime(100'000)); // 100 ms: frame 3 starts at 120 ms
    source.finishOpen();
    EXPECT_EQ(video.state(), VideoSession::State::Ready);
    EXPECT_EQ(video.frameCount(), 10);
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 3);
    source.answer();
    EXPECT_EQ(video.shownFrame(), 3);
    ASSERT_EQ(presenter.shown.size(), 1u);
    EXPECT_EQ(presenter.shown[0].frame->index, 3);
    ASSERT_TRUE(presenter.shown[0].overlay);
    EXPECT_EQ(renderer.renderedAt, std::vector<std::int64_t>{120'000}); // the frame's start, not the Line's
    EXPECT_EQ(video.lastPresent()->outcome, PresentOutcome::Accepted);
}

TEST_F(VideoTest, SourceGeometryOutlivesADecoderFailure)
{
    // T1: the visual tools take the source's frame size and SAR from its
    // timeline (legacy ProviderFFMS2::Init); a frame that fails to decode
    // leaves them, so the tools stay usable.
    source.geometry = {720, 480, 32, 27};
    video.setPresenter(&presenter);
    EXPECT_FALSE(video.sourceGeometry().valid());
    video.open("/m/ep1.mkv");
    source.finishOpen();
    EXPECT_EQ(video.sourceGeometry(), (SourceGeometry{720, 480, 32, 27}));
    source.fail();
    EXPECT_EQ(video.state(), VideoSession::State::Ready);
    EXPECT_EQ(video.sourceGeometry(), (SourceGeometry{720, 480, 32, 27}));
    video.showFrame(1);
    source.answer();
    ASSERT_FALSE(presenter.shown.empty());
    EXPECT_DOUBLE_EQ(presenter.shown.back().transform.pixelAspect, 32.0 / 27.0);
    video.close();
    EXPECT_FALSE(video.sourceGeometry().valid());
}

TEST_F(VideoTest, SourceGeometryFallsBackToTheShownFrame)
{
    video.open("/m/ep1.mkv");
    source.finishOpen(); // a timeline without a size
    EXPECT_FALSE(video.sourceGeometry().valid());
    source.answer();
    EXPECT_EQ(video.sourceGeometry(), (SourceGeometry{4, 2, 0, 1}));
}

TEST_F(VideoTest, ASeekMadeBeforeOpeningAppliesToTheVideo)
{
    video.seekTo(core::DocumentTime(100'000)); // no video yet: the active Line's start
    video.open("/m/ep1.mkv");
    source.finishOpen();
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 3);
}

TEST_F(VideoTest, ANewerRequestSupersedesAnOlderOne)
{
    video.setPresenter(&presenter);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer(); // frame 0
    video.showFrame(5);
    video.showFrame(7);
    source.answer(1); // 7 arrives first
    source.answer(0); // then the superseded 5
    EXPECT_EQ(video.shownFrame(), 7);
    EXPECT_EQ(presenter.shown.back().frame->index, 7);
    EXPECT_EQ(presenter.shown.size(), 2u);
}

TEST_F(VideoTest, StepsStopAtTheEnds)
{
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    EXPECT_FALSE(video.step(-1));
    EXPECT_TRUE(video.step(1));
    EXPECT_EQ(video.requestedFrame(), 1);
    video.showFrame(9);
    EXPECT_FALSE(video.step(1));
    // Past the last frame start the last frame is shown.
    video.seekTo(core::DocumentTime(10'000'000));
    EXPECT_EQ(video.requestedFrame(), 9);
}

TEST_F(VideoTest, APresenterAttachedLaterGetsTheShownFrame)
{
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    EXPECT_TRUE(presenter.shown.empty());
    video.setPresenter(&presenter);
    ASSERT_EQ(presenter.shown.size(), 1u);
    EXPECT_EQ(presenter.shown[0].frame->index, 0);
}

TEST_F(VideoTest, NewSubtitlesRenderTheShownFrameAgain)
{
    video.setPresenter(&presenter);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    EXPECT_FALSE(presenter.shown.back().overlay); // no subtitles yet
    video.setSubtitles({std::byte{'x'}});
    EXPECT_EQ(presenter.shown.size(), 2u);
    EXPECT_TRUE(presenter.shown.back().overlay);
}

TEST_F(VideoTest, AFailedOpenLeavesTheSessionFailed)
{
    video.open("/m/missing.mkv");
    source.finishOpen(false);
    EXPECT_EQ(video.state(), VideoSession::State::Failed);
    EXPECT_EQ(video.error(), SourceError::InvalidInput);
    video.seekTo(core::DocumentTime(0));
    EXPECT_FALSE(video.step(1));
    EXPECT_TRUE(source.frames.empty());
}

TEST_F(VideoTest, CloseDropsLateFrames)
{
    video.setPresenter(&presenter);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    video.close();
    source.answer();
    EXPECT_TRUE(presenter.shown.empty());
    EXPECT_EQ(video.state(), VideoSession::State::Closed);
}

} // namespace

namespace {

struct FakePlayer : GeneralPlayerPort {
    std::vector<std::string> calls;
    Opened pendingOpen;
    Seeked pendingSeek;
    std::int64_t soughtUs = -1;
    void open(const std::string &path, Opened done) override
    {
        calls.push_back("open " + path);
        pendingOpen = std::move(done);
    }
    void seek(std::int64_t us, Seeked done) override
    {
        calls.push_back("seek " + std::to_string(us));
        soughtUs = us;
        pendingSeek = std::move(done);
    }
    void play() override { calls.push_back("play"); }
    void pause() override { calls.push_back("pause"); }
    void stop() override { calls.push_back("stop"); }
    bool selectAudioTrack(int index) override
    {
        calls.push_back("audio track " + std::to_string(index));
        return true;
    }
    bool selectSubtitleTrack(int) override { return true; }
    PlaybackState playbackState() const override { return PlaybackState::Stopped; }
    MediaStatus mediaStatus() const override { return MediaStatus::Loaded; }
    double bufferProgress() const override { return 1; }
    PlayerClock clock() const override { return {}; }
    MediaDescription description() const override { return {}; }
    std::uint64_t generation() const override { return 1; }
    void opened(MediaDescription description = {}) { pendingOpen(std::move(description)); }
    void failed() { pendingOpen(std::unexpected(PlayerError::FormatError)); }
    void delivered() { pendingSeek(SeekResult{1, soughtUs, soughtUs, {}}); }
};

IndexedFrame playerFrame()
{
    IndexedFrame f;
    f.index = -1;
    f.width = 4;
    f.height = 2;
    f.stride = 16;
    f.bgra.resize(32);
    return f;
}

} // namespace

TEST_F(VideoTest, PlaybackShowsThePlayersFramesAndPausesOnTheExactIndexedFrame)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    video.setSubtitles({std::byte{'x'}});
    EXPECT_FALSE(video.play()); // no video yet
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer(); // frame 0
    video.showFrame(2);
    source.answer(); // frame 2 at 80 ms
    ASSERT_TRUE(video.play());
    EXPECT_TRUE(video.playing());
    player.opened();
    player.delivered(); // the seek to the shown frame's start
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.mkv", "seek 80000", "play"}));
    // Frames from the player are shown with the overlay at their own time.
    video.generalFrame(playerFrame(), 205'000);
    EXPECT_EQ(presenter.shown.back().frame->index, -1);
    EXPECT_EQ(renderer.renderedAt.back(), 205'000);
    // Pause: the indexed frame whose interval holds 205 ms is frame 5 (200-240 ms).
    ASSERT_TRUE(video.pause());
    EXPECT_FALSE(video.playing());
    EXPECT_EQ(player.calls.back(), "pause");
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 5);
    source.answer();
    EXPECT_EQ(video.shownFrame(), 5);
    EXPECT_EQ(renderer.renderedAt.back(), 200'000);
    // A late player frame after the pause is ignored.
    video.generalFrame(playerFrame(), 250'000);
    EXPECT_EQ(video.shownFrame(), 5);
    // Playing again does not reopen; Stop returns to the first frame.
    ASSERT_TRUE(video.play());
    player.delivered();
    EXPECT_EQ(player.calls[player.calls.size() - 2], "seek 200000");
    EXPECT_FALSE(video.stopped());
    ASSERT_TRUE(video.stop());
    source.answer();
    EXPECT_EQ(video.shownFrame(), 0);
    // A1: legacy's Stopped state lasts until the next Play (the audio box
    // marks the video's frame only while Paused); Stop while paused is not it.
    EXPECT_TRUE(video.stopped());
    video.showFrame(3);
    EXPECT_TRUE(video.stopped());
    ASSERT_TRUE(video.play());
    EXPECT_FALSE(video.stopped());
    ASSERT_TRUE(video.pause());
    ASSERT_TRUE(video.stop());
    EXPECT_FALSE(video.stopped());
}

TEST_F(VideoTest, SeeksAndKeyframesFollowTheLegacyRules)
{
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer(); // frame 0
    auto lands = [&](int expected) {
        if (source.frames.empty())
            return false;
        const int asked = source.frames.back().first;
        while (!source.frames.empty())
            source.answer();
        return asked == expected && video.shownFrame() == expected;
    };
    video.showFrame(2);
    EXPECT_TRUE(lands(2));                // 80 ms
    ASSERT_TRUE(video.seekBy(100));       // 180 ms: the frame at or after is 5 (200 ms)
    EXPECT_TRUE(lands(5));
    ASSERT_TRUE(video.seekBy(-5000));     // before the start: the first frame
    EXPECT_TRUE(lands(0));
    ASSERT_TRUE(video.seekBy(60'000));    // past the end: the last frame
    EXPECT_TRUE(lands(9));
    // An end time shows the frame showing 1 ms before it.
    video.seekToEnd(core::DocumentTime(200'000));
    EXPECT_TRUE(lands(4));
    video.seekToEnd(core::DocumentTime(0));
    EXPECT_TRUE(lands(0));
    // Keyframes 0, 4, 8, wrapping at both ends.
    EXPECT_TRUE(video.isKeyframe(4));
    EXPECT_FALSE(video.isKeyframe(5));
    video.showFrame(5);
    EXPECT_TRUE(lands(5));
    ASSERT_TRUE(video.nextKeyframe());
    EXPECT_TRUE(lands(8));
    ASSERT_TRUE(video.nextKeyframe());
    EXPECT_TRUE(lands(0));
    ASSERT_TRUE(video.previousKeyframe());
    EXPECT_TRUE(lands(8));
    ASSERT_TRUE(video.previousKeyframe());
    EXPECT_TRUE(lands(4));
}

// A1: the track the video's open chose (legacy's provider track, from the
// chooser or ACCEPTED_AUDIO_STREAM) is the one general playback plays, as
// legacy played the audio box's track; its index request reaches the source.
TEST_F(VideoTest, PlaybackPlaysTheChosenAudioTrack)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    source.audioTracks = {1, 2, 4};
    source.newIndex = false;
    video.open("/m/ep1.mkv", IndexRequest{4, "/cfg/Indices/ep1_4.ffindex"});
    EXPECT_EQ(source.lastRequest.audioTrack, 4);
    EXPECT_EQ(source.lastRequest.indexFile, "/cfg/Indices/ep1_4.ffindex");
    source.finishOpen();
    source.answer();
    EXPECT_EQ(video.audioTrack(), 4);
    EXPECT_FALSE(video.newIndex());
    EXPECT_TRUE(video.hasAudio());
    ASSERT_TRUE(video.play());
    MediaDescription d;
    d.audioTracks.resize(3);
    player.opened(d);
    player.delivered();
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.mkv", "audio track 2", "seek 0", "play"}));
    ASSERT_TRUE(video.pause());
    source.answer();
    // the same file reopened with another track: the open player switches
    video.open("/m/ep1.mkv", IndexRequest{1, {}});
    source.newIndex = true;
    source.handoffIndexFile = "/tmp/hikari-index-x.ffindex"; // its index file could not be written
    source.finishOpen();
    source.answer();
    EXPECT_EQ(video.audioTrack(), 1);
    EXPECT_TRUE(video.newIndex());
    EXPECT_EQ(video.indexHandoff(), "/tmp/hikari-index-x.ffindex");
    player.calls.clear();
    ASSERT_TRUE(video.play());
    EXPECT_EQ(player.calls.front(), "audio track 0");
    // without a choice: the first audio track, and an open with the default request
    video.open("/m/ep2.mkv");
    EXPECT_EQ(source.lastRequest.audioTrack, IndexRequest::kEveryAudioTrack);
    EXPECT_TRUE(source.lastRequest.indexFile.empty());
    source.finishOpen();
    EXPECT_EQ(video.audioTrack(), 1);
    video.close();
    EXPECT_EQ(video.audioTrack(), -1);
    EXPECT_TRUE(video.indexHandoff().empty());
}

// A4: GLOBAL_PLAY_ACTUAL_LINE (legacy RendererVideo::PlayLine with
// Timebase::PlayEndBefore): frames every 40 ms, 0..360 ms.
TEST_F(VideoTest, PlayLinePlaysToTheFrameBeforeTheEnd)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    EXPECT_FALSE(video.playLine(100, 300)); // no video
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    // 300 ms is in frame 8 (320 ms): the end is frame 7's start, 280 ms;
    // the start shows frame 3 (FrameAt(100): 120 ms) and plays from it
    ASSERT_TRUE(video.playLine(100, 300));
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 3);
    player.opened();
    player.delivered();
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.mkv", "seek 120000", "play"}));
    video.generalFrame(playerFrame(), 245'000); // frame 6
    EXPECT_TRUE(video.playing());
    video.generalFrame(playerFrame(), 285'000); // frame 7: the end, paused there
    EXPECT_FALSE(video.playing());
    EXPECT_EQ(player.calls.back(), "pause");
    EXPECT_EQ(source.frames.back().first, 7);
    // a start at or after that end, or at or after the last frame, plays nothing
    EXPECT_FALSE(video.playLine(280, 300));
    EXPECT_FALSE(video.playLine(360, 2000));
    // an end past the video is the last frame's time
    player.calls.clear();
    ASSERT_TRUE(video.playLine(0, 2000));
    player.delivered();
    video.generalFrame(playerFrame(), 330'000);
    EXPECT_TRUE(video.playing());
    // a Line played while playing pauses first
    ASSERT_TRUE(video.playLine(40, 200));
    EXPECT_EQ(player.calls, (std::vector<std::string>{"seek 0", "play", "pause", "seek 40000"}));
    player.delivered();
    video.generalFrame(playerFrame(), 165'000); // frame 4 (160 ms): the end
    EXPECT_FALSE(video.playing());
    // an end in the first frame gives 0: legacy plays on
    ASSERT_TRUE(video.playLine(-100, 20));
    player.delivered();
    video.generalFrame(playerFrame(), 365'000);
    EXPECT_TRUE(video.playing());
    // a plain Play has no end
    ASSERT_TRUE(video.pause());
    ASSERT_TRUE(video.play());
    player.delivered();
    video.generalFrame(playerFrame(), 300'000);
    EXPECT_TRUE(video.playing());
}

// V3: legacy ProgressSink "Indexing video" and its Cancel
// (ProviderFFMS2.cpp:89-97, 301-312): the progress while indexing, and a
// cancelled indexing leaves no video (the failed provider deleted the
// renderer) and nothing to log; a failure keeps the source's stage and text.
TEST_F(VideoTest, IndexingReportsProgressAndCanBeCancelled)
{
    video.open("/m/ep1.mkv");
    EXPECT_FALSE(video.indexingProgress());
    ASSERT_TRUE(source.pendingProgress);
    source.pendingProgress(25, 100);
    ASSERT_TRUE(video.indexingProgress());
    EXPECT_EQ(*video.indexingProgress(), std::make_pair(std::int64_t{25}, std::int64_t{100}));
    video.cancelOpen();
    EXPECT_EQ(source.cancelled, 1);
    EXPECT_EQ(video.state(), VideoSession::State::Closed);
    EXPECT_TRUE(video.path().empty());
    EXPECT_FALSE(video.indexingProgress());
    // the helper's late Cancelled answer changes nothing
    source.pendingOpen(std::unexpected(SourceError::Cancelled));
    EXPECT_EQ(video.state(), VideoSession::State::Closed);
    // a cancelled answer the session did not ask for closes it too
    video.open("/m/ep1.mkv");
    source.failure = OpenFailure{OpenStage::Indexing, "Cancelled by user"};
    source.pendingOpen(std::unexpected(SourceError::Cancelled));
    EXPECT_EQ(video.state(), VideoSession::State::Closed);
    // a failed indexing: Failed, with FFMS2's text
    video.open("/m/ep2.mkv");
    source.failure = OpenFailure{OpenStage::Indexing, "Codec not found"};
    source.pendingOpen(std::unexpected(SourceError::BackendFailure));
    EXPECT_EQ(video.state(), VideoSession::State::Failed);
    ASSERT_TRUE(video.openFailure());
    EXPECT_EQ(video.openFailure()->stage, OpenStage::Indexing);
    EXPECT_EQ(video.openFailure()->message, "Codec not found");
    video.open("/m/ep3.mkv");
    EXPECT_FALSE(video.openFailure());
    EXPECT_FALSE(video.dummy());
}

// V3: the stream menu's choice through both transports: chosen before
// playback it is the track general playback opens with, chosen while playing
// it switches the player at once, and it outlives the pause's handoff to the
// indexed frame and the next play.
TEST_F(VideoTest, AChosenAudioStreamPlaysThroughBothTransports)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    source.audioTracks = {1, 2, 4};
    EXPECT_FALSE(video.selectPlaybackAudioTrack(0)); // no video
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    EXPECT_EQ(video.audioTracks(), (std::vector<int>{1, 2, 4}));
    EXPECT_EQ(video.playbackAudioTrack(), 0);
    EXPECT_FALSE(video.selectPlaybackAudioTrack(3));
    EXPECT_FALSE(video.selectPlaybackAudioTrack(-1));
    ASSERT_TRUE(video.selectPlaybackAudioTrack(2));
    EXPECT_TRUE(player.calls.empty()); // the player has nothing open yet
    ASSERT_TRUE(video.play());
    MediaDescription d;
    d.audioTracks.resize(3);
    player.opened(d);
    player.delivered();
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.mkv", "audio track 2", "seek 0", "play"}));
    player.calls.clear();
    ASSERT_TRUE(video.selectPlaybackAudioTrack(1)); // while playing: at once
    EXPECT_EQ(player.calls, (std::vector<std::string>{"audio track 1"}));
    EXPECT_EQ(video.audioTrack(), 1); // the box's track is not the stream menu's
    ASSERT_TRUE(video.pause());
    source.answer();
    player.calls.clear();
    ASSERT_TRUE(video.play());
    EXPECT_EQ(player.calls.front(), "audio track 1");
    // a new video starts from its own track again
    video.open("/m/ep2.mkv");
    source.finishOpen();
    EXPECT_EQ(video.playbackAudioTrack(), 0);
}

// V3: chapters seek with legacy Seek(ms) (SeekFrame: the frame at or after,
// 0 at or before 0), and while playing the player goes on from there;
// Tell() is the shown frame's start, or while playing the delivered time.
TEST_F(VideoTest, ChapterSeeksAndTellFollowLegacy)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    EXPECT_FALSE(video.seekToMs(100));
    EXPECT_EQ(video.tell(), 0);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    ASSERT_TRUE(video.seekToMs(100)); // frames every 40 ms: 120 ms is frame 3
    EXPECT_EQ(video.requestedFrame(), 3);
    source.answer();
    EXPECT_EQ(video.tell(), 120);
    ASSERT_TRUE(video.seekToMs(-5));
    EXPECT_EQ(video.requestedFrame(), 0);
    source.answer();
    ASSERT_TRUE(video.seekToMs(100000)); // past the end: the last frame
    EXPECT_EQ(video.requestedFrame(), 9);
    source.answer();
    ASSERT_TRUE(video.play());
    player.opened();
    player.delivered();
    video.generalFrame(playerFrame(), 360000);
    EXPECT_EQ(video.tell(), 360);
    player.calls.clear();
    ASSERT_TRUE(video.seekToMs(160));
    EXPECT_TRUE(video.playing());
    EXPECT_EQ(player.calls, (std::vector<std::string>{"seek 160000"}));
    player.delivered();
    EXPECT_EQ(player.calls.back(), "play");
}

// V3: VideoBox::NextFile with no file that way: Seek(0) then Pause(false),
// which toggles play.
TEST_F(VideoTest, RestartGoesToTheStartAndTogglesPlay)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    EXPECT_FALSE(video.restartToggled());
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    video.showFrame(5);
    source.answer();
    ASSERT_TRUE(video.restartToggled()); // paused: shows frame 0 and plays from it
    EXPECT_EQ(video.requestedFrame(), 0);
    EXPECT_TRUE(video.playing());
    player.opened();
    EXPECT_EQ(player.soughtUs, 0);
    player.delivered();
    video.generalFrame(playerFrame(), 200000);
    ASSERT_TRUE(video.restartToggled()); // playing: back to the start, paused there
    EXPECT_FALSE(video.playing());
    EXPECT_EQ(video.requestedFrame(), 0);
}

// W1: legacy built the DirectShow graph once as the video loaded and warned
// once when it failed (VideoBox::LoadVideo, VideoBox.cpp:328-331). A failed
// prepare is not retried on later frames or plays until the video loads
// again or the player changes.
TEST_F(VideoTest, AFailedPrepareIsNotRetriedUntilTheVideoLoadsAgain)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    video.open("/m/ep1.avi");
    source.finishOpen();
    source.answer();
    video.preparePlayer();
    video.preparePlayer(); // pending: not asked twice
    ASSERT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.avi"}));
    player.failed();
    EXPECT_TRUE(video.playerFailed());
    EXPECT_FALSE(video.playerHasVideo());
    // Frame steps, seeks and plays after the failure do not open it again.
    video.showFrame(2);
    source.answer();
    video.preparePlayer();
    EXPECT_FALSE(video.play());
    EXPECT_FALSE(video.playing());
    EXPECT_FALSE(video.playLine(0, 200));
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.avi"}));
    // Loading the video again asks once more.
    video.open("/m/ep1.avi");
    source.finishOpen();
    EXPECT_FALSE(video.playerFailed());
    video.preparePlayer();
    EXPECT_EQ(player.calls.size(), 2u);
    player.failed();
    EXPECT_TRUE(video.playerFailed());
    // So does another player chosen in the settings.
    FakePlayer other;
    video.setGeneralPlayer(&other);
    EXPECT_FALSE(video.playerFailed());
    video.preparePlayer();
    EXPECT_EQ(other.calls, (std::vector<std::string>{"open /m/ep1.avi"}));
    other.opened();
    EXPECT_TRUE(video.playerHasVideo());
}

// V4: the Script properties matrix through the source's input matrix
// (legacy ProviderFFMS2::Init, then SetColorSpace with a Render while
// paused, RendererFFMS2.h:66-72).
TEST_F(VideoTest, TheDocumentMatrixSetsTheSourcesInputMatrix)
{
    std::vector<std::string> logged;
    video.setLog([&](const std::string &message) { logged.push_back(message); });
    video.setPresenter(&presenter);
    source.colorSpace = 1; // BT.709, limited
    source.colorRange = 1;
    video.setMatrix("TV.601");
    video.open("/m/ep1.mkv");
    source.finishOpen();
    // Before the first frame is asked for.
    ASSERT_EQ(source.matrixCalls, (std::vector<std::pair<int, int>>{{5, 1}}));
    EXPECT_EQ(source.framesBeforeMatrix, 0u);
    ASSERT_EQ(source.frames.size(), 1u);
    source.answer();
    EXPECT_EQ(video.colourMatrix().applied(), "TV.601");
    // The source's own: BT.709 set, the shown frame decoded again.
    video.setMatrix("TV.709");
    EXPECT_EQ(source.matrixCalls.back(), (std::pair<int, int>{1, 1}));
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 0);
    EXPECT_EQ(source.framesBeforeMatrix, 0u); // the frame after the matrix
    source.answer();
    // Unchanged: nothing.
    video.setMatrix("TV.709");
    EXPECT_EQ(source.matrixCalls.size(), 2u);
    EXPECT_TRUE(source.frames.empty());
    // Refused: legacy's message, the old name kept.
    source.refuseMatrix = true;
    video.setMatrix("TV.601");
    EXPECT_EQ(logged, (std::vector<std::string>{"Cannot change YCbCr matrix"}));
    EXPECT_EQ(video.colourMatrix().applied(), "TV.709");
    // A BT.601 source never takes "TV.709".
    source.refuseMatrix = false;
    source.colorSpace = 6;
    video.setMatrix("TV.709");
    video.open("/m/ep2.mkv");
    const auto before = source.matrixCalls.size();
    source.finishOpen();
    EXPECT_EQ(source.matrixCalls.size(), before);
    EXPECT_EQ(video.colourMatrix().source(), "TV.601");
    // An untagged HD source with a Document matrix other than TV.601: its
    // guess, BT.709, is set before the first frame (approved departure
    // V4-untagged-matrix; legacy left the converter's BT.601 under the name
    // TV.709), so a later "TV.709" has nothing to change.
    source.colorSpace = 2;
    source.colorRange = 0;
    source.geometry.width = 1280;
    source.geometry.height = 720;
    video.open("/m/ep3.mkv");
    video.setMatrix("PC.709");
    source.frames.clear();
    source.matrixCalls.clear();
    source.finishOpen();
    ASSERT_EQ(source.matrixCalls, (std::vector<std::pair<int, int>>{{1, 0}}));
    EXPECT_EQ(source.framesBeforeMatrix, 0u);
    EXPECT_EQ(video.colourMatrix().applied(), "TV.709");
    video.setMatrix("TV.709");
    EXPECT_EQ(source.matrixCalls.size(), 1u);
}
