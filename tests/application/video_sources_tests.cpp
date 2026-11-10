#include "hikari/application/video_details.h"
#include "hikari/application/video_session.h"
#include "hikari/application/video_sources.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>

using namespace hikari;
using namespace hikari::application;

namespace {

std::array<int, 4> pixel(const std::vector<std::byte> &bgra, int width, int x, int y)
{
    const std::size_t k = (static_cast<std::size_t>(y) * width + x) * 4;
    return {int(bgra[k]), int(bgra[k + 1]), int(bgra[k + 2]), int(bgra[k + 3])};
}

}

TEST(NextFile, VideoExtensions)
{
    for (const char *p : {"a.avi", "a.MP4", "a.mkv", "a.ogm", "a.wmv", "a.asf", "a.rmvb", "a.rm", "a.3gp", "a.ts",
                          "a.M2TS", "a.mpg", "a.mpeg", "a.WebM", "dir.v/a.Mkv",
                          "mkv"})
        EXPECT_TRUE(isNextFileVideo(p)) << p;
    for (const char *p : {"a.avs", "a.ass", "a.mov", "a.mkv.txt", "a.", "/v/mkv/notes"})
        EXPECT_FALSE(isNextFileVideo(p)) << p;
}

TEST(NextFile, WalksTheListingInNameOrderSkippingOtherFiles)
{
    const std::vector<std::string> files{"/v/c.mkv", "/v/notes.txt", "/v/a.mkv", "/v/b.ass", "/v/b.MP4", "/v/z.avs"};
    NextFileWalker walker;
    auto step = walker.step(files, "/v/a.mkv", true);
    EXPECT_EQ(step.kind, NextFileStep::Kind::Open);
    EXPECT_EQ(step.path, "/v/b.MP4");
    step = walker.step(files, "/v/b.MP4", true);
    EXPECT_EQ(step.path, "/v/c.mkv");
    step = walker.step(files, "/v/c.mkv", true);
    EXPECT_EQ(step.kind, NextFileStep::Kind::Restart);
    step = walker.step(files, "/v/c.mkv", false);
    EXPECT_EQ(step.path, "/v/b.MP4");
    step = walker.step(files, "/v/b.MP4", false);
    EXPECT_EQ(step.path, "/v/a.mkv");
    EXPECT_EQ(walker.step(files, "/v/a.mkv", false).kind, NextFileStep::Kind::Restart);
}

TEST(NextFile, EpisodesInNaturalOrderWhateverTheListing)
{
    const std::vector<std::string> files{"/v/Ep 10.mkv", "/v/Ep 2.mkv", "/v/ep 1.mkv", "/v/Ep 3.MKV", "/v/Ep 9.mkv"};
    NextFileWalker walker;
    auto step = walker.step(files, "/v/ep 1.mkv", true);
    EXPECT_EQ(step.path, "/v/Ep 2.mkv");
    step = walker.step(files, "/v/Ep 2.mkv", true);
    EXPECT_EQ(step.path, "/v/Ep 3.MKV");
    step = walker.step(std::nullopt, "/v/Ep 3.MKV", true);
    EXPECT_EQ(step.path, "/v/Ep 9.mkv");
    step = walker.step(files, "/v/Ep 9.mkv", true);
    EXPECT_EQ(step.path, "/v/Ep 10.mkv");
    EXPECT_EQ(walker.step(files, "/v/Ep 10.mkv", true).kind, NextFileStep::Kind::Restart);
    step = walker.step(files, "/v/Ep 10.mkv", false);
    EXPECT_EQ(step.path, "/v/Ep 9.mkv");
    step = walker.step(files, "/v/Ep 2.mkv", false);
    EXPECT_EQ(step.path, "/v/ep 1.mkv");
    EXPECT_EQ(walker.step(files, "/v/ep 1.mkv", false).kind, NextFileStep::Kind::Restart);
}

namespace {

std::vector<std::string> naturallySorted(std::vector<std::string> names, const NameLower &lower = {})
{
    std::sort(names.begin(), names.end(),
              [&](const std::string &a, const std::string &b) { return naturalNameCompare(a, b, lower) < 0; });
    return names;
}

int sign(int v)
{
    return v < 0 ? -1 : v > 0 ? 1 : 0;
}

char32_t testLower(char32_t c)
{
    if (c >= U'A' && c <= U'Z')
        return c + (U'a' - U'A');
    if (c == U'\u00C4' || c == U'\u00C9')
        return c + 0x20;
    return c;
}

}

