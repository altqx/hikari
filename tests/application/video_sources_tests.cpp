// V3: video sources, tracks and chapters without Qt, against legacy
// VideoBox::NextFile/NextChap/PrevChap/ContextMenu/OpenKeyframes, DummyVideo,
// ProviderDummy, colorspace.cpp and AudioDisplay::GetBoundarySnap at
// 20d647c4. The dummy's expected pixels and counts were printed by legacy's
// own functions (colorspace.cpp:44-142 and 253-290, ProviderDummy.cpp:98-142,
// DummyVideo.cpp:94-97 and 118-140) copied verbatim into a standalone
// program, built without floating-point contraction as this library is.

#include "hikari/application/video_session.h"
#include "hikari/application/video_sources.h"

#include <gtest/gtest.h>

#include <array>
#include <climits>

using namespace hikari;
using namespace hikari::application;

namespace {

std::array<int, 4> pixel(const std::vector<std::byte> &bgra, int width, int x, int y)
{
    const std::size_t k = (static_cast<std::size_t>(y) * width + x) * 4;
    return {int(bgra[k]), int(bgra[k + 1]), int(bgra[k + 2]), int(bgra[k + 3])};
}

} // namespace

// VideoBox.cpp:719-723: lower-cased extension, avs left out.
TEST(NextFile, LegacyVideoExtensions)
{
    for (const char *p : {"a.avi", "a.MP4", "a.mkv", "a.ogm", "a.wmv", "a.asf", "a.rmvb", "a.rm", "a.3gp", "a.ts",
                          "a.M2TS", "a.mpg", "a.mpeg", "dir.v/a.Mkv",
                          "mkv"}) // AfterLast('.') without a dot is the whole name
        EXPECT_TRUE(isNextFileVideo(p)) << p;
    for (const char *p : {"a.avs", "a.ass", "a.mov", "a.webm", "a.mkv.txt", "a.", "/v/mkv/notes"})
        EXPECT_FALSE(isNextFileVideo(p)) << p;
}

// VideoBox::NextFile (VideoBox.cpp:682-739) over the folder's listing as the
// platform lists it (wxDir::GetAllFiles is unsorted).
TEST(NextFile, WalksTheListingInItsOwnOrderSkippingOtherFiles)
{
    const std::vector<std::string> files{"/v/c.mkv", "/v/notes.txt", "/v/a.mkv", "/v/b.ass", "/v/b.MP4", "/v/z.avs"};
    NextFileWalker walker;
    auto step = walker.step(files, "/v/c.mkv", true);
    EXPECT_EQ(step.kind, NextFileStep::Kind::Open);
    EXPECT_EQ(step.path, "/v/a.mkv"); // the listing's next video, not the name order
    step = walker.step(files, "/v/a.mkv", true);
    EXPECT_EQ(step.path, "/v/b.MP4");
    // after the last video only files legacy does not open (avs): back to the start, play toggled
    step = walker.step(files, "/v/b.MP4", true);
    EXPECT_EQ(step.kind, NextFileStep::Kind::Restart);
    step = walker.step(files, "/v/b.MP4", false);
    EXPECT_EQ(step.path, "/v/a.mkv");
    step = walker.step(files, "/v/a.mkv", false);
    EXPECT_EQ(step.path, "/v/c.mkv");
}

TEST(NextFile, EndsRestartAndAMissingFileUsesTheRememberedPosition)
{
    const std::vector<std::string> files{"/v/a.mkv", "/v/b.mkv", "/v/c.mkv"};
    NextFileWalker walker;
    // first and last: Seek(0); Pause(false), the position clamped
    EXPECT_EQ(walker.step(files, "/v/a.mkv", false).kind, NextFileStep::Kind::Restart);
    EXPECT_EQ(walker.position(), 0);
    EXPECT_EQ(walker.step(files, "/v/c.mkv", true).kind, NextFileStep::Kind::Restart);
    EXPECT_EQ(walker.position(), 2);
    // the current file is not in the folder (renamed meanwhile): actualFile as left
    auto step = walker.step(files, "/v/gone.mkv", false);
    EXPECT_EQ(step.path, "/v/b.mkv");
    step = walker.step(files, "/v/gone.mkv", true);
    EXPECT_EQ(step.path, "/v/c.mkv");
    // an empty folder: next restarts (actualFile >= count - 1)
    NextFileWalker empty;
    EXPECT_EQ(empty.step(std::vector<std::string>{}, "/v/a.mkv", true).kind, NextFileStep::Kind::Restart);
    EXPECT_EQ(empty.step(std::vector<std::string>{}, "/v/a.mkv", false).kind, NextFileStep::Kind::Restart);
    // a folder that could not be opened (wxDir not opened: `files` is not
    // cleared): the last listing is walked again
    step = walker.step(std::nullopt, "/v/a.mkv", true);
    EXPECT_EQ(step.path, "/v/b.mkv");
    EXPECT_EQ(empty.step(std::nullopt, "?dummy:25:10:8:4:0:0:0:", true).kind, NextFileStep::Kind::Restart);
}

