#include "hikari/application/layer_builders.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::StyleValues style(std::u8string name = u8"Default", std::u8string border = u8"2", std::u8string shadow = u8"0")
{
    core::StyleValues s;
    s.name = std::move(name);
    s.fontsize = u8"20";
    s.scaleX = s.scaleY = u8"100";
    s.spacing = s.angle = u8"0";
    s.outlineWidth = std::move(border);
    s.shadow = std::move(shadow);
    s.primary = {255, 255, 255, 0};
    s.outline = {0, 0, 255, 0};
    s.back = {0, 0, 0, 0x40};
    return s;
}

LayerOptions only(bool glow, bool border, bool shadow)
{
    LayerOptions o;
    o.glow = glow;
    o.border = border;
    o.shadow = shadow;
    return o;
}

BuiltLayers build(std::u8string_view text, const LayerOptions &o, const core::StyleValues &s = style())
{
    return buildLayers(text, s, {s, style(u8"Alt", u8"5")}, 2000, o);
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kScript = "[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                                     "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                                     "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                                     "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                                     "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00FF0000,&H40000000,0,0,0,0,100,"
                                     "100,0,0,1,2,3,2,10,10,10,1\n\n[Events]\n"
                                     "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                     "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\pos(10,10)}One\n"
                                     "Dialogue: 1,0:00:02.00,0:00:04.00,Default,,0,0,0,,Over\n"
                                     "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Two\n";

std::vector<std::pair<std::int64_t, std::u8string>> rows(const core::Document &doc)
{
    std::vector<std::pair<std::int64_t, std::u8string>> out;
    for (const auto *line : doc.lines())
        out.emplace_back(line->layer.value, line->text);
    return out;
}

std::set<core::LineId> ids(const core::Document &doc, std::initializer_list<int> rowsWanted)
{
    std::set<core::LineId> out;
    const auto lines = doc.lines();
    for (const int r : rowsWanted)
        out.insert(lines[static_cast<std::size_t>(r - 1)]->id);
    return out;
}

}

TEST(LayerBuilders, BorderSplitsTheBorderOntoItsOwnLayer)
{
    const auto built = build(u8"{\\pos(10,10)}Sign", only(false, true, false));
    ASSERT_EQ(built.below.size(), 1u);
    EXPECT_EQ(built.below[0], u8"{\\c&HFF0000&\\1a&H00&\\blur0.6\\pos(10,10)}Sign");
    EXPECT_EQ(built.top, u8"{\\shad0\\bord0\\blur0.6\\pos(10,10)}Sign");
}

TEST(LayerBuilders, BorderLayerTakesTheBorderBlur)
{
    auto o = only(false, true, false);
    o.borderBlur = 1.2;
    const auto built = build(u8"{\\blur3}Sign", o);
    EXPECT_EQ(built.below[0], u8"{\\c&HFF0000&\\1a&H00&\\blur1.2}Sign");
    EXPECT_EQ(built.top, u8"{\\shad0\\bord0\\blur3}Sign");
}

TEST(LayerBuilders, TransparentBorderLeavesAThinFillColouredBorderOnTop)
{
    auto o = only(false, true, false);
    o.under = UnderBorder::Transparent;
    const auto built = build(u8"{\\bord3\\alpha&H20&}Sign", o);
    EXPECT_EQ(built.below[0], u8"{\\1a&HFF&\\blur0.6\\bord3.6\\3a&H20&}Sign");
    EXPECT_EQ(built.top, u8"{\\shad0\\3c&HFFFFFF&\\3a&H20&\\blur0.6\\bord0.6\\alpha&H20&}Sign");
}

TEST(LayerBuilders, InlineBorderColourIsFollowedByTheFillOnTheBorderLayer)
{
    const auto built = build(u8"{\\3c&H00FF00&}A{\\3c&H0000FF&\\bord4}B", only(false, true, false));
    EXPECT_EQ(built.below[0], u8"{\\1a&H00&\\blur0.6\\3c&H00FF00&\\c&H00FF00&}A{\\3c&H0000FF&\\c&H0000FF&\\bord4}B");
    EXPECT_EQ(built.top, u8"{\\shad0\\bord0\\blur0.6}A{\\bord0}B");
}