TEST(NaturalNameOrder, DigitRunsAsNumbers)
{
    EXPECT_LT(naturalNameCompare("Ep 2.mkv", "Ep 10.mkv"), 0);
    EXPECT_GT(naturalNameCompare("Ep 10.mkv", "Ep 2.mkv"), 0);
    EXPECT_LT(naturalNameCompare("ep9.mkv", "ep10.mkv"), 0);
    EXPECT_EQ(naturallySorted({"/v/Show - 10.mkv", "/v/Show - 1.mkv", "/v/Show - 100.mkv", "/v/Show - 2.mkv",
                               "/v/Show - 9.mkv", "/v/Show - 11.mkv"}),
              (std::vector<std::string>{"/v/Show - 1.mkv", "/v/Show - 2.mkv", "/v/Show - 9.mkv", "/v/Show - 10.mkv",
                                        "/v/Show - 11.mkv", "/v/Show - 100.mkv"}));
    EXPECT_EQ(naturallySorted({"S2E1.mkv", "S10E1.mkv", "S1E10.mkv", "S1E2.mkv"}),
              (std::vector<std::string>{"S1E2.mkv", "S1E10.mkv", "S2E1.mkv", "S10E1.mkv"}));
    EXPECT_LT(naturalNameCompare("x99999999999999999999999.mkv", "x100000000000000000000000.mkv"), 0);
    EXPECT_LT(naturalNameCompare("a1.mkv", "ab.mkv"), 0);
    EXPECT_LT(naturalNameCompare("a.mkv", "a1.mkv"), 0);
}

TEST(NaturalNameOrder, CaseLeftOutThenThePlainName)
{
    EXPECT_EQ(naturallySorted({"c.MKV", "B.mkv", "a.mkv", "D.mkv"}),
              (std::vector<std::string>{"a.mkv", "B.mkv", "c.MKV", "D.mkv"}));
    EXPECT_LT(naturalNameCompare("ep 3.mkv", "Ep 4.mkv"), 0);
    EXPECT_GT(naturalNameCompare("EP 5.mkv", "ep 4.mkv"), 0);
    EXPECT_LT(naturalNameCompare("A.mkv", "a.mkv"), 0);
    EXPECT_GT(naturalNameCompare("a.mkv", "A.mkv"), 0);
    EXPECT_LT(naturalNameCompare("Ep 01.mkv", "Ep 1.mkv"), 0);
    EXPECT_LT(naturalNameCompare("Ep 01.mkv", "Ep 2.mkv"), 0);
    EXPECT_EQ(naturalNameCompare("Ep 1.mkv", "Ep 1.mkv"), 0);
    EXPECT_EQ(naturalNameCompare("", ""), 0);
    EXPECT_LT(naturalNameCompare("", "a"), 0);
    EXPECT_EQ(naturallySorted({"ep 1.mkv", "Ep 1.mkv", "Ep 01.mkv", "EP 001.mkv"}),
              (std::vector<std::string>{"EP 001.mkv", "Ep 01.mkv", "Ep 1.mkv", "ep 1.mkv"}));
    EXPECT_LT(naturalNameCompare("\u00c4rger.mkv", "\u00e9.mkv"), 0);
    EXPECT_LT(naturalNameCompare("\u00c4b.mkv", "\u00e4a.mkv"), 0);
    EXPECT_GT(naturalNameCompare("\u00c4b.mkv", "\u00e4a.mkv", testLower), 0);
    EXPECT_GT(naturalNameCompare("\xff.mkv", "z.mkv"), 0);
}

