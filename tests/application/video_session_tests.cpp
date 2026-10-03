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
    std::deque<std::pair<int, FrameReady>> frames;
    std::uint64_t open(const std::string &, Progress, Opened done) override
    {
        pendingOpen = std::move(done);
        return ++gen;
    }
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
        pendingOpen(t);
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
    void cancelOpen() override {}
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
    bool selectAudioTrack(int) override { return true; }
    bool selectSubtitleTrack(int) override { return true; }
    PlaybackState playbackState() const override { return PlaybackState::Stopped; }
    MediaStatus mediaStatus() const override { return MediaStatus::Loaded; }
    double bufferProgress() const override { return 1; }
    PlayerClock clock() const override { return {}; }
    MediaDescription description() const override { return {}; }
    std::uint64_t generation() const override { return 1; }
    void opened() { pendingOpen(MediaDescription{}); }
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