// DummyVideo::GetDummyText (DummyVideo.cpp:118-140).
TEST(DummyVideo, DialogTextAsLegacyWritesIt)
{
    // the defaults: 23.976, 0:25:00.00, 1920x1080, &HFEA32F&
    auto text = dummyVideoText("23.976", 1500000, 1920, 1080, 0x2F, 0xA3, 0xFE, false);
    ASSERT_TRUE(text);
    EXPECT_EQ(*text, "?dummy:23.976000:35964:1920:1080:47:163:254:");
    text = dummyVideoText("29.97", 60000, 640, 480, 0, 0, 0, true);
    ASSERT_TRUE(text);
    EXPECT_EQ(*text, "?dummy:29.969999:1798:640:480:0:0:0:c"); // (float)29.97 printed with %f
    EXPECT_EQ(dummyVideoDialogFrames(), 35964); // "This gives %i frames", never updated
    // "Invalid FPS value.": below 15, above 120, or not one number
    for (const char *fps : {"14.99", "120.5", "1.2.3", "", "."})
        EXPECT_EQ(dummyVideoText(fps, 60000, 640, 480, 0, 0, 0, false).error(), DummyVideoError::InvalidFps) << fps;
    EXPECT_TRUE(dummyVideoText("15", 60000, 640, 480, 0, 0, 0, false));
    EXPECT_TRUE(dummyVideoText("120", 60000, 640, 480, 0, 0, 0, false));
    // a duration shorter than a frame gives 0 frames, which the provider refuses
    text = dummyVideoText("25", 30, 640, 480, 0, 0, 0, false);
    ASSERT_TRUE(text);
    EXPECT_FALSE(parseDummyVideo(*text));
}

// ProviderDummy::ParseDummyData (ProviderDummy.cpp:144-223).
TEST(DummyVideo, ParsesWhatTheProviderAccepts)
{
    const auto video = parseDummyVideo("?dummy:23.976000:35964:1920:1080:47:163:254:c");
    ASSERT_TRUE(video);
    EXPECT_FLOAT_EQ(video->fps, 23.976f);
    EXPECT_EQ(video->frames, 35964);
    EXPECT_EQ(video->width, 1920);
    EXPECT_EQ(video->height, 1080);
    EXPECT_EQ(video->red, 47);
    EXPECT_EQ(video->green, 163);
    EXPECT_EQ(video->blue, 254);
    EXPECT_TRUE(video->pattern);
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:640:480:0:0:0")); // a field missing
    EXPECT_FALSE(parseDummyVideo("?dummy:x:10:640:480:0:0:0:"));  // fps not a number
    EXPECT_FALSE(parseDummyVideo("?dummy:25:0:640:480:0:0:0:"));  // no frames
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:0:480:0:0:0:"));   // no width
    // V3-dummy-negative-size: legacy let a negative size through (only 0 was
    // refused, ProviderDummy.cpp:198-201) and then could not make the frame
    // buffer (m_framePlane = height * width * 4 < 0)
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:-8:4:0:0:0:"));
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:8:-4:0:0:0:"));
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:-8:-4:0:0:0:"));
    EXPECT_TRUE(parseDummyVideo("?dummy:25:10:1:1:0:0:0:"));
    EXPECT_TRUE(isDummyVideo("?dummy:25:10:640:480:0:0:0:"));
    EXPECT_FALSE(isDummyVideo("/v/a.mkv"));
}