TEST(NaturalNameOrder, TheCaptureFolderInOrder)
{
    const std::vector<std::string> created{"ep10.mkv", "B.mkv", "notes.txt", "ep2.mkv", ".dotted.mkv", "a.b.mkv",
                                           "\u00c4rger.mkv", "ep1.mkv", "attr-hidden.mkv", "\u65e5\u672c\u8a9e.mkv",
                                           "c.MKV", "folder.mkv", "x.mkv.txt", "~tilde.avi", "a.mkv", "_under.mp4",
                                           "noext", "\u00e9.mkv"};
    const std::vector<std::string> expected{".dotted.mkv", "_under.mp4", "a.b.mkv", "a.mkv", "attr-hidden.mkv",
                                            "B.mkv", "c.MKV", "ep1.mkv", "ep2.mkv", "ep10.mkv", "folder.mkv", "noext",
                                            "notes.txt", "x.mkv.txt", "~tilde.avi", "\u00c4rger.mkv", "\u00e9.mkv",
                                            "\u65e5\u672c\u8a9e.mkv"};
    EXPECT_EQ(naturallySorted(created, testLower), expected);
    EXPECT_EQ(naturallySorted({created.rbegin(), created.rend()}, testLower), expected);
    for (const auto &a : created)
        for (const auto &b : created) {
            EXPECT_EQ(sign(naturalNameCompare(a, b, testLower)), -sign(naturalNameCompare(b, a, testLower))) << a << " " << b;
            EXPECT_EQ(naturalNameCompare(a, b, testLower) == 0, a == b) << a << " " << b;
        }
}

TEST(NextFile, TheFolderAsWxFileNameGivesIt)
{
    EXPECT_EQ(nextFileFolder("/v/a.mkv", PathFormat::Unix), "/v");
    EXPECT_EQ(nextFileFolder("/v/sub/../a.mkv", PathFormat::Unix), "/v/sub/..");
    EXPECT_EQ(nextFileFolder("/v/./a.mkv", PathFormat::Unix), "/v/.");
    EXPECT_EQ(nextFileFolder("//v//w///a.mkv", PathFormat::Unix), "/v/w");
    EXPECT_EQ(nextFileFolder("/a.mkv", PathFormat::Unix), "/");
    EXPECT_EQ(nextFileFolder("v/a.mkv", PathFormat::Unix), "v");
    EXPECT_EQ(nextFileFolder("../v/a.mkv", PathFormat::Unix), "../v");
    EXPECT_EQ(nextFileFolder("a.mkv", PathFormat::Unix), "");
    EXPECT_EQ(nextFileFolder("/v\\w/a.mkv", PathFormat::Unix), "/v\\w");
    EXPECT_EQ(nextFileFolder(R"(C:\v\a.mkv)", PathFormat::Dos), R"(C:\v)");
    EXPECT_EQ(nextFileFolder(R"(C:\v\sub\..\a.mkv)", PathFormat::Dos), R"(C:\v\sub\..)");
    EXPECT_EQ(nextFileFolder(R"(C:\v\\w/a.mkv)", PathFormat::Dos), R"(C:\v\w)");
    EXPECT_EQ(nextFileFolder(R"(C:\a.mkv)", PathFormat::Dos), R"(C:\)");
    EXPECT_EQ(nextFileFolder(R"(C:a.mkv)", PathFormat::Dos), "C:");
    EXPECT_EQ(nextFileFolder(R"(\\server\share\v\a.mkv)", PathFormat::Dos), R"(\\server\share\v)");
    EXPECT_EQ(nextFileFolder(R"(\\?\C:\v\a.mkv)", PathFormat::Dos), R"(\\?\C:\v)");
    EXPECT_EQ(nextFileFolder(R"(v\a.mkv)", PathFormat::Dos), "v");
}

TEST(NextFile, EndsRestartAndAMissingFileUsesTheRememberedPosition)
{
    const std::vector<std::string> files{"/v/a.mkv", "/v/b.mkv", "/v/c.mkv"};
    NextFileWalker walker;
    EXPECT_EQ(walker.step(files, "/v/a.mkv", false).kind, NextFileStep::Kind::Restart);
    EXPECT_EQ(walker.position(), 0);
    EXPECT_EQ(walker.step(files, "/v/c.mkv", true).kind, NextFileStep::Kind::Restart);
    EXPECT_EQ(walker.position(), 2);
    auto step = walker.step(files, "/v/gone.mkv", false);
    EXPECT_EQ(step.path, "/v/b.mkv");
    step = walker.step(files, "/v/gone.mkv", true);
    EXPECT_EQ(step.path, "/v/c.mkv");
    NextFileWalker empty;
    EXPECT_EQ(empty.step(std::vector<std::string>{}, "/v/a.mkv", true).kind, NextFileStep::Kind::Restart);
    EXPECT_EQ(empty.step(std::vector<std::string>{}, "/v/a.mkv", false).kind, NextFileStep::Kind::Restart);
    step = walker.step(std::nullopt, "/v/a.mkv", true);
    EXPECT_EQ(step.path, "/v/b.mkv");
    EXPECT_EQ(empty.step(std::nullopt, "?dummy:25:10:8:4:0:0:0:", true).kind, NextFileStep::Kind::Restart);
}

