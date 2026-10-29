#include "hikari/application/media_association.h"
#include "hikari/application/video_session.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>

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
    EXPECT_EQ(a.video->resolved, "/subs/ep1.mkv");
    EXPECT_EQ(a.audio->resolved, "/abs/ep1.wav");
    EXPECT_EQ(a.keyframes->authored, "gone.txt");
    EXPECT_FALSE(a.keyframes->resolved);
    EXPECT_TRUE(a.offersAnything());
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

struct FakeSource : IndexedSourcePort {
    std::uint64_t gen = 0;
    Opened pendingOpen;
    IndexRequest lastRequest;
    std::vector<int> audioTracks;
    bool newIndex = true;
    std::string handoffIndexFile;
    SourceGeometry geometry;
    int colorSpace = 2, colorRange = 0;
    std::vector<std::pair<int, int>> matrixCalls;
    std::size_t framesBeforeMatrix = 0;
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
    Progress pendingProgress;
    int cancelled = 0;
    std::optional<OpenFailure> failure;
    std::uint8_t fill = 0;
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
        f.bgra.assign(32, std::byte{fill});
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
    std::vector<OverlaySize> scaled;
    OverlayPixels pixels;
    std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot) override { return 1; }
    std::expected<OverlayFrame, RenderError> renderScaled(core::DocumentTime t, int w, int h, int vw, int vh) override
    {
        scaled.push_back({w, h, vw, vh});
        return render(t, w, h);
    }
    std::expected<OverlayFrame, RenderError> render(core::DocumentTime t, int w, int h) override
    {
        renderedAt.push_back(t.microseconds());
        OverlayFrame o;
        o.width = w;
        o.height = h;
        o.stride = w * 4;
        o.empty = false;
        if (pixels.size() == static_cast<std::size_t>(w) * h * 4)
            o.pixels = pixels;
        return o;
    }
};