// ProviderDummy::GenerateFrame and Timebase::FromFps.
TEST(DummyVideo, FramesAndTimecodesAsLegacyMakesThem)
{
    DummyVideo plain{25.f, 3, 4, 2, 47, 163, 254, false};
    const auto frame = dummyVideoFrame(plain);
    ASSERT_EQ(frame.size(), 4u * 2u * 4u);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            EXPECT_EQ(pixel(frame, 4, x, y), (std::array<int, 4>{254, 163, 47, 255}));
    // the checkerboard: 10 px squares, the first in the colour, the other 24
    // lighter through legacy's HSL with blue and green swapped (93, 184, 254)
    DummyVideo board{25.f, 1, 25, 25, 47, 163, 254, true};
    const auto b = dummyVideoFrame(board);
    const std::array<int, 4> colour{254, 163, 47, 255}, lighter{254, 184, 93, 255};
    EXPECT_EQ(pixel(b, 25, 0, 0), colour);
    EXPECT_EQ(pixel(b, 25, 9, 9), colour);
    EXPECT_EQ(pixel(b, 25, 10, 0), lighter);
    EXPECT_EQ(pixel(b, 25, 0, 10), lighter);
    EXPECT_EQ(pixel(b, 25, 10, 10), colour);
    EXPECT_EQ(pixel(b, 25, 20, 0), colour);
    EXPECT_EQ(pixel(b, 25, 24, 24), colour);
    // black and white wrap the byte lightness as legacy does
    DummyVideo black{25.f, 1, 11, 1, 0, 0, 0, true}, white{25.f, 1, 11, 1, 255, 255, 255, true};
    EXPECT_EQ(pixel(dummyVideoFrame(black), 11, 10, 0), (std::array<int, 4>{24, 24, 24, 255}));
    EXPECT_EQ(pixel(dummyVideoFrame(white), 11, 10, 0), (std::array<int, 4>{231, 231, 231, 255}));
    DummyVideo red{25.f, 1, 11, 1, 128, 0, 0, true};
    EXPECT_EQ(pixel(dummyVideoFrame(red), 11, 10, 0), (std::array<int, 4>{0, 0, 176, 255}));
    // frame n at n * (1000.f / fps) ms, truncated: the duration is frames * that
    DummyVideo film{23.976f, 35964, 2, 2, 0, 0, 0, false};
    const auto times = dummyVideoTimecodes(film);
    ASSERT_EQ(times.size(), 35964u);
    EXPECT_EQ(times[0], 0);
    EXPECT_EQ(times[1], 41);
    EXPECT_EQ(times[2], 83);
    EXPECT_EQ(times[3], 125);
    EXPECT_EQ(times[24], 1001);
    EXPECT_EQ(times[1000], 41708);
    EXPECT_EQ(times[35963], 1499958);
}