TEST(DummyVideo, DialogTextAsLegacyWritesIt)
{
    auto text = dummyVideoText("23.976", 1500000, 1920, 1080, 0x2F, 0xA3, 0xFE, false);
    ASSERT_TRUE(text);
    EXPECT_EQ(*text, "?dummy:23.976000:35964:1920:1080:47:163:254:");
    text = dummyVideoText("29.97", 60000, 640, 480, 0, 0, 0, true);
    ASSERT_TRUE(text);
    EXPECT_EQ(*text, "?dummy:29.969999:1798:640:480:0:0:0:c");
    EXPECT_EQ(dummyVideoFrames("23.976", 1500000), 35964);
    EXPECT_EQ(dummyVideoFrames("25", 1500000), 37500);
    EXPECT_EQ(dummyVideoFrames("29.97", 60000), 1798);
    EXPECT_EQ(dummyVideoFrames("25", 30), 0);
    for (const char *fps : {"14.99", "120.5", "1.2.3", "", "."})
        EXPECT_EQ(dummyVideoFrames(fps, 60000).error(), DummyVideoError::InvalidFps) << fps;
    for (const char *fps : {"14.99", "120.5", "1.2.3", "", "."})
        EXPECT_EQ(dummyVideoText(fps, 60000, 640, 480, 0, 0, 0, false).error(), DummyVideoError::InvalidFps) << fps;
    EXPECT_TRUE(dummyVideoText("15", 60000, 640, 480, 0, 0, 0, false));
    EXPECT_TRUE(dummyVideoText("120", 60000, 640, 480, 0, 0, 0, false));
    text = dummyVideoText("25", 30, 640, 480, 0, 0, 0, false);
    ASSERT_TRUE(text);
    EXPECT_FALSE(parseDummyVideo(*text));
}

TEST(DummyVideo, ParsesWhatTheProviderAccepts)
{
    const auto video = parseDummyVideo("?dummy:23.976000:35964:1920:1080:47:163:254:c");
    ASSERT_TRUE(video);
    EXPECT_FLOAT_EQ(video->rate.fps, 23.976);
    EXPECT_EQ(video->frames, 35964);
    EXPECT_EQ(video->width, 1920);
    EXPECT_EQ(video->height, 1080);
    EXPECT_EQ(video->red, 47);
    EXPECT_EQ(video->green, 163);
    EXPECT_EQ(video->blue, 254);
    EXPECT_TRUE(video->pattern);
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:640:480:0:0:0"));
    EXPECT_FALSE(parseDummyVideo("?dummy:x:10:640:480:0:0:0:"));
    EXPECT_FALSE(parseDummyVideo("?dummy:25:0:640:480:0:0:0:"));
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:0:480:0:0:0:"));
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:-8:4:0:0:0:"));
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:8:-4:0:0:0:"));
    EXPECT_FALSE(parseDummyVideo("?dummy:25:10:-8:-4:0:0:0:"));
    EXPECT_TRUE(parseDummyVideo("?dummy:25:10:1:1:0:0:0:"));
    EXPECT_TRUE(isDummyVideo("?dummy:25:10:640:480:0:0:0:"));
    EXPECT_FALSE(isDummyVideo("/v/a.mkv"));
}