struct FakePresenter : PresenterPort {
    std::vector<Presentation> shown;
    bool ycbcr = false;
    bool convertsYcbcr() const override { return ycbcr; }
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

TEST_F(VideoTest, PlayerFramesAreKeptAsPlanesOnlyForAPresenterThatConvertsThem)
{
    EXPECT_FALSE(video.presenterConvertsYcbcr());
    video.setPresenter(&presenter);
    EXPECT_FALSE(video.presenterConvertsYcbcr());
    presenter.ycbcr = true;
    EXPECT_TRUE(video.presenterConvertsYcbcr());
}

TEST_F(VideoTest, ASeekWhileIndexingAppliesWhenReady)
{
    video.setPresenter(&presenter);
    video.setSubtitles({std::byte{'x'}});
    video.open("/m/ep1.mkv");
    EXPECT_EQ(video.state(), VideoSession::State::Opening);
    video.seekTo(core::DocumentTime(100'000));
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
    EXPECT_EQ(renderer.renderedAt, std::vector<std::int64_t>{120'000});
    EXPECT_EQ(video.lastPresent()->outcome, PresentOutcome::Accepted);
}

TEST_F(VideoTest, AFrameWithSubtitlesHasTheOverlayLaidAsASnapshotLaysIt)
{
    source.fill = 200;
    std::vector<std::uint8_t> overlay(32, 0);
    for (int c = 0; c < 4; ++c)
        overlay[static_cast<std::size_t>(c)] = c < 3 ? 100 : 128;
    renderer.pixels = OverlayPixels(overlay);
    video.setSubtitles({std::byte{'x'}});
    video.open("/m/ep1.mkv");
    source.finishOpen();
    while (!source.frames.empty())
        source.answer();
    std::shared_ptr<const IndexedFrame> got;
    video.requestFrame(2, true, [&](std::shared_ptr<const IndexedFrame> f) { got = std::move(f); });
    ASSERT_EQ(source.frames.size(), 1u);
    source.answer();
    ASSERT_TRUE(got);
    const auto byte = [&](std::size_t i) { return std::to_integer<int>(got->bgra[i]); };
    EXPECT_EQ(byte(0), 200);
    EXPECT_EQ(byte(1), 200);
    EXPECT_EQ(byte(2), 200);
    EXPECT_EQ(byte(3), 200);
    for (std::size_t i = 4; i < 32; ++i)
        EXPECT_EQ(byte(i), 200) << i;
    video.requestFrame(2, false, [&](std::shared_ptr<const IndexedFrame> f) { got = std::move(f); });
    source.fill = 7;
    source.answer();
    ASSERT_TRUE(got);
    EXPECT_EQ(std::to_integer<int>(got->bgra[0]), 7);
}

TEST_F(VideoTest, SourceGeometryOutlivesADecoderFailure)
{
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
    source.finishOpen();
    EXPECT_FALSE(video.sourceGeometry().valid());
    source.answer();
    EXPECT_EQ(video.sourceGeometry(), (SourceGeometry{4, 2, 0, 1}));
}

TEST_F(VideoTest, ASeekMadeBeforeOpeningAppliesToTheVideo)
{
    video.seekTo(core::DocumentTime(100'000));
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
    source.answer();
    video.showFrame(5);
    video.showFrame(7);
    source.answer(1);
    source.answer(0);
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
    EXPECT_FALSE(presenter.shown.back().overlay);
    video.setSubtitles({std::byte{'x'}});
    EXPECT_EQ(presenter.shown.size(), 2u);
    EXPECT_TRUE(presenter.shown.back().overlay);
}

TEST_F(VideoTest, AScreenSizeDrawsTheShownFramesOverlayAgain)
{
    video.setPresenter(&presenter);
    video.setSubtitles({std::byte{'x'}});
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    ASSERT_TRUE(presenter.shown.back().overlay);
    const int width = presenter.shown.back().frame->width, height = presenter.shown.back().frame->height;
    EXPECT_EQ(presenter.shown.back().overlay->width, width);
    video.setScreenSize(std::pair{width * 3, height * 3});
    ASSERT_EQ(presenter.shown.size(), 2u);
    ASSERT_TRUE(presenter.shown.back().overlay);
    EXPECT_EQ(presenter.shown.back().overlay->width, width * 3);
    EXPECT_EQ(renderer.scaled, (std::vector<OverlaySize>{{width * 3, height * 3, width, height}}));
    video.setScreenSize(std::pair{width * 3, height * 3});
    EXPECT_EQ(presenter.shown.size(), 2u);
    std::shared_ptr<const IndexedFrame> got;
    video.requestFrame(2, true, [&](std::shared_ptr<const IndexedFrame> f) { got = std::move(f); });
    source.answer();
    ASSERT_TRUE(got);
    EXPECT_EQ(renderer.scaled.size(), 1u);
    video.setScreenSize(std::nullopt);
    ASSERT_EQ(presenter.shown.size(), 3u);
    EXPECT_EQ(presenter.shown.back().overlay->width, width);
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

}

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

}

TEST_F(VideoTest, PlaybackShowsThePlayersFramesAndPausesOnTheExactIndexedFrame)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    video.setSubtitles({std::byte{'x'}});
    EXPECT_FALSE(video.play());
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    video.showFrame(2);
    source.answer();
    ASSERT_TRUE(video.play());
    EXPECT_TRUE(video.playing());
    player.opened();
    player.delivered();
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.mkv", "seek 80000", "play"}));
    video.generalFrame(playerFrame(), 205'000);
    EXPECT_EQ(presenter.shown.back().frame->index, -1);
    EXPECT_EQ(renderer.renderedAt.back(), 205'000);
    ASSERT_TRUE(video.pause());
    EXPECT_FALSE(video.playing());
    EXPECT_EQ(player.calls.back(), "pause");
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 5);
    source.answer();
    EXPECT_EQ(video.shownFrame(), 5);
    EXPECT_EQ(renderer.renderedAt.back(), 200'000);
    video.generalFrame(playerFrame(), 250'000);
    EXPECT_EQ(video.shownFrame(), 5);
    ASSERT_TRUE(video.play());
    player.delivered();
    EXPECT_EQ(player.calls[player.calls.size() - 2], "seek 200000");
    EXPECT_FALSE(video.stopped());
    ASSERT_TRUE(video.stop());
    source.answer();
    EXPECT_EQ(video.shownFrame(), 0);
    EXPECT_TRUE(video.stopped());
    video.showFrame(3);
    EXPECT_TRUE(video.stopped());
    ASSERT_TRUE(video.play());
    EXPECT_FALSE(video.stopped());
    ASSERT_TRUE(video.pause());
    ASSERT_TRUE(video.stop());
    EXPECT_FALSE(video.stopped());
}

TEST_F(VideoTest, PlayingReportsThePlayersFrameAndTheSeekBarSeeksIt)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    EXPECT_EQ(video.currentFrame(), 0);
    ASSERT_TRUE(video.play());
    player.opened();
    player.delivered();
    EXPECT_EQ(video.currentFrame(), 0);
    video.generalFrame(playerFrame(), 120'000);
    EXPECT_EQ(video.currentFrame(), 3);
    video.generalFrame(playerFrame(), 205'000);
    EXPECT_EQ(video.currentFrame(), 5);
    EXPECT_EQ(video.requestedFrame(), 0);
    EXPECT_EQ(std::count_if(player.calls.begin(), player.calls.end(),
                            [](const std::string &c) { return c.starts_with("seek"); }),
              1);
    ASSERT_TRUE(video.seekFrame(6));
    EXPECT_TRUE(video.playing());
    EXPECT_EQ(video.currentFrame(), 6);
    EXPECT_EQ(player.calls.back(), "seek 240000");
    player.delivered();
    EXPECT_EQ(player.calls.back(), "play");
    EXPECT_TRUE(source.frames.empty());
    video.generalFrame(playerFrame(), 250'000);
    EXPECT_EQ(video.currentFrame(), 6);
    video.generalFrame(playerFrame(), 285'000);
    EXPECT_EQ(video.currentFrame(), 7);
    EXPECT_FALSE(video.seekFrame(10));
    EXPECT_FALSE(video.seekFrame(-1));
    ASSERT_TRUE(video.pause());
    source.answer();
    EXPECT_EQ(video.shownFrame(), 7);
    EXPECT_EQ(video.currentFrame(), 7);
    const std::size_t calls = player.calls.size();
    ASSERT_TRUE(video.seekFrame(2));
    EXPECT_EQ(player.calls.size(), calls);
    EXPECT_EQ(video.currentFrame(), 2);
    source.answer();
    EXPECT_EQ(video.shownFrame(), 2);
}

TEST_F(VideoTest, APlayerTimePastTheLastFrameIsTheLastFrame)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    video.showFrame(2);
    source.answer();
    ASSERT_TRUE(video.play());
    player.opened();
    player.delivered();
    video.generalFrame(playerFrame(), 360'000);
    EXPECT_EQ(video.currentFrame(), 9);
    video.generalFrame(playerFrame(), 375'000);
    EXPECT_EQ(video.currentFrame(), 9);
    EXPECT_EQ(video.reportedFrame(), 9);
    video.generalFrame(playerFrame(), 2'000'000);
    EXPECT_EQ(video.currentFrame(), 9);
    EXPECT_EQ(video.requestedFrame(), 2);
    ASSERT_TRUE(video.pause());
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 9);
    source.answer();
    EXPECT_EQ(video.shownFrame(), 9);
    ASSERT_TRUE(video.seekFrame(4));
    source.answer();
    ASSERT_TRUE(video.play());
    player.delivered();
    video.generalFrame(playerFrame(), -5'000);
    EXPECT_EQ(video.currentFrame(), 0);
}

TEST_F(VideoTest, TheReportedFrameIsThePlayersWhilePlaying)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    EXPECT_EQ(video.reportedFrame(), std::nullopt);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    video.showFrame(2);
    source.answer();
    EXPECT_EQ(video.reportedFrame(), 2);
    video.showFrame(3);
    EXPECT_EQ(video.reportedFrame(), 2);
    source.answer();
    EXPECT_EQ(video.reportedFrame(), 3);
    ASSERT_TRUE(video.play());
    player.opened();
    player.delivered();
    EXPECT_EQ(video.reportedFrame(), 3);
    video.generalFrame(playerFrame(), 285'000);
    EXPECT_EQ(video.shownFrame(), -1);
    EXPECT_EQ(video.reportedFrame(), 7);
    ASSERT_TRUE(video.pause());
    EXPECT_EQ(video.shownFrame(), -1);
    EXPECT_EQ(video.reportedFrame(), 7);
    ASSERT_TRUE(video.play());
    player.delivered();
    EXPECT_EQ(player.calls[player.calls.size() - 2], "seek 280000");
    ASSERT_TRUE(video.stop());
    EXPECT_EQ(video.reportedFrame(), 0);
    while (!source.frames.empty())
        source.answer();
    EXPECT_EQ(video.reportedFrame(), 0);
}

TEST_F(VideoTest, JumpsWhilePlayingGoFromThePlayersFrameAndPlayOn)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    ASSERT_TRUE(video.play());
    player.opened();
    player.delivered();
    video.generalFrame(playerFrame(), 285'000);
    const auto seeks = [&] {
        return std::count_if(player.calls.begin(), player.calls.end(),
                             [](const std::string &c) { return c.starts_with("seek"); });
    };
    ASSERT_TRUE(video.seekBy(-120));
    EXPECT_TRUE(video.playing());
    EXPECT_EQ(video.requestedFrame(), 4);
    EXPECT_EQ(video.currentFrame(), 4);
    EXPECT_EQ(player.calls.back(), "seek 160000");
    player.delivered();
    EXPECT_EQ(player.calls.back(), "play");
    EXPECT_TRUE(source.frames.empty());
    video.generalFrame(playerFrame(), 205'000);
    ASSERT_TRUE(video.nextKeyframe());
    EXPECT_EQ(player.calls.back(), "seek 320000");
    EXPECT_EQ(video.currentFrame(), 8);
    player.delivered();
    video.generalFrame(playerFrame(), 330'000);
    ASSERT_TRUE(video.previousKeyframe());
    EXPECT_EQ(player.calls.back(), "seek 160000");
    player.delivered();
    video.generalFrame(playerFrame(), 170'000);
    ASSERT_TRUE(video.seekBy(60'000));
    EXPECT_EQ(player.calls.back(), "seek 360000");
    EXPECT_EQ(video.requestedFrame(), 9);
    player.delivered();
    video.generalFrame(playerFrame(), 365'000);
    ASSERT_TRUE(video.seekBy(-5'000));
    EXPECT_EQ(player.calls.back(), "seek 0");
    player.delivered();
    video.generalFrame(playerFrame(), 45'000);
    const auto before = seeks();
    const auto calls = player.calls.size();
    EXPECT_FALSE(video.step(1));
    EXPECT_FALSE(video.step(-1));
    EXPECT_EQ(video.requestedFrame(), 0);
    EXPECT_EQ(video.currentFrame(), 1);
    EXPECT_EQ(seeks(), before);
    EXPECT_EQ(player.calls.size(), calls);
    EXPECT_TRUE(source.frames.empty());
    ASSERT_TRUE(video.pause());
    source.answer();
    EXPECT_EQ(video.shownFrame(), 1);
    EXPECT_TRUE(video.step(1));
    source.answer();
    EXPECT_EQ(video.shownFrame(), 2);
    EXPECT_TRUE(video.seekBy(-1000));
    source.answer();
    EXPECT_EQ(video.shownFrame(), 0);
}

TEST_F(VideoTest, SeeksAndKeyframesFollowTheLegacyRules)
{
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    auto lands = [&](int expected) {
        if (source.frames.empty())
            return false;
        const int asked = source.frames.back().first;
        while (!source.frames.empty())
            source.answer();
        return asked == expected && video.shownFrame() == expected;
    };
    video.showFrame(2);
    EXPECT_TRUE(lands(2));
    ASSERT_TRUE(video.seekBy(100));
    EXPECT_TRUE(lands(5));
    ASSERT_TRUE(video.seekBy(-5000));
    EXPECT_TRUE(lands(0));
    ASSERT_TRUE(video.seekBy(60'000));
    EXPECT_TRUE(lands(9));
    video.seekToEnd(core::DocumentTime(200'000));
    EXPECT_TRUE(lands(4));
    video.seekToEnd(core::DocumentTime(0));
    EXPECT_TRUE(lands(0));
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
    video.open("/m/ep1.mkv", IndexRequest{1, {}});
    source.newIndex = true;
    source.handoffIndexFile = "/tmp/hikari-index-x.ffindex";
    source.finishOpen();
    source.answer();
    EXPECT_EQ(video.audioTrack(), 1);
    EXPECT_TRUE(video.newIndex());
    EXPECT_EQ(video.indexHandoff(), "/tmp/hikari-index-x.ffindex");
    player.calls.clear();
    ASSERT_TRUE(video.play());
    EXPECT_EQ(player.calls.front(), "audio track 0");
    video.open("/m/ep2.mkv");
    EXPECT_EQ(source.lastRequest.audioTrack, IndexRequest::kEveryAudioTrack);
    EXPECT_TRUE(source.lastRequest.indexFile.empty());
    source.finishOpen();
    EXPECT_EQ(video.audioTrack(), 1);
    video.close();
    EXPECT_EQ(video.audioTrack(), -1);
    EXPECT_TRUE(video.indexHandoff().empty());
}

TEST_F(VideoTest, PlayLinePlaysToTheFrameBeforeTheEnd)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    EXPECT_FALSE(video.playLine(100, 300));
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    ASSERT_TRUE(video.playLine(100, 300));
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 3);
    player.opened();
    player.delivered();
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.mkv", "seek 120000", "play"}));
    video.generalFrame(playerFrame(), 245'000);
    EXPECT_TRUE(video.playing());
    video.generalFrame(playerFrame(), 285'000);
    EXPECT_FALSE(video.playing());
    EXPECT_EQ(player.calls.back(), "pause");
    EXPECT_EQ(source.frames.back().first, 7);
    EXPECT_FALSE(video.playLine(280, 300));
    EXPECT_FALSE(video.playLine(360, 2000));
    player.calls.clear();
    ASSERT_TRUE(video.playLine(0, 2000));
    player.delivered();
    video.generalFrame(playerFrame(), 330'000);
    EXPECT_TRUE(video.playing());
    ASSERT_TRUE(video.playLine(40, 200));
    EXPECT_EQ(player.calls, (std::vector<std::string>{"seek 0", "play", "pause", "seek 40000"}));
    player.delivered();
    video.generalFrame(playerFrame(), 165'000);
    EXPECT_FALSE(video.playing());
    ASSERT_TRUE(video.playLine(-100, 20));
    player.delivered();
    video.generalFrame(playerFrame(), 365'000);
    EXPECT_TRUE(video.playing());
    ASSERT_TRUE(video.pause());
    ASSERT_TRUE(video.play());
    player.delivered();
    video.generalFrame(playerFrame(), 300'000);
    EXPECT_TRUE(video.playing());
}

TEST_F(VideoTest, ALoopedLinePlaysAgainFromItsStart)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    ASSERT_TRUE(video.playLine(100, 300, /*loop*/ true));
    EXPECT_TRUE(video.looping());
    player.opened();
    player.delivered();
    player.calls.clear();
    video.generalFrame(playerFrame(), 285'000);
    EXPECT_TRUE(video.playing());
    EXPECT_TRUE(video.looping());
    EXPECT_EQ(source.frames.back().first, 3);
    player.delivered();
    EXPECT_EQ(player.calls, (std::vector<std::string>{"pause", "seek 120000", "play"}));
    video.generalFrame(playerFrame(), 285'000);
    EXPECT_TRUE(video.playing());
    ASSERT_TRUE(video.pause());
    EXPECT_FALSE(video.looping());
    ASSERT_TRUE(video.playLine(100, 300));
    EXPECT_FALSE(video.looping());
    player.delivered();
    video.generalFrame(playerFrame(), 285'000);
    EXPECT_FALSE(video.playing());
    ASSERT_TRUE(video.playLine(100, 300, true));
    player.delivered();
    ASSERT_TRUE(video.seekToMs(160));
    EXPECT_FALSE(video.looping());
    player.delivered();
    video.generalFrame(playerFrame(), 285'000);
    EXPECT_TRUE(video.playing());
    EXPECT_FALSE(video.looping());
    ASSERT_TRUE(video.playLine(100, 300, true));
    ASSERT_TRUE(video.stop());
    EXPECT_FALSE(video.looping());
}

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
    source.pendingOpen(std::unexpected(SourceError::Cancelled));
    EXPECT_EQ(video.state(), VideoSession::State::Closed);
    video.open("/m/ep1.mkv");
    source.failure = OpenFailure{OpenStage::Indexing, "Cancelled by user"};
    source.pendingOpen(std::unexpected(SourceError::Cancelled));
    EXPECT_EQ(video.state(), VideoSession::State::Closed);
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

TEST_F(VideoTest, AChosenAudioStreamPlaysThroughBothTransports)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    source.audioTracks = {1, 2, 4};
    EXPECT_FALSE(video.selectPlaybackAudioTrack(0));
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    EXPECT_EQ(video.audioTracks(), (std::vector<int>{1, 2, 4}));
    EXPECT_EQ(video.playbackAudioTrack(), 0);
    EXPECT_FALSE(video.selectPlaybackAudioTrack(3));
    EXPECT_FALSE(video.selectPlaybackAudioTrack(-1));
    ASSERT_TRUE(video.selectPlaybackAudioTrack(2));
    EXPECT_TRUE(player.calls.empty());
    ASSERT_TRUE(video.play());
    MediaDescription d;
    d.audioTracks.resize(3);
    player.opened(d);
    player.delivered();
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.mkv", "audio track 2", "seek 0", "play"}));
    player.calls.clear();
    ASSERT_TRUE(video.selectPlaybackAudioTrack(1));
    EXPECT_EQ(player.calls, (std::vector<std::string>{"audio track 1"}));
    EXPECT_EQ(video.audioTrack(), 1);
    ASSERT_TRUE(video.pause());
    source.answer();
    player.calls.clear();
    ASSERT_TRUE(video.play());
    EXPECT_EQ(player.calls.front(), "audio track 1");
    video.open("/m/ep2.mkv");
    source.finishOpen();
    EXPECT_EQ(video.playbackAudioTrack(), 0);
}

TEST_F(VideoTest, ChapterSeeksAndTellFollowLegacy)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    EXPECT_FALSE(video.seekToMs(100));
    EXPECT_EQ(video.tell(), 0);
    video.open("/m/ep1.mkv");
    source.finishOpen();
    source.answer();
    ASSERT_TRUE(video.seekToMs(100));
    EXPECT_EQ(video.requestedFrame(), 3);
    source.answer();
    EXPECT_EQ(video.tell(), 120);
    ASSERT_TRUE(video.seekToMs(-5));
    EXPECT_EQ(video.requestedFrame(), 0);
    source.answer();
    ASSERT_TRUE(video.seekToMs(100000));
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
    ASSERT_TRUE(video.restartToggled());
    EXPECT_EQ(video.requestedFrame(), 0);
    EXPECT_TRUE(video.playing());
    player.opened();
    EXPECT_EQ(player.soughtUs, 0);
    player.delivered();
    video.generalFrame(playerFrame(), 200000);
    ASSERT_TRUE(video.restartToggled());
    EXPECT_FALSE(video.playing());
    EXPECT_EQ(video.requestedFrame(), 0);
}

TEST_F(VideoTest, AFailedPrepareIsNotRetriedUntilTheVideoLoadsAgain)
{
    FakePlayer player;
    video.setGeneralPlayer(&player);
    video.setPresenter(&presenter);
    video.open("/m/ep1.avi");
    source.finishOpen();
    source.answer();
    video.preparePlayer();
    video.preparePlayer();
    ASSERT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.avi"}));
    player.failed();
    EXPECT_TRUE(video.playerFailed());
    EXPECT_FALSE(video.playerHasVideo());
    video.showFrame(2);
    source.answer();
    video.preparePlayer();
    EXPECT_FALSE(video.play());
    EXPECT_FALSE(video.playing());
    EXPECT_FALSE(video.playLine(0, 200));
    EXPECT_EQ(player.calls, (std::vector<std::string>{"open /m/ep1.avi"}));
    video.open("/m/ep1.avi");
    source.finishOpen();
    EXPECT_FALSE(video.playerFailed());
    video.preparePlayer();
    EXPECT_EQ(player.calls.size(), 2u);
    player.failed();
    EXPECT_TRUE(video.playerFailed());
    FakePlayer other;
    video.setGeneralPlayer(&other);
    EXPECT_FALSE(video.playerFailed());
    video.preparePlayer();
    EXPECT_EQ(other.calls, (std::vector<std::string>{"open /m/ep1.avi"}));
    other.opened();
    EXPECT_TRUE(video.playerHasVideo());
}

TEST_F(VideoTest, TheDocumentMatrixSetsTheSourcesInputMatrix)
{
    std::vector<std::string> logged;
    video.setLog([&](const std::string &message) { logged.push_back(message); });
    video.setPresenter(&presenter);
    source.colorSpace = 1;
    source.colorRange = 1;
    video.setMatrix("TV.601");
    video.open("/m/ep1.mkv");
    source.finishOpen();
    ASSERT_EQ(source.matrixCalls, (std::vector<std::pair<int, int>>{{5, 1}}));
    EXPECT_EQ(source.framesBeforeMatrix, 0u);
    ASSERT_EQ(source.frames.size(), 1u);
    source.answer();
    EXPECT_EQ(video.colourMatrix().applied(), "TV.601");
    using Input = LegacyColourMatrix::Input;
    EXPECT_EQ(video.playerColour(), (Input{5, 1}));
    video.setMatrix("TV.709");
    EXPECT_EQ(source.matrixCalls.back(), (std::pair<int, int>{1, 1}));
    ASSERT_EQ(source.frames.size(), 1u);
    EXPECT_EQ(source.frames[0].first, 0);
    EXPECT_EQ(source.framesBeforeMatrix, 0u);
    source.answer();
    video.setMatrix("TV.709");
    EXPECT_EQ(source.matrixCalls.size(), 2u);
    EXPECT_TRUE(source.frames.empty());
    source.refuseMatrix = true;
    video.setMatrix("TV.601");
    EXPECT_EQ(logged, (std::vector<std::string>{"Cannot change YCbCr matrix"}));
    EXPECT_EQ(video.colourMatrix().applied(), "TV.709");
    EXPECT_EQ(video.playerColour(), (Input{1, 1}));
    source.refuseMatrix = false;
    source.colorSpace = 6;
    video.setMatrix("TV.709");
    video.open("/m/ep2.mkv");
    const auto before = source.matrixCalls.size();
    source.finishOpen();
    EXPECT_EQ(source.matrixCalls.size(), before);
    EXPECT_EQ(video.colourMatrix().source(), "TV.601");
    EXPECT_EQ(video.playerColour(), (Input{6, 1}));
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

namespace {

PlaybackPipeline::Make pooledPlayerFrame()
{
    return [](FramePool &pool) {
        auto f = pool.acquire();
        *f = playerFrame();
        return f;
    };
}

struct PostQueue {
    std::mutex mutex;
    std::deque<std::function<void()>> tasks;
    PlaybackPipeline::Post post()
    {
        return [this](std::function<void()> task) {
            std::lock_guard lock(mutex);
            tasks.push_back(std::move(task));
        };
    }
    bool drain()
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            {
                std::lock_guard lock(mutex);
                if (!tasks.empty())
                    break;
            }
            if (std::chrono::steady_clock::now() > until)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (;;) {
            std::function<void()> task;
            {
                std::lock_guard lock(mutex);
                if (tasks.empty())
                    return true;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            task();
        }
    }
};

struct PlayingTest : VideoTest {
    FakePlayer player;
    int notified = 0;
    void SetUp() override
    {
        video.setGeneralPlayer(&player);
        video.setPresenter(&presenter);
        video.setSubtitles({std::byte{'x'}});
        video.open("/m/ep1.mkv");
        source.finishOpen();
        source.answer();
        ASSERT_TRUE(video.play());
        player.opened();
        player.delivered();
        video.setObserver([this] { ++notified; });
    }
};

}

TEST_F(PlayingTest, APlayerFrameIsShownWithTheWorkersOverlayAndOneNotification)
{
    const auto renders = renderer.renderedAt.size();
    const auto shown = presenter.shown.size();
    video.submitGeneralFrame(205'000, 245'000, pooledPlayerFrame());
    ASSERT_EQ(presenter.shown.size(), shown + 1);
    const auto &p = presenter.shown.back();
    EXPECT_EQ(p.frame->index, -1);
    ASSERT_TRUE(p.overlay);
    EXPECT_TRUE(p.overlayBoundsKnown);
    EXPECT_EQ(renderer.renderedAt.size(), renders + 1);
    EXPECT_EQ(renderer.renderedAt.back(), 205'000);
    EXPECT_EQ(notified, 1);
    video.submitGeneralFrame(245'000, 285'000, pooledPlayerFrame());
    EXPECT_EQ(notified, 2);
    const auto c = video.pipelineCounters();
    ASSERT_TRUE(c);
    EXPECT_EQ(c->delivered, 2u);
}

TEST_F(PlayingTest, AnOlderPlayerFrameThanTheShownOneIsDropped)
{
    video.generalFrame(playerFrame(), 245'000);
    const auto shown = presenter.shown.size();
    video.generalFrame(playerFrame(), 205'000);
    EXPECT_EQ(presenter.shown.size(), shown);
    EXPECT_EQ(notified, 1);
}

TEST_F(PlayingTest, AResultFromBeforeAPauseIsDropped)
{
    PostQueue queue;
    video.setFrameWorker(queue.post());
    video.submitGeneralFrame(205'000, std::nullopt, pooledPlayerFrame());
    ASSERT_TRUE(video.pause());
    while (!source.frames.empty())
        source.answer();
    const auto shown = presenter.shown.size();
    ASSERT_TRUE(queue.drain());
    EXPECT_EQ(presenter.shown.size(), shown);
    EXPECT_EQ(video.shownFrame(), 0);
    EXPECT_GE(presenter.shown.back().frame->index, 0);
    ASSERT_TRUE(video.play());
    player.delivered();
    video.submitGeneralFrame(245'000, std::nullopt, pooledPlayerFrame());
    ASSERT_TRUE(queue.drain());
    EXPECT_EQ(presenter.shown.back().frame->index, -1);
    video.setFrameWorker(nullptr);
}

TEST_F(PlayingTest, NewSubtitlesRenderTheWorkersFrameAgain)
{
    PostQueue queue;
    video.setFrameWorker(queue.post());
    video.submitGeneralFrame(205'000, std::nullopt, pooledPlayerFrame());
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            {
                std::lock_guard lock(queue.mutex);
                if (!queue.tasks.empty())
                    break;
            }
            ASSERT_LT(std::chrono::steady_clock::now(), until);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    video.setSubtitles({std::byte{'y'}});
    ASSERT_TRUE(queue.drain());
    EXPECT_EQ(presenter.shown.back().frame->index, -1);
    EXPECT_EQ(std::count(renderer.renderedAt.begin(), renderer.renderedAt.end(), 205'000), 2);
    EXPECT_EQ(renderer.renderedAt.back(), 205'000);
    video.setFrameWorker(nullptr);
}