// The dummy goes through the session as any video: its frames, its legacy
// timebase and no audio; other paths reach the media helper's port.
TEST(DummyVideo, SessionShowsTheDummyFromItsOwnSource)
{
    struct Media : IndexedSourcePort {
        int opens = 0, cancels = 0;
        std::uint64_t open(const std::string &, Progress, Opened) override { return ++opens; }
        void cancelOpen() override { ++cancels; }
        void frame(int, FrameReady) override {}
        void openAudio(int, AudioOpened) override {}
        void audio(std::int64_t, std::int64_t, AudioReady) override {}
        void beginPcm(std::int64_t, std::int64_t, int, int, PcmBegun) override {}
        void nextPcm(std::int64_t, PcmReady) override {}
        void cancelReads() override {}
        std::uint64_t generation() const override { return 7; }
    } media;
    struct Renderer : SubtitleRendererPort {
        std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot) override { return 1; }
        std::expected<OverlayFrame, RenderError> render(core::DocumentTime, int, int) override
        {
            return std::unexpected(RenderError::NoSnapshot);
        }
    } renderer;
    DummyVideoSource source(media);
    VideoSession session(source, renderer);
    session.open("?dummy:25.000000:50:8:4:1:2:3:");
    ASSERT_EQ(session.state(), VideoSession::State::Ready);
    EXPECT_TRUE(session.dummy());
    EXPECT_EQ(media.opens, 0);
    EXPECT_GT(source.generation(), 7u);
    EXPECT_EQ(session.frameCount(), 50);
    EXPECT_FALSE(session.hasAudio());
    EXPECT_EQ(session.legacyTimebase().msAt(49), 1960);
    ASSERT_TRUE(session.lastFrame());
    EXPECT_EQ(session.lastFrame()->width, 8);
    EXPECT_EQ(pixel(session.lastFrame()->bgra, 8, 7, 3), (std::array<int, 4>{3, 2, 1, 255}));
    EXPECT_EQ(session.sourceGeometry().width, 8);
    EXPECT_FALSE(session.play()); // no file for the general player
    session.seekToMs(1000);
    EXPECT_EQ(session.shownFrame(), 25);
    // a text the provider refuses fails the open
    session.open("?dummy:25:0:8:4:1:2:3:");
    EXPECT_EQ(session.state(), VideoSession::State::Failed);
    // a negative size is refused as the open (V3-dummy-negative-size)
    session.open("?dummy:25.000000:50:8:4:1:2:3:");
    ASSERT_EQ(session.state(), VideoSession::State::Ready);
    session.open("?dummy:25:10:-8:4:1:2:3:");
    EXPECT_EQ(session.state(), VideoSession::State::Failed);
    EXPECT_FALSE(session.lastFrame());
    session.open("?dummy:25:10:8:-4:1:2:3:");
    EXPECT_EQ(session.state(), VideoSession::State::Failed);
    // a file goes to the media helper
    session.open("/v/a.mkv");
    EXPECT_EQ(media.opens, 1);
    EXPECT_EQ(source.generation(), 7u);
    EXPECT_FALSE(session.dummy());
}

// A dummy's text, refused or not, reports no failure of the helper's: an
// earlier file's failure (FFMS2's stage and text) stays with that file.
// Legacy ProviderDummy logs nothing when its text is refused
// (ProviderDummy.cpp:98-142), so the session gives its own status.
TEST(DummyVideo, ARefusedTextDoesNotReportTheLastFilesFailure)
{
    struct Media : IndexedSourcePort {
        std::uint64_t open(const std::string &, Progress, Opened done) override
        {
            done(std::unexpected(SourceError::BackendFailure));
            return 1;
        }
        void cancelOpen() override {}
        void frame(int, FrameReady) override {}
        void openAudio(int, AudioOpened) override {}
        void audio(std::int64_t, std::int64_t, AudioReady) override {}
        void beginPcm(std::int64_t, std::int64_t, int, int, PcmBegun) override {}
        void nextPcm(std::int64_t, PcmReady) override {}
        void cancelReads() override {}
        std::uint64_t generation() const override { return 1; }
        std::optional<OpenFailure> openFailure() const override
        {
            return OpenFailure{OpenStage::Source, "no source"};
        }
    } media;
    struct Renderer : SubtitleRendererPort {
        std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot) override { return 1; }
        std::expected<OverlayFrame, RenderError> render(core::DocumentTime, int, int) override
        {
            return std::unexpected(RenderError::NoSnapshot);
        }
    } renderer;
    DummyVideoSource source(media);
    VideoSession session(source, renderer);
    session.open("/v/a.mkv");
    ASSERT_EQ(session.state(), VideoSession::State::Failed);
    ASSERT_TRUE(session.openFailure());
    EXPECT_EQ(session.openFailure()->stage, OpenStage::Source);
    session.open("?dummy:25:0:8:4:1:2:3:"); // 0 frames: refused
    ASSERT_EQ(session.state(), VideoSession::State::Failed);
    EXPECT_FALSE(source.openFailure());
    EXPECT_FALSE(session.openFailure());
    session.open("?dummy:25:10:8:4:1:2:3:");
    ASSERT_EQ(session.state(), VideoSession::State::Ready);
    EXPECT_FALSE(source.openFailure());
    session.open("/v/a.mkv"); // the file again: its own failure
    ASSERT_TRUE(session.openFailure());
    EXPECT_EQ(session.openFailure()->message, "no source");
}