TEST(DummyVideo, FramesAndTimecodesAsLegacyMakesThem)
{
    DummyVideo plain{{25.f}, 3, 4, 2, 47, 163, 254, false};
    const auto frame = dummyVideoFrame(plain);
    ASSERT_EQ(frame.size(), 4u * 2u * 4u);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            EXPECT_EQ(pixel(frame, 4, x, y), (std::array<int, 4>{254, 163, 47, 255}));
    DummyVideo board{{25.f}, 1, 25, 25, 47, 163, 254, true};
    const auto b = dummyVideoFrame(board);
    const std::array<int, 4> colour{254, 163, 47, 255}, lighter{254, 184, 93, 255};
    EXPECT_EQ(pixel(b, 25, 0, 0), colour);
    EXPECT_EQ(pixel(b, 25, 9, 9), colour);
    EXPECT_EQ(pixel(b, 25, 10, 0), lighter);
    EXPECT_EQ(pixel(b, 25, 0, 10), lighter);
    EXPECT_EQ(pixel(b, 25, 10, 10), colour);
    EXPECT_EQ(pixel(b, 25, 20, 0), colour);
    EXPECT_EQ(pixel(b, 25, 24, 24), colour);
    DummyVideo black{{25.f}, 1, 11, 1, 0, 0, 0, true}, white{{25.f}, 1, 11, 1, 255, 255, 255, true};
    EXPECT_EQ(pixel(dummyVideoFrame(black), 11, 10, 0), (std::array<int, 4>{24, 24, 24, 255}));
    EXPECT_EQ(pixel(dummyVideoFrame(white), 11, 10, 0), (std::array<int, 4>{231, 231, 231, 255}));
    DummyVideo red{{25.f}, 1, 11, 1, 128, 0, 0, true};
    EXPECT_EQ(pixel(dummyVideoFrame(red), 11, 10, 0), (std::array<int, 4>{0, 0, 176, 255}));
    DummyVideo film{{23.976f}, 35964, 2, 2, 0, 0, 0, false};
    const auto times = dummyVideoTimecodes(film);
    ASSERT_EQ(times.size(), 35964u);
    EXPECT_EQ(times[0], 0);
    EXPECT_EQ(times[1], 41);
    EXPECT_EQ(times[2], 83);
    EXPECT_EQ(times[3], 125);
    EXPECT_EQ(times[24], 1001);
    EXPECT_EQ(times[1000], 41708);
    EXPECT_EQ(times[35963], 1499958);
    EXPECT_EQ(dummyVideoAudioFrames(film, 44100), std::llround(35964 * double(1000.f / 23.976f) * 44.1));
    EXPECT_EQ(dummyVideoAudioFrames(DummyVideo{{25.f}, 50, 2, 2, 0, 0, 0, false}, 44100), 88200);
    EXPECT_EQ(dummyVideoAudioFrames(DummyVideo{{0.f}, 50, 2, 2, 0, 0, 0, false}, 44100), 0);
}

TEST(DummyVideo, ADecimalRateStillWritesTheLegacyBytes)
{
    EXPECT_EQ(dummyVideoText("23.976", 1500000, 1920, 1080, 0x2F, 0xA3, 0xFE, false),
              "?dummy:23.976000:35964:1920:1080:47:163:254:");
    const auto video = parseDummyVideo("?dummy:23.976000:35964:1920:1080:47:163:254:");
    ASSERT_TRUE(video);
    EXPECT_FALSE(video->rate.exact());
}

TEST(DummyVideo, AFractionalRateIsStoredAndTimedExactly)
{
    const auto text = dummyVideoText("24000/1001", 1500000, 1920, 1080, 0x2F, 0xA3, 0xFE, false);
    ASSERT_TRUE(text);
    EXPECT_EQ(*text, "?dummy:24000/1001:35964:1920:1080:47:163:254:");
    EXPECT_EQ(dummyVideoFrames("30000/1001", 60000), 1798);
    for (const char *fps : {"24000/", "/1001", "24000/0", "0/1001", "-24000/1001", "24000/1001/1", "1/1", "24000.5/1001",
                            "3000000000/1001"})
        EXPECT_EQ(dummyVideoFrames(fps, 60000).error(), DummyVideoError::InvalidFps) << fps;

    const auto video = parseDummyVideo(*text);
    ASSERT_TRUE(video);
    EXPECT_TRUE(video->rate.exact());
    EXPECT_EQ(video->rate.numerator, 24000);
    EXPECT_EQ(video->rate.denominator, 1001);
    const auto times = dummyVideoTimecodes(*video);
    ASSERT_EQ(times.size(), 35964u);
    EXPECT_EQ(times[1], 41);
    EXPECT_EQ(times[2], 83);
    EXPECT_EQ(times[24], 1001);
    EXPECT_EQ(times[1000], 41708);
    // 35963 × 1001 / 24 = 1499956.79; the decimal 23.976 drifts to 1499958.
    EXPECT_EQ(times[35963], 1499956);
    EXPECT_EQ(dummyVideoAudioFrames(*video, 48000), std::llround(35964.0 * 1001 * 48000 / 24000));
}