TEST(LayerBuilders, ResetTakesItsStylesBorderColour)
{
    auto alt = style(u8"Alt", u8"5");
    alt.outline = {0, 255, 0, 0};
    const auto built = buildLayers(u8"A{\\rAlt}B", style(), {style(), alt}, 2000, only(false, true, false));
    EXPECT_EQ(built.below[0], u8"{\\c&HFF0000&\\1a&H00&\\blur0.6}A{\\rAlt\\c&H00FF00&\\1a&H00&\\blur0.6}B");
    EXPECT_EQ(built.top, u8"{\\shad0\\bord0\\blur0.6}A{\\rAlt\\shad0\\bord0\\blur0.6}B");
}

TEST(LayerBuilders, TransformsKeepWhatTheLayerDoesNotChange)
{
    const auto built = build(u8"{\\t(0,500,\\3c&H00FF00&\\bord5\\fscx120)}Sign", only(false, true, false));
    EXPECT_EQ(built.below[0], u8"{\\c&HFF0000&\\1a&H00&\\blur0.6\\t(0,500,\\3c&H00FF00&\\c&H00FF00&\\bord5\\fscx120)}Sign");
    EXPECT_EQ(built.top, u8"{\\shad0\\bord0\\blur0.6\\t(0,500,\\fscx120)}Sign");
}

TEST(LayerBuilders, EmptiedTransformIsRemoved)
{
    const auto built = build(u8"{\\t(\\bord5)}Sign", only(false, true, false));
    EXPECT_EQ(built.top, u8"{\\shad0\\bord0\\blur0.6}Sign");
}

TEST(LayerBuilders, ClipMoveAndDrawingsStayOnEveryLayer)
{
    LayerOptions o = only(true, true, true);
    o.steps = 2;
    const std::u8string_view text = u8"{\\clip(0,0,10,10)\\move(1,2,3,4)\\shad2\\p1}m 0 0 l 10 0 10 10";
    const auto built = build(text, o);
    ASSERT_EQ(built.below.size(), 4u);
    for (const auto &layer : built.below) {
        EXPECT_NE(layer.find(u8"\\clip(0,0,10,10)\\move(1,2,3,4)"), std::u8string::npos);
        EXPECT_NE(layer.find(u8"\\p1}m 0 0 l 10 0 10 10"), std::u8string::npos);
    }
    EXPECT_NE(built.top.find(u8"\\clip(0,0,10,10)\\move(1,2,3,4)"), std::u8string::npos);
}

TEST(LayerBuilders, GlowIsABlurredCopyUnderTheLineAndKeepsTheShadow)
{
    const auto built = build(u8"{\\shad2}Sign", only(true, false, false));
    ASSERT_EQ(built.below.size(), 1u);
    EXPECT_EQ(built.below[0], u8"{\\alpha&H80&\\blur3\\shad2}Sign");
    EXPECT_EQ(built.top, u8"{\\shad2}Sign");
}

TEST(LayerBuilders, GlowRaisesBorderAlphaAndTakesItsColourAndWidth)
{
    auto o = only(true, false, false);
    o.glows = {GlowLayer{5, 2, core::Colour{0, 128, 255, 0}, 0x60}};
    const auto built = build(u8"{\\bord3\\3a&HA0&\\blur1}Sign", o);
    EXPECT_EQ(built.below[0], u8"{\\3c&HFF8000&\\alpha&H60&\\bord5\\3a&HA0&\\blur5}Sign");
}

TEST(LayerBuilders, GlowOnALineWithoutBorderUsesTheFill)
{
    auto o = only(true, true, false);
    o.glows = {GlowLayer{4, 0, core::Colour{0, 0, 0, 0}, 0x80}};
    const auto built = build(u8"{\\bord0\\1a&H10&}Sign", o);
    ASSERT_EQ(built.below.size(), 1u);
    EXPECT_TRUE(built.skippedBorder);
    EXPECT_EQ(built.below[0], u8"{\\c&H000000&\\alpha&H80&\\blur4\\bord0\\1a&H80&}Sign");
    EXPECT_EQ(built.top, u8"{\\bord0\\1a&H10&}Sign");
}

TEST(LayerBuilders, SeveralGlowsStackInListOrder)
{
    auto o = only(true, false, false);
    o.glows = {GlowLayer{8, 3, std::nullopt, 0xC0}, GlowLayer{2, 0, std::nullopt, 0x40}};
    const auto built = build(u8"Sign", o);
    ASSERT_EQ(built.below.size(), 2u);
    EXPECT_EQ(built.below[0], u8"{\\alpha&HC0&\\bord5\\blur8}Sign");
    EXPECT_EQ(built.below[1], u8"{\\alpha&H40&\\blur2}Sign");
}

