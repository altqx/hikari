#include "hikari/application/video_analysis.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

constexpr KeyColour kBlack{0, 0, 0}, kWhite{255, 255, 255}, kGrey{128, 128, 128};
constexpr KeyColour kA{0x33, 0x22, 0x11}, kB{0x66, 0x55, 0x44}, kC{0x99, 0x88, 0x77};

AegisubFrameTimes pal()
{
    std::vector<std::int64_t> starts;
    for (std::int64_t f = 0; f < 500; ++f)
        starts.push_back(f * 40);
    return *AegisubFrameTimes::fromFrameStarts(starts);
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::u8string s(std::string_view text)
{
    return {text.begin(), text.end()};
}

ColourTrackOptions fillAndBorder()
{
    ColourTrackOptions o;
    o.colours = {true, false, true, false};
    o.skipRepeats = false;
    return o;
}

}

TEST(VideoAnalysis, AssColourIsBlueGreenRed)
{
    EXPECT_EQ(assColour(kA), u8"&H112233&");
}

TEST(VideoAnalysis, LineFramesFollowFrameFromMs)
{
    const auto times = pal();
    const auto frames = lineFrames(times, 1000, 3000);
    EXPECT_EQ(frames.first, times.frameFromMs(1000));
    EXPECT_EQ(frames.count, times.frameFromMs(3000) - times.frameFromMs(1000));
}

TEST(VideoAnalysis, ColourTagsMatchColorTrackFrameByFrame)
{
    const std::vector<std::optional<KeyColour>> colours{kA, kB, kB, std::nullopt, kC};
    const std::vector<std::int64_t> offsets{0, 40, 80, 120, 160};
    EXPECT_EQ(colourTrackTags(colours, offsets, fillAndBorder()),
              u8"\\c&H112233&\\3c&H112233&"
              u8"\\t(40,40,\\c&H445566&\\3c&H445566&)"
              u8"\\t(80,80,\\c&H445566&\\3c&H445566&)"
              u8"\\t(160,160,\\c&H778899&\\3c&H778899&)");
}

TEST(VideoAnalysis, SkipRepeatedColoursDropsTransformsThatChangeNothing)
{
    auto options = fillAndBorder();
    options.skipRepeats = true;
    const std::vector<std::optional<KeyColour>> colours{kA, kA, kB, std::nullopt, kB, kC};
    const std::vector<std::int64_t> offsets{0, 40, 80, 120, 160, 200};
    EXPECT_EQ(colourTrackTags(colours, offsets, options),
              u8"\\c&H112233&\\3c&H112233&"
              u8"\\t(80,80,\\c&H445566&\\3c&H445566&)"
              u8"\\t(200,200,\\c&H778899&\\3c&H778899&)");
}

TEST(VideoAnalysis, AlphaGoesBySignAndBackgroundBrightness)
{
    EXPECT_EQ(trackedAlpha(kBlack, kWhite, kBlack), u8"00");
    EXPECT_EQ(trackedAlpha(kBlack, kWhite, kWhite), u8"FF");
    EXPECT_EQ(trackedAlpha(kBlack, kWhite, kGrey), u8"7F");
    EXPECT_EQ(trackedAlpha(kWhite, kBlack, KeyColour{64, 64, 64}), u8"BF");
}

TEST(VideoAnalysis, AlphaTagsUseAlphaOrTheTickedLayers)
{
    ColourTrackOptions o;
    o.alpha = true;
    o.sign = kBlack;
    o.background = kWhite;
    o.skipRepeats = false;
    const std::vector<std::optional<KeyColour>> colours{kBlack, kGrey};
    const std::vector<std::int64_t> offsets{0, 40};
    EXPECT_EQ(colourTrackTags(colours, offsets, o), u8"\\alpha&H00&\\t(40,40,\\alpha&H7F&)");
    o.allAlpha = false;
    o.alphas = {true, false, true, false};
    EXPECT_EQ(colourTrackTags(colours, offsets, o), u8"\\1a&H00&\\3a&H00&\\t(40,40,\\1a&H7F&\\3a&H7F&)");
}

TEST(VideoAnalysis, TrackTagsCloseTheFirstBlockAfterRemovingWhatTheyReplace)
{
    const auto o = fillAndBorder();
    EXPECT_EQ(withColourTrack(u8"{\\pos(1,2)\\1c&H0000FF&\\3c&HFFFFFF&\\4c&H000000&\\bord2}Sign", u8"T", o),
              u8"{\\pos(1,2)\\4c&H000000&\\bord2T}Sign");
    EXPECT_EQ(withColourTrack(u8"{\\c&H1&}a{\\c&H2&}b", u8"T", o), u8"{T}a{\\c&H2&}b");
    EXPECT_EQ(withColourTrack(u8"Sign", u8"T", o), u8"{T}Sign");
}

TEST(VideoAnalysis, AlphaRemovesEveryAlphaWhenAllIsTicked)
{
    ColourTrackOptions o;
    o.alpha = true;
    EXPECT_EQ(withColourTrack(u8"{\\alpha&H00&\\1a&H10&\\3a&H20&\\c&H1&}x", u8"T", o), u8"{\\c&H1&T}x");
    o.allAlpha = false;
    o.alphas = {false, false, true, false};
    EXPECT_EQ(withColourTrack(u8"{\\alpha&H00&\\1a&H10&\\3a&H20&}x", u8"T", o), u8"{\\alpha&H00&\\1a&H10&T}x");
}