TEST(DummyVideo, AFractionalRateReachesTheTimelineExactly)
{
    struct Media : IndexedSourcePort {
        std::uint64_t open(const std::string &, Progress, Opened) override { return 1; }
        void cancelOpen() override {}
        void frame(int, FrameReady) override {}
        void openAudio(int, AudioOpened) override {}
        void audio(std::int64_t, std::int64_t, AudioReady) override {}
        void beginPcm(std::int64_t, std::int64_t, int, int, PcmBegun) override {}
        void nextPcm(std::int64_t, PcmReady) override {}
        void cancelReads() override {}
        std::uint64_t generation() const override { return 1; }
    } media;
    DummyVideoSource source(media);
    std::optional<std::expected<SourceTimeline, SourceError>> opened;
    source.open("?dummy:30000/1001:100:8:4:0:0:0:", {}, [&](auto r) { opened = std::move(r); });
    ASSERT_TRUE(opened && *opened);
    EXPECT_EQ((*opened)->fpsNumerator, 30000);
    EXPECT_EQ((*opened)->fpsDenominator, 1001);
    EXPECT_EQ((*opened)->pts.at(99), 99 * 1001 / 30);
    std::optional<std::expected<IndexedFrame, SourceError>> frame;
    source.frame(99, [&](auto r) { frame = std::move(r); });
    ASSERT_TRUE(frame && *frame);
    EXPECT_EQ((*frame)->pts, (*opened)->pts.at(99));
}

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
    EXPECT_FALSE(session.play());
    session.seekToMs(1000);
    EXPECT_EQ(session.shownFrame(), 25);
    session.open("?dummy:25:0:8:4:1:2:3:");
    EXPECT_EQ(session.state(), VideoSession::State::Failed);
    session.open("?dummy:25.000000:50:8:4:1:2:3:");
    ASSERT_EQ(session.state(), VideoSession::State::Ready);
    session.open("?dummy:25:10:-8:4:1:2:3:");
    EXPECT_EQ(session.state(), VideoSession::State::Failed);
    EXPECT_FALSE(session.lastFrame());
    session.open("?dummy:25:10:8:-4:1:2:3:");
    EXPECT_EQ(session.state(), VideoSession::State::Failed);
    session.open("/v/a.mkv");
    EXPECT_EQ(media.opens, 1);
    EXPECT_EQ(source.generation(), 7u);
    EXPECT_FALSE(session.dummy());
}

TEST(DummyVideo, SidesOver8192AreDrawnScaledButReportedAsAsked)
{
    EXPECT_EQ(dummyVideoDrawnSize(DummyVideo{{25.f}, 1, 8192, 4320, 0, 0, 0, false}), (DummyVideoSize{8192, 4320}));
    EXPECT_EQ(dummyVideoDrawnSize(DummyVideo{{25.f}, 1, 16384, 9216, 0, 0, 0, false}), (DummyVideoSize{8192, 4608}));
    EXPECT_EQ(dummyVideoDrawnSize(DummyVideo{{25.f}, 1, 300, 100000, 0, 0, 0, false}), (DummyVideoSize{25, 8192}));
    EXPECT_EQ(dummyVideoDrawnSize(DummyVideo{{25.f}, 1, 100000, 1, 0, 0, 0, false}), (DummyVideoSize{8192, 1}));
    struct Media : IndexedSourcePort {
        std::uint64_t open(const std::string &, Progress, Opened) override { return 1; }
        void cancelOpen() override {}
        void frame(int, FrameReady) override {}
        void openAudio(int, AudioOpened) override {}
        void audio(std::int64_t, std::int64_t, AudioReady) override {}
        void beginPcm(std::int64_t, std::int64_t, int, int, PcmBegun) override {}
        void nextPcm(std::int64_t, PcmReady) override {}
        void cancelReads() override {}
        std::uint64_t generation() const override { return 1; }
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
    session.open("?dummy:25.000000:2:16384:9216:1:2:3:");
    ASSERT_EQ(session.state(), VideoSession::State::Ready);
    ASSERT_TRUE(session.lastFrame());
    EXPECT_EQ(session.lastFrame()->width, 8192);
    EXPECT_EQ(session.lastFrame()->height, 4608);
    EXPECT_EQ(session.lastFrame()->bgra.size(), 8192u * 4608u * 4u);
    EXPECT_EQ(session.sourceGeometry().width, 16384);
    EXPECT_EQ(session.sourceGeometry().height, 9216);
}

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
    session.open("?dummy:25:0:8:4:1:2:3:");
    ASSERT_EQ(session.state(), VideoSession::State::Failed);
    EXPECT_FALSE(source.openFailure());
    EXPECT_FALSE(session.openFailure());
    session.open("?dummy:25:10:8:4:1:2:3:");
    ASSERT_EQ(session.state(), VideoSession::State::Ready);
    EXPECT_FALSE(source.openFailure());
    session.open("/v/a.mkv");
    ASSERT_TRUE(session.openFailure());
    EXPECT_EQ(session.openFailure()->message, "no source");
}