TEST(LayerBuilders, ShadowIsStackedCopiesFarthestFirstWithARamp)
{
    auto o = only(false, false, true);
    o.nearColour = core::Colour{0, 0, 0, 0};
    o.farColour = core::Colour{200, 100, 0, 0};
    o.nearAlpha = 0x00;
    o.farAlpha = 0xA0;
    const auto built = build(u8"{\\xshad3\\yshad1.5}Sign", o);
    ASSERT_EQ(built.below.size(), 3u);
    EXPECT_EQ(built.below[0], u8"{\\xshad3\\yshad1.5\\1a&HFE&\\3a&HFF&\\4c&H0064C8&\\4a&HA0&\\blur0.6}Sign");
    EXPECT_EQ(built.below[1], u8"{\\xshad2\\yshad1\\1a&HFE&\\3a&HFF&\\4c&H003264&\\4a&H50&\\blur0.6}Sign");
    EXPECT_EQ(built.below[2], u8"{\\xshad1\\yshad0.5\\1a&HFE&\\3a&HFF&\\4c&H000000&\\4a&H00&\\blur0.6}Sign");
    EXPECT_EQ(built.top, u8"{\\shad0}Sign");
}

TEST(LayerBuilders, ShadowStepsDefaultToOneAPixelOfTheStylesOffset)
{
    const auto built = build(u8"Sign", only(false, false, true), style(u8"Default", u8"2", u8"4.7"));
    EXPECT_EQ(built.below.size(), 4u);
    EXPECT_NE(built.below[0].find(u8"\\xshad4.7\\yshad4.7"), std::u8string::npos);
    EXPECT_NE(built.below[3].find(u8"\\xshad1.17\\yshad1.17"), std::u8string::npos);
}

TEST(LayerBuilders, ShadowUnderAPixelIsSkipped)
{
    const auto built = build(u8"{\\shad0.5}Sign", only(false, false, true));
    EXPECT_TRUE(built.below.empty());
    EXPECT_TRUE(built.skippedShadow);
    EXPECT_EQ(built.top, u8"{\\shad0.5}Sign");
}

TEST(LayerBuilders, SecondBorderGoesUnderTheBorderAndKeepsTheShadow)
{
    auto o = only(false, true, false);
    o.secondBorder = true;
    o.secondWidth = 3;
    const auto built = build(u8"{\\shad2}Sign", o);
    ASSERT_EQ(built.below.size(), 2u);
    EXPECT_EQ(built.below[0], u8"{\\3c&HFFFFFF&\\c&HFFFFFF&\\bord5\\1a&H00&\\blur0.6\\shad2}Sign");
    EXPECT_EQ(built.below[1], u8"{\\shad0\\c&HFF0000&\\1a&H00&\\blur0.6}Sign");
}

TEST(LayerBuilders, AllThreeStackGlowShadowSecondBorderBorderThenTop)
{
    auto o = only(true, true, true);
    o.secondBorder = true;
    o.steps = 2;
    const auto built = build(u8"{\\shad2}Sign", o);
    ASSERT_EQ(built.below.size(), 5u);
    EXPECT_NE(built.below[0].find(u8"\\blur3"), std::u8string::npos);
    EXPECT_NE(built.below[0].find(u8"\\shad0"), std::u8string::npos);
    EXPECT_NE(built.below[1].find(u8"\\xshad2\\yshad2"), std::u8string::npos);
    EXPECT_NE(built.below[2].find(u8"\\xshad1\\yshad1"), std::u8string::npos);
    EXPECT_NE(built.below[3].find(u8"\\bord4"), std::u8string::npos);
    EXPECT_NE(built.below[4].find(u8"\\c&HFF0000&"), std::u8string::npos);
    for (const auto i : {0, 3, 4})
        EXPECT_NE(built.below[std::size_t(i)].find(u8"\\shad0"), std::u8string::npos) << i;
    EXPECT_NE(built.top.find(u8"\\bord0"), std::u8string::npos);
}

TEST(LayerBuilders, FadeHidesTheFillUnderTheBorder)
{
    const auto built = build(u8"{\\fad(300,200)}Sign", only(false, true, false));
    EXPECT_EQ(built.below[0],
              u8"{\\c&HFF0000&\\blur0.6\\fad(300,200)\\1a&HFF&\\t(255,300,\\1a&H00&)\\t(1800,1845,\\1a&HFF&)}Sign");
}

TEST(LayerBuilders, FadeFixCanBeTurnedOff)
{
    auto o = only(false, true, false);
    o.fadeFix = false;
    const auto built = build(u8"{\\fad(300,200)}Sign", o);
    EXPECT_EQ(built.below[0], u8"{\\c&HFF0000&\\1a&H00&\\blur0.6\\fad(300,200)}Sign");
}