TEST(VideoAnalysis, PointPathFollowsTheTrackFromTheReferenceRow)
{
    MotionTrack track;
    track.x = {10, 12, 15.5};
    track.y = {5, 5, 8};
    track.scaleX = track.scaleY = {100, 100, 100};
    track.rotation = {0, 0, 0};
    const PointPath path({200, 100}, track, 100, 101);
    EXPECT_EQ(path.at(100), (FramePoint{198, 100}));
    EXPECT_EQ(path.at(101), (FramePoint{200, 100}));
    EXPECT_EQ(path.at(102), (FramePoint{203, 103}));
    EXPECT_FALSE(path.at(99));
    EXPECT_FALSE(path.at(103));
    EXPECT_EQ(PointPath({7, 8}).at(12345), (FramePoint{7, 8}));
}

TEST(VideoAnalysis, FindFadeTakesTheMiddleOfTheFirstMatchingFrames)
{
    const auto times = pal();
    std::vector<std::optional<double>> distances;
    for (int f = 25; f <= 75; ++f)
        distances.push_back(f < 30 || f >= 70 ? 100.0 : 1.0);
    const auto fade = findFade(times, distances, 25, 50, 1000, 3000, {});
    ASSERT_TRUE(fade);
    EXPECT_EQ(fade->inMs, 200);
    EXPECT_EQ(fade->inFrame, 30);
    EXPECT_EQ(fade->outMs, 240);
    EXPECT_EQ(fade->outFrame, 70);
}

TEST(VideoAnalysis, FindFadeChecksEveryFrameAfterTheCurrentOne)
{
    const auto times = pal();
    std::vector<std::optional<double>> distances(51, 1.0);
    distances[40] = 9.0;
    const auto fade = findFade(times, distances, 25, 50, 1000, 3000, {.in = false, .out = true});
    ASSERT_TRUE(fade);
    EXPECT_FALSE(fade->inMs);
    EXPECT_EQ(fade->outFrame, 65);
}

TEST(VideoAnalysis, FindFadeIgnoresFadesUnderHalfAFrameAndExactTolerance)
{
    const auto times = pal();
    std::vector<std::optional<double>> distances(51, 1.0);
    distances[50] = 5.0;
    EXPECT_EQ(findFade(times, distances, 25, 50, 1010, 3000, {.in = true, .out = false})->inMs, 0);
    EXPECT_EQ(findFade(times, distances, 25, 50, 1010, 3000, {.in = false, .out = true}).error(),
              FadeProblem::OutNotFound);
    distances.assign(51, std::nullopt);
    EXPECT_EQ(findFade(times, distances, 25, 50, 1000, 3000, {}).error(), FadeProblem::InNotFound);
}

TEST(VideoAnalysis, PointClipReadsOnlySinglePointVectorClips)
{
    EXPECT_EQ(pointClip(u8"{\\clip(m 385 93)}x"), (FramePoint{385, 93}));
    EXPECT_EQ(pointClip(u8"{\\pos(1,1)\\iclip(m 10.7 20)}x"), (FramePoint{10, 20}));
    EXPECT_FALSE(pointClip(u8"{\\clip(m 0 0 l 10 0 10 10)}x"));
    EXPECT_FALSE(pointClip(u8"{\\clip(0,0,10,10)}x"));
    EXPECT_FALSE(pointClip(u8"\\clip(m 1 2)"));
}

TEST(VideoAnalysis, FadeReplacesOrOpensTheFirstBlockAndKeepsTheOtherEnd)
{
    EXPECT_EQ(withFade(u8"{\\pos(1,2)}x", 220, 300, false), u8"{\\fad(220,300)\\pos(1,2)}x");
    EXPECT_EQ(withFade(u8"x", 220, 300, false), u8"{\\fad(220,300)}x");
    EXPECT_EQ(withFade(u8"{\\fad(100,400)\\bord2}x", 220, std::nullopt, false), u8"{\\fad(220,400)\\bord2}x");
    EXPECT_EQ(withFade(u8"{\\bord2}x", 0, 0, false), u8"{\\bord2}x");
    EXPECT_EQ(withFade(u8"{\\clip(m 385 93)}{\\bord2\\clip(0,0,5,5)}x", 0, 120, true),
              u8"{\\fad(0,120)\\bord2\\clip(0,0,5,5)}x");
}

TEST(VideoAnalysis, ReplaceTextsIsOneStepAndSkipsUnchangedLines)
{
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,b\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,c\n")};
    const auto lines = session.document().lines();
    const core::LineId a = lines[0]->id, b = lines[1]->id, c = lines[2]->id;
    session.setSelection(Selection{a, {a, b, c}, a, {}});
    const auto steps = session.historySize();
    ASSERT_TRUE(replaceTexts(session, "Track colour", {{a, s("{T}a")}, {b, s("b")}, {c, s("{T}c")}}));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Track colour");
    EXPECT_EQ(session.document().lines()[0]->text, u8"{T}a");
    EXPECT_EQ(session.document().lines()[2]->text, u8"{T}c");
    EXPECT_EQ(session.selection().selected.size(), 3u);
    ASSERT_TRUE(replaceTexts(session, "Track colour", {{a, s("{T}a")}}));
    EXPECT_EQ(session.historySize(), steps + 1);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.document().lines()[0]->text, u8"a");
}