TEST(Chapters, NextAndPreviousAsLegacyWalksThem)
{
    const std::vector<int> c{0, 60000, 120000};
    int prev = -1;
    EXPECT_EQ(nextChapter(c, 1000, prev), 1);
    EXPECT_EQ(prev, 1);
    EXPECT_EQ(nextChapter(c, 60000, prev), 2);
    EXPECT_EQ(nextChapter(c, 125000, prev), 0);
    const std::vector<int> late{5000, 60000};
    prev = -1;
    EXPECT_EQ(nextChapter(late, 1000, prev), 0);
    EXPECT_EQ(nextChapter(late, 1000, prev), 1);
    prev = -1;
    EXPECT_EQ(previousChapter(c, 70000, prev), 1);
    EXPECT_EQ(previousChapter(c, 60000, prev), 0);
    EXPECT_EQ(previousChapter(c, 0, prev), 2);
    prev = -1;
    EXPECT_EQ(previousChapter(c, 125000, prev), 2);
    int none = -1;
    EXPECT_FALSE(nextChapter({}, 0, none));
    EXPECT_FALSE(previousChapter({}, 0, none));
}

TEST(Chapters, TheMenuMarksTheFirstChapterNotYetLeft)
{
    const std::vector<int> c{5000, 60000, 120000};
    EXPECT_EQ(currentChapter(c, 0), 0);
    EXPECT_EQ(currentChapter(c, 59999), 0);
    EXPECT_EQ(currentChapter(c, 60000), 1);
    EXPECT_EQ(currentChapter(c, INT_MAX - 1), 2);
    EXPECT_EQ(currentChapter({}, 0), -1);
}

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
    AudioTrack l;
    l.codec = "flac";
    l.hasLanguage = true;
    l.language = "English [eng]";
    EXPECT_EQ(audioStreamLabel(l), "A: eng (flac)");
    l.hasName = true;
    l.name = "";
    EXPECT_EQ(audioStreamLabel(l), "A: English  [eng] (flac)");
}

TEST(Keyframes, WithoutVideoAtFilmRate)
{
    EXPECT_EQ(keyframesWithoutVideo({24, 0, 48, 24, 1}), (std::vector<int>{0, 41, 1000, 2001}));
    EXPECT_EQ(keyframeSnapWithoutVideo(1000), 970);
    EXPECT_EQ(keyframeSnapWithoutVideo(41), 20);
    EXPECT_EQ(keyframeSnapWithoutVideo(0), -20);
}

namespace {

struct NoRenderer : SubtitleRendererPort {
    std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot) override { return 1; }
    std::expected<OverlayFrame, RenderError> render(core::DocumentTime, int, int) override
    {
        return std::unexpected(RenderError::NoSnapshot);
    }
};

struct TimelineMedia : IndexedSourcePort {
    SourceTimeline timeline;
    std::uint64_t open(const std::string &, Progress, Opened done) override
    {
        done(timeline);
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
};

}