TEST(LayerBuilders, LineWithoutBlurOnlyGetsTheDefaultOnBuiltLayers)
{
    auto o = only(true, false, false);
    o.defaultBlur = 0.8;
    const auto built = build(u8"Sign", o);
    EXPECT_EQ(built.top, u8"Sign");
}

TEST(LayerBuilders, BorderedXAndYBordAreDroppedFromTheTop)
{
    const auto built = build(u8"{\\xbord3\\ybord1}Sign", only(false, true, false));
    EXPECT_EQ(built.top, u8"{\\shad0\\bord0\\blur0.6}Sign");
    EXPECT_EQ(built.below[0], u8"{\\c&HFF0000&\\1a&H00&\\blur0.6\\xbord3\\ybord1}Sign");
}

TEST(LayerBuilders, PlanMovesSourcesUpAndLeavesOtherLinesAlone)
{
    auto doc = load(kScript);
    const auto plan = planLayers(doc, ids(doc, {1, 3}), only(true, true, false));
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->sources.size(), 2u);
    EXPECT_EQ(plan->built, 4);
    EXPECT_EQ(plan->sources[0].newLayer, 2);
    ASSERT_EQ(plan->covered.size(), 1u);
    EXPECT_EQ(plan->covered[0].row, 2);
    EXPECT_EQ(plan->covered[0].sourceRow, 1);
    ASSERT_TRUE(applyLayerPlan(doc, *plan));
    const auto after = rows(doc);
    ASSERT_EQ(after.size(), 7u);
    EXPECT_EQ(after[0].first, 0);
    EXPECT_EQ(after[1].first, 1);
    EXPECT_EQ(after[2].first, 2);
    EXPECT_EQ(after[2].second, u8"{\\shad0\\bord0\\blur0.6\\pos(10,10)}One");
    EXPECT_EQ(after[3], (std::pair<std::int64_t, std::u8string>{1, u8"Over"}));
    EXPECT_EQ(after[6].first, 2);
}

TEST(LayerBuilders, PlanRefusesWhenNothingIsOnOrOverTheLimits)
{
    const auto doc = load(kScript);
    EXPECT_EQ(planLayers(doc, ids(doc, {1}), only(false, false, false)).error(), LayerProblem::NothingOn);
    EXPECT_EQ(planLayers(doc, {}, only(true, false, false)).error(), LayerProblem::NoLines);
    auto glows = only(true, false, false);
    glows.glows.assign(kMaxGlows + 1, GlowLayer{});
    EXPECT_EQ(planLayers(doc, ids(doc, {1}), glows).error(), LayerProblem::TooManyGlows);
    auto steps = only(false, false, true);
    steps.steps = kMaxShadowSteps + 1;
    EXPECT_EQ(planLayers(doc, ids(doc, {1}), steps).error(), LayerProblem::TooManySteps);
}

TEST(LayerBuilders, PlanCountsSkippedLines)
{
    const auto doc = load(kScript);
    auto o = only(false, false, true);
    const auto plan = planLayers(doc, ids(doc, {1, 2}), o);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->skippedShadow, 0);
    EXPECT_EQ(plan->built, 6);
}

TEST(LayerBuilders, BuildIsOneHistoryStepAndKeepsTheSelectionOnTheSources)
{
    EditSession session{load(kScript)};
    const auto source = ids(session.document(), {1});
    session.setSelection(Selection{*source.begin(), source, *source.begin(), {}});
    ASSERT_TRUE(buildLayerStacks(session, only(true, true, false)));
    EXPECT_EQ(session.document().lines().size(), 5u);
    EXPECT_EQ(session.selection().selected, source);
    EXPECT_EQ(session.document().lines()[2]->id, *source.begin());
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.document().lines().size(), 3u);
}

TEST(LayerBuilders, BuiltLinesDoNotCarryTheSourcesExtradata)
{
    auto doc = load(kScript);
    const auto id = *ids(doc, {1}).begin();
    doc.editLine(id, [](core::LineRecord &l) { l.extra = {core::Extradata{u8"qc", u8"note"}}; });
    const auto plan = planLayers(doc, {id}, only(true, false, false));
    ASSERT_TRUE(plan && applyLayerPlan(doc, *plan));
    EXPECT_TRUE(doc.lines()[0]->extra.empty());
    EXPECT_EQ(doc.lines()[1]->extra.size(), 1u);
}