// VideoBox::NextChap / PrevChap (VideoBox.cpp:1420-1468) with prevchap.
TEST(Chapters, NextAndPreviousAsLegacyWalksThem)
{
    const std::vector<int> c{0, 60000, 120000};
    int prev = -1;
    // inside chapter 0: the next is 1
    EXPECT_EQ(nextChapter(c, 1000, prev), 1);
    EXPECT_EQ(prev, 1);
    // at chapter 1's start (after jumping there): j = 1, jj = 2
    EXPECT_EQ(nextChapter(c, 60000, prev), 2);
    // in the last chapter: wraps to 0
    EXPECT_EQ(nextChapter(c, 125000, prev), 0);
    // before the first chapter starts (chapters[0] >= time): the first
    const std::vector<int> late{5000, 60000};
    prev = -1;
    EXPECT_EQ(nextChapter(late, 1000, prev), 0);
    // again at the same time: jj == prevchap moves on
    EXPECT_EQ(nextChapter(late, 1000, prev), 1);
    // previous: the chapter holding the time, or the one before when that
    // was the last jumped to
    prev = -1;
    EXPECT_EQ(previousChapter(c, 70000, prev), 1);
    EXPECT_EQ(previousChapter(c, 60000, prev), 0);
    EXPECT_EQ(previousChapter(c, 0, prev), 2); // jj 0 == prevchap: wraps to the last
    prev = -1;
    EXPECT_EQ(previousChapter(c, 125000, prev), 2);
    int none = -1;
    EXPECT_FALSE(nextChapter({}, 0, none));
    EXPECT_FALSE(previousChapter({}, 0, none));
}

// VideoBox.cpp:1010-1016 with Menu.cpp:764-767: the first radio item gets
// the dot.
TEST(Chapters, TheMenuMarksTheFirstChapterNotYetLeft)
{
    const std::vector<int> c{5000, 60000, 120000};
    EXPECT_EQ(currentChapter(c, 0), 0); // before the first: the first
    EXPECT_EQ(currentChapter(c, 59999), 0);
    EXPECT_EQ(currentChapter(c, 60000), 1);
    EXPECT_EQ(currentChapter(c, INT_MAX - 1), 2);
    EXPECT_EQ(currentChapter({}, 0), -1);
}

// ProviderFFMS2::Init's track description (ProviderFFMS2.cpp:213-247).
TEST(Streams, AudioLabelsAsLegacyDescribesTracks)
{
    AudioTrack t;
    t.codec = "aac";
    EXPECT_EQ(audioStreamLabel(t), "A: Untitled (aac)");
    t.hasLanguage = true;
    t.language = "jpn";
    EXPECT_EQ(audioStreamLabel(t), "A: jpn (aac)");
    t.hasName = true;
    t.name = "Main";
    EXPECT_EQ(audioStreamLabel(t), "A: Main [jpn] (aac)");
    // "Name [code]" in the language field: the code, and the name when none
    AudioTrack l;
    l.codec = "flac";
    l.hasLanguage = true;
    l.language = "English [eng]";
    EXPECT_EQ(audioStreamLabel(l), "A: eng (flac)");
    l.hasName = true;
    l.name = "";
    EXPECT_EQ(audioStreamLabel(l), "A: English  [eng] (flac)");
}

// VideoBox::OpenKeyframes without video (VideoBox.cpp:1730-1731) and
// AudioDisplay::GetBoundarySnap without video (AudioDisplay.cpp:2506-2509).
TEST(Keyframes, WithoutVideoAtFilmRate)
{
    // Timebase::FromFps(24000.f / 1001.f): frame * (double)(1000.f / fps), truncated
    // (41.708332 ms a frame: 24 frames are 999.99997 ms)
    EXPECT_EQ(keyframesWithoutVideo({24, 0, 48, 24, 1}), (std::vector<int>{0, 41, 1000, 2001}));
    EXPECT_EQ(keyframeSnapWithoutVideo(1000), 970); // ZEROIT(1000 - 21)
    EXPECT_EQ(keyframeSnapWithoutVideo(41), 20);
    EXPECT_EQ(keyframeSnapWithoutVideo(0), -20); // ZEROIT(-21): C division truncates
}