TEST(VideoDetails, Texts)
{
    EXPECT_EQ(rateDecimal({24000, 1001}), "23.976");
    EXPECT_EQ(rateDecimal({30000, 1001}), "29.97");
    EXPECT_EQ(rateDecimal({25, 1}), "25");
    EXPECT_EQ(rateDecimal({2997, 125}), "23.976");
    EXPECT_EQ(rateDecimal({}), "");
    EXPECT_TRUE(rateIsRounded({24000, 1001}));
    EXPECT_FALSE(rateIsRounded({2997, 125}));
    EXPECT_FALSE(rateIsRounded({25, 1}));
    EXPECT_EQ(chromaSubsampling({"yuv420p10le", false, 3, 1, 1, 10}), "4:2:0");
    EXPECT_EQ(chromaSubsampling({"yuv422p", false, 3, 1, 0, 8}), "4:2:2");
    EXPECT_EQ(chromaSubsampling({"yuv444p", false, 3, 0, 0, 8}), "4:4:4");
    EXPECT_EQ(chromaSubsampling({"gray", false, 1, 0, 0, 8}), "");
    EXPECT_EQ(chromaSubsampling({"gbrp", true, 3, 0, 0, 8}), "");
    EXPECT_EQ(lengthText(1'438'000 + 970), "0:23:58.97");
    EXPECT_EQ(lengthText(3'600'000 * 2 + 5), "2:00:00.00");
}

TEST(VideoDetails, DescribeTheOpenVideo)
{
    TimelineMedia media;
    NoRenderer renderer;
    DummyVideoSource source(media);
    VideoSession session(source, renderer);
    EXPECT_FALSE(videoDetails(session));
    session.open("?dummy:24000/1001:100:1920:1080:0:0:0:");
    auto details = videoDetails(session);
    ASSERT_TRUE(details);
    EXPECT_EQ(details->path, "?dummy:24000/1001:100:1920:1080:0:0:0:");
    EXPECT_EQ(details->width, 1920);
    EXPECT_EQ(details->height, 1080);
    EXPECT_EQ(details->aspect, (Ratio{16, 9}));
    EXPECT_EQ(details->fps, (Ratio{24000, 1001}));
    EXPECT_FALSE(details->variableRate);
    EXPECT_EQ(details->frames, 100);
    EXPECT_EQ(details->lastFrameMs, 99 * 1001 / 24);
    EXPECT_TRUE(details->pixelFormat.rgb);
    EXPECT_EQ(details->pixelFormat.bitDepth, 8);
    EXPECT_FALSE(details->tagged);
    EXPECT_EQ(details->matrix, "TV.709");
    EXPECT_EQ(details->scriptMatrix, "");
    session.setMatrix("TV.601");
    EXPECT_EQ(videoDetails(session)->scriptMatrix, "TV.601");
    session.setMatrix("TV.709");
    EXPECT_EQ(videoDetails(session)->scriptMatrix, "");

    media.timeline.generation = 1;
    media.timeline.fpsNumerator = 50;
    media.timeline.fpsDenominator = 2;
    media.timeline.pts = {0, 40, 80, 130, 170};
    media.timeline.width = 1440;
    media.timeline.height = 1080;
    media.timeline.sarNum = 4;
    media.timeline.sarDen = 3;
    media.timeline.colorSpace = ffms_colour::kBt709;
    media.timeline.colorRange = ffms_colour::kRangeJpeg;
    media.timeline.pixelFormat = {"yuv420p10le", false, 3, 1, 1, 10};
    session.open("/v/a.mkv");
    details = videoDetails(session);
    ASSERT_TRUE(details);
    EXPECT_EQ(details->aspect, (Ratio{16, 9}));
    EXPECT_EQ(details->fps, (Ratio{25, 1}));
    EXPECT_TRUE(details->variableRate);
    EXPECT_EQ(details->lastFrameMs, 170);
    EXPECT_EQ(details->pixelFormat.name, "yuv420p10le");
    EXPECT_TRUE(details->tagged);
    EXPECT_EQ(details->matrix, "PC.709");
    media.timeline.pts = {0, 41, 83, 125};
    session.open("/v/b.mkv");
    EXPECT_FALSE(videoDetails(session)->variableRate);
    EXPECT_FALSE(videoDetails(session)->frameTimesReplaced);
    session.setFrameTimes(AegisubFrameTimes::fromFrameStarts({0, 100, 150, 400}));
    details = videoDetails(session);
    EXPECT_TRUE(details->frameTimesReplaced);
    EXPECT_EQ(details->fps, (Ratio{25, 1}));
    EXPECT_FALSE(details->variableRate);
    EXPECT_EQ(details->frames, 4);
    EXPECT_EQ(details->lastFrameMs, 400);
    session.setFrameTimes(std::nullopt);
    EXPECT_FALSE(videoDetails(session)->frameTimesReplaced);
    EXPECT_EQ(videoDetails(session)->lastFrameMs, 125);
    session.close();
    EXPECT_FALSE(videoDetails(session));
}
