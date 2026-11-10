#include "hikari/application/gradient.h"
#include "hikari/application/gradient_geometry.h"
#include "hikari/application/tag_lerp.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace hikari::application;
using namespace hikari::application::gradient;

namespace {

const lerp::TagSpec &spec(std::string_view name)
{
    return *lerp::tagSpec(name);
}

lerp::Value colour(int b, int g, int r)
{
    return {{static_cast<double>(b), static_cast<double>(g), static_cast<double>(r), 0}};
}

double area(const Shape &s)
{
    return std::abs(signedArea(s));
}

double segmentDistance(Point p, Point a, Point b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = dx * dx + dy * dy;
    const double t = len > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.0, 1.0) : 0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

double boundaryDistance(const Shape &shape, Point p)
{
    double best = 1e18;
    for (const auto &c : shape)
        for (std::size_t i = 0; i < c.size(); ++i)
            best = std::min(best, segmentDistance(p, c[i], c[(i + 1) % c.size()]));
    return best;
}

void expectBandsMatchDistance(const Shape &drawn, double step, double spacing = 1.5)
{
    Shape shape = drawn;
    if (signedArea(shape) < 0)
        for (auto &c : shape)
            std::reverse(c.begin(), c.end());
    shape = positiveRegion(shape);
    const auto bands = insetBands(drawn, step);
    ASSERT_FALSE(bands.empty());
    double x1 = 1e18, y1 = 1e18, x2 = -1e18, y2 = -1e18;
    for (const auto &c : shape)
        for (const auto &p : c) {
            x1 = std::min(x1, p.x);
            y1 = std::min(y1, p.y);
            x2 = std::max(x2, p.x);
            y2 = std::max(y2, p.y);
        }
    int checked = 0;
    for (double y = y1 - 3; y <= y2 + 3; y += spacing)
        for (double x = x1 - 3; x <= x2 + 3; x += spacing) {
            const Point p{x + 0.123, y + 0.317};
            const bool inside = winding(shape, p) != 0;
            const double d = boundaryDistance(shape, p);
            const double inBand = inside ? d / step : -1;
            if (inside && std::abs(inBand - std::round(inBand)) * step < 0.6)
                continue;
            int hits = 0, which = -1;
            for (std::size_t b = 0; b < bands.size(); ++b)
                if (winding(bands[b].shape, p) > 0) {
                    ++hits;
                    which = static_cast<int>(b);
                }
            if (!inside) {
                EXPECT_EQ(hits, 0) << "outside sample " << p.x << "," << p.y;
                continue;
            }
            ++checked;
            ASSERT_EQ(hits, 1) << "sample " << p.x << "," << p.y << " d=" << d;
            EXPECT_EQ(which, static_cast<int>(std::floor(inBand))) << "sample " << p.x << "," << p.y << " d=" << d;
        }
    EXPECT_GT(checked, 20);
}

Shape lShape()
{
    return {{{0, 0}, {60, 0}, {60, 20}, {20, 20}, {20, 80}, {0, 80}}};
}

Shape cShape()
{
    return {{{0, 0}, {70, 0}, {70, 18}, {18, 18}, {18, 62}, {70, 62}, {70, 80}, {0, 80}}};
}

Shape spiral()
{
    return {{{0, 0}, {100, 0}, {100, 100}, {24, 100}, {24, 36}, {64, 36}, {64, 64}, {52, 64}, {52, 48}, {36, 48},
             {36, 88}, {88, 88}, {88, 12}, {12, 12}, {12, 100}, {0, 100}}};
}

}

TEST(TagLerp, ParsesAndFormatsEachKind)
{
    const auto c = lerp::parse(spec("1c"), "&H00FF7F&");
    ASSERT_TRUE(c);
    EXPECT_EQ(lerp::format(spec("c"), *c), "\\1c&H00FF7F&");
    EXPECT_EQ(lerp::format(spec("1a"), *lerp::parse(spec("1a"), "&H80&")), "\\1a&H80&");
    EXPECT_EQ(lerp::format(spec("fscx"), *lerp::parse(spec("fscx"), "102.500")), "\\fscx102.5");
    EXPECT_EQ(lerp::format(spec("pos"), *lerp::parse(spec("pos"), "(10.004,20)")), "\\pos(10,20)");
    EXPECT_EQ(lerp::format(spec("clip"), *lerp::parse(spec("clip"), "(1.4,2.6,3,4)")), "\\clip(1,3,3,4)");
    EXPECT_FALSE(lerp::parse(spec("pos"), "(10)"));
    EXPECT_FALSE(lerp::parse(spec("bord"), "x"));
    EXPECT_EQ(lerp::tagSpec("fr")->key, "frz");
    EXPECT_EQ(lerp::tagSpec("q"), nullptr);
}

TEST(TagLerp, RoundsHalfUpAndBlendsInHsl)
{
    const auto mid = lerp::lerp(spec("1c"), colour(0, 0, 0), colour(255, 255, 255), 0.5);
    EXPECT_EQ(lerp::format(spec("1c"), mid), "\\1c&H808080&");
    EXPECT_EQ(lerp::format(spec("1a"), lerp::lerp(spec("1a"), {{0}}, {{255}}, 0.5)), "\\1a&H80&");
    const auto hsl = lerp::lerp(spec("1c"), colour(0, 0, 255), colour(255, 0, 0), 0.5, lerp::Blend::Hsl);
    EXPECT_EQ(lerp::format(spec("1c"), hsl), "\\1c&HFF00FF&");
}

TEST(TagLerp, StyleDefaultsAndStops)
{
    hikari::core::StyleValues style;
    style.primary = {255, 0, 0, 0};
    style.scaleX = u8"90";
    style.outlineWidth = u8"2.5";
    EXPECT_EQ(lerp::format(spec("1c"), *lerp::styleDefault(spec("1c"), style)), "\\1c&H0000FF&");
    EXPECT_EQ(lerp::styleDefault(spec("fscx"), style)->v[0], 90);
    EXPECT_EQ(lerp::styleDefault(spec("xbord"), style)->v[0], 2.5);
    EXPECT_EQ(lerp::styleDefault(spec("blur"), style)->v[0], 0);
    EXPECT_FALSE(lerp::styleDefault(spec("pos"), style));
    const auto at = lerp::stopAt(0.75, 3);
    EXPECT_EQ(at.index, 1u);
    EXPECT_DOUBLE_EQ(at.t, 0.5);
    EXPECT_EQ(lerp::stopAt(1, 3).index, 1u);
    EXPECT_DOUBLE_EQ(lerp::stopAt(1, 3).t, 1);
    EXPECT_DOUBLE_EQ(lerp::accelerate(0.5, 2), 0.25);
}

TEST(GradientGeometry, ClipConvexCutsAConcaveSubject)
{
    const Shape cut = clipConvex(lShape(), rectContour({10, 10, 50, 50}));
    EXPECT_NEAR(area(cut), 40 * 10 + 10 * 30, 1e-6);
    EXPECT_TRUE(clipConvex(lShape(), rectContour({200, 200, 210, 210})).empty());
}

TEST(GradientGeometry, LinearSlabsTileTheRegion)
{
    const Shape region{rectContour({0, 0, 100, 40})};
    const auto frame = linearFrame({10, 20}, {90, 20});
    ASSERT_TRUE(frame);
    double total = 0;
    total += area(slab(region, *frame, -INFINITY, 20));
    total += area(slab(region, *frame, 20, 40));
    total += area(slab(region, *frame, 40, INFINITY));
    EXPECT_NEAR(total, 4000, 1e-6);
    const auto r = asRect(slab(region, *frame, 20, 40));
    ASSERT_TRUE(r);
    EXPECT_DOUBLE_EQ(r->x1, 30);
    EXPECT_DOUBLE_EQ(r->x2, 50);
    const auto skew = linearFrame({10, 20}, {90, 20}, 30);
    ASSERT_TRUE(skew);
    const Shape piece = slab(region, *skew, 20, 40);
    EXPECT_FALSE(asRect(piece));
    EXPECT_NEAR(area(piece), 20 * 40, 1e-6);
    EXPECT_FALSE(linearFrame({1, 1}, {1, 1}));
    EXPECT_FALSE(linearFrame({0, 0}, {1, 0}, 90));
}

TEST(GradientGeometry, PositiveRegionRemovesLoopsAndOverlaps)
{
    const Shape bowTie{{{0, 0}, {10, 10}, {10, 0}, {0, 10}}};
    const Shape kept = positiveRegion(bowTie);
    EXPECT_NEAR(area(kept), 25, 1e-6);
    const Shape both{rectContour({0, 0, 10, 10}), rectContour({5, 5, 15, 15})};
    EXPECT_NEAR(area(positiveRegion(both)), 175, 1e-6);
}

TEST(GradientGeometry, InsetOfConvexAndHoledShapes)
{
    EXPECT_NEAR(area(inset({rectContour({0, 0, 40, 20})}, 5)), 30 * 10, 1e-6);
    EXPECT_TRUE(inset({rectContour({0, 0, 40, 20})}, 10).empty());
    Contour hole = rectContour({15, 15, 25, 25});
    std::reverse(hole.begin(), hole.end());
    const Shape frame{rectContour({0, 0, 40, 40}), hole};
    const Shape in = inset(frame, 2);
    EXPECT_NEAR(area(in), 36 * 36 - (14 * 14 - (4 - std::numbers::pi) * 4), 0.5);
}

TEST(GradientGeometry, BandsFollowTheDistanceOnAnL)
{
    for (const double step : {3.0, 7.0, 11.0})
        expectBandsMatchDistance(lShape(), step);
}

TEST(GradientGeometry, BandsFollowTheDistanceOnAC)
{
    for (const double step : {4.0, 9.0})
        expectBandsMatchDistance(cShape(), step);
}

TEST(GradientGeometry, BandsFollowTheDistanceOnASpiral)
{
    for (const double step : {2.0, 5.0})
        expectBandsMatchDistance(spiral(), step);
}

TEST(GradientGeometry, BandsFollowTheDistanceOnSelfTouchingAndReversedClips)
{
    Shape touching{{{0, 0}, {30, 0}, {30, 30}, {60, 30}, {60, 60}, {30, 60}, {30, 30}, {0, 30}}};
    std::reverse(touching.front().begin(), touching.front().end());
    expectBandsMatchDistance(touching, 4);
    const Shape keyhole{{{0, 0}, {50, 0}, {50, 50}, {26, 50}, {26, 20}, {26, 50}, {0, 50}}};
    expectBandsMatchDistance(keyhole, 5);
}

TEST(GradientGeometry, ParsesAndFormatsDrawings)
{
    const Shape s = parseDrawing("m 0 0 l 20 0 20 10 0 10 m 30 30 l 40 30 40 40");
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0].size(), 4u);
    EXPECT_EQ(formatShape(s), "m 0 0 l 20 0 20 10 0 10 m 30 30 l 40 30 40 40");
    EXPECT_EQ(parseDrawing("m 0 0 l 40 0 40 20", 2).front()[1], (Point{20, 0}));
    const Shape curve = parseDrawing("m 0 0 b 10 0 20 10 20 20 l 0 20");
    EXPECT_GT(curve.front().size(), 10u);
}

TEST(GradientGeometry, PhosWedgeComb)
{
    EXPECT_EQ(wedgeMask({{0, 100}, {40, 100}, {20, 80}}, 0.1, 10),
              "m 0 100 l 0 98 5 80 10 98 15 80 20 98 25 80 30 98 35 80 40 98 40 100");
    EXPECT_EQ(wedgeMask({{0, 0}, {0, 0}, {5, 5}}, 0.1, 10), "");
    EXPECT_EQ(wedgeMask({{0, 100}, {10, 100}, {5, 80}}, 4, 10), "m 0 100 l 0 80 5 80 10 80 10 100");
}

TEST(GradientGeometry, PhosBoxRingStarVignette)
{
    EXPECT_EQ(boxMask({{0, 0}, {10, 0}, {0, 20}}, 10),
              "m 0 0 l 10 0 10 6.667 0 6.667 m 0 10 l 10 10 10 13.333 0 13.333");
    const std::string ring = ringMask({0, 0}, {40, 0}, 10);
    EXPECT_EQ(ring.rfind("m 26.667 0 b", 0), 0u);
    EXPECT_NE(ring.find("m 33.333 0 b"), std::string::npos);
    EXPECT_NE(ring.find("m 30 0 b"), std::string::npos);
    EXPECT_EQ(ringMask({0, 0}, {40, 0}, 20).rfind("m 30 0 b", 0), 0u);
    const std::string star = starMask({0, 0}, {20, 0}, 4, 0.5);
    EXPECT_EQ(star.rfind("m 15 0 l ", 0), 0u);
    EXPECT_EQ(std::count(star.begin(), star.end(), ' ') + 1, 1 + 2 + 1 + 7 * 2);
    EXPECT_FALSE(vignetteMask({0, 0}, {40, 20}, 10, 0.3).empty());
    EXPECT_EQ(vignetteMask({0, 0}, {0, 0}, 10, 0), "");
}

namespace {

Spec rectSpec()
{
    Spec s;
    s.mode = Mode::Rect;
    s.tags = {{"1c", colour(0, 0, 255), colour(255, 0, 0)}};
    s.start = {0, 50};
    s.end = {100, 50};
    s.strip = 25;
    return s;
}

}

TEST(GradientPlan, RectStripsCoverTheBoxWithEndsOpen)
{
    LineIn line{"{\\pos(50,50)\\1c&H00FF00&}Text", {}, Rect{10, 30, 90, 70}};
    const auto out = stripTexts(rectSpec(), line);
    ASSERT_TRUE(out) << static_cast<int>(out.error());
    ASSERT_EQ(out->size(), 4u);
    EXPECT_EQ((*out)[0], "{\\pos(50,50)\\1c&H0000FF&\\clip(10,30,25,70)}Text");
    EXPECT_EQ((*out)[1], "{\\pos(50,50)\\1c&H5500AA&\\clip(25,30,50,70)}Text");
    EXPECT_EQ((*out)[2], "{\\pos(50,50)\\1c&HAA0055&\\clip(50,30,75,70)}Text");
    EXPECT_EQ((*out)[3], "{\\pos(50,50)\\1c&HFF0000&\\clip(75,30,90,70)}Text");
}

TEST(GradientPlan, RectSnapsToTheLongerAxisAndKeepsARectClip)
{
    Spec s = rectSpec();
    s.start = {50, 0};
    s.end = {52, 40};
    s.strip = 20;
    LineIn line{"{\\clip(0,10,100,30)}A{\\1c&H00FF00&}B", {}, std::nullopt};
    const auto out = stripTexts(s, line);
    ASSERT_TRUE(out);
    ASSERT_EQ(out->size(), 2u);
    EXPECT_EQ((*out)[0], "{\\1c&H0000FF&\\clip(0,10,100,20)}AB");
    EXPECT_EQ((*out)[1], "{\\1c&HFF0000&\\clip(0,20,100,30)}AB");
}

TEST(GradientPlan, MergeJoinsEqualStrips)
{
    Spec s = rectSpec();
    s.tags = {{"bord", {{2}}, {{2.004}}}};
    LineIn line{"Text", {}, Rect{0, 0, 100, 10}};
    const auto merged = stripTexts(s, line);
    ASSERT_TRUE(merged);
    ASSERT_EQ(merged->size(), 1u);
    EXPECT_EQ(merged->front(), "{\\bord2\\clip(0,0,100,10)}Text");
    s.merge = false;
    EXPECT_EQ(stripTexts(s, line)->size(), 4u);
}

TEST(GradientPlan, RotatedStripsAreVectorClipsCutToTheLinesClip)
{
    Spec s = rectSpec();
    s.mode = Mode::Rotated;
    s.start = {0, 0};
    s.end = {40, 40};
    s.strip = 20;
    s.overlap = 1;
    LineIn line{"{\\clip(m 0 0 l 40 0 40 40 0 40)}T", {}, std::nullopt};
    const auto shapes = stripShapes(s, line);
    ASSERT_TRUE(shapes);
    ASSERT_EQ(shapes->size(), 3u);
    double total = 0;
    for (const auto &shape : *shapes)
        total += area(shape);
    EXPECT_GT(total, 1600);
    EXPECT_LT(total, 1600 + 2 * 60);
    const auto out = stripTexts(s, line);
    ASSERT_TRUE(out);
    EXPECT_NE(out->front().find("\\clip(m "), std::string::npos);
    EXPECT_EQ(out->front().find("\\clip(m 0 0 l 40 0"), std::string::npos);
}

TEST(GradientPlan, StripsRefuseWhatTheyCannotDo)
{
    Spec s = rectSpec();
    EXPECT_EQ(stripTexts(s, {"Text", {}, std::nullopt}).error(), Problem::NoArea);
    EXPECT_EQ(stripTexts(s, {"{\\iclip(0,0,5,5)}T", {}, Rect{0, 0, 9, 9}}).error(), Problem::InverseClip);
    s.end = s.start;
    EXPECT_EQ(stripTexts(s, {"T", {}, Rect{0, 0, 9, 9}}).error(), Problem::NoLength);
    s = rectSpec();
    s.tags.clear();
    EXPECT_EQ(stripTexts(s, {"T", {}, Rect{0, 0, 9, 9}}).error(), Problem::NoTags);
    s = rectSpec();
    s.mode = Mode::ClipEdge;
    EXPECT_EQ(stripTexts(s, {"T", {}, Rect{0, 0, 9, 9}}).error(), Problem::NeedsClip);
}

TEST(GradientPlan, StripsAlongARectClip)
{
    Spec s = rectSpec();
    s.mode = Mode::ClipEdge;
    s.strip = 5;
    LineIn line{"{\\clip(0,0,40,20)}T", {}, std::nullopt};
    const auto out = stripTexts(s, line);
    ASSERT_TRUE(out);
    ASSERT_EQ(out->size(), 2u);
    EXPECT_EQ((*out)[0], "{\\1c&H0000FF&\\clip(m 0 0 l 40 0 40 20 0 20 m 5 15 l 35 15 35 5 5 5)}T");
    EXPECT_EQ((*out)[1], "{\\1c&HFF0000&\\clip(m 5 5 l 35 5 35 15 5 15)}T");
}

TEST(GradientPlan, PickedLinesGradeEachBlockBetweenStops)
{
    Spec s = rectSpec();
    s.pickedLines = true;
    s.strip = 50;
    s.end = {100, 50};
    hikari::core::StyleValues style;
    style.primary = {255, 255, 255, 0};
    const std::vector<LineIn> stops{
        {"{\\pos(10,10)}A{\\3c&H000000&}B", style, Rect{0, 0, 100, 20}},
        {"{\\pos(30,10)\\1c&H000000&}A{\\3c&HFFFFFF&}B", style, Rect{0, 0, 100, 20}},
    };
    const auto out = stripTextsFromStops(s, stops);
    ASSERT_TRUE(out) << static_cast<int>(out.error());
    ASSERT_EQ(out->size(), 2u);
    EXPECT_EQ((*out)[0], "{\\pos(10,10)\\1c&HFFFFFF&\\clip(0,0,50,20)}A{\\3c&H000000&}B");
    EXPECT_EQ((*out)[1], "{\\pos(30,10)\\1c&H000000&\\clip(50,0,100,20)}A{\\3c&HFFFFFF&}B");
    EXPECT_EQ(stripTextsFromStops(s, {stops[0]}).error(), Problem::TooFewStops);
    EXPECT_EQ(stripTextsFromStops(s, {stops[0], {"X", style, Rect{0, 0, 1, 1}}}).error(), Problem::StopsDiffer);
    EXPECT_EQ(stripTextsFromStops(s, {stops[0], stops[0]}).error(), Problem::NothingDiffers);
}

TEST(GradientPlan, ByCharacterByCountAndByWidth)
{
    Spec s;
    s.mode = Mode::ByChar;
    s.tags = {{"1c", colour(0, 0, 0), colour(0, 0, 255)}};
    s.measured = false;
    LineIn line{"{\\b1}Ab c\\Nd", {}, std::nullopt};
    const auto byCount = byCharText(s, line, {});
    ASSERT_TRUE(byCount);
    EXPECT_EQ(*byCount, "{\\b1\\1c&H000000&}A{\\1c&H000040&}b {\\1c&H0000BF&}c\\N{\\1c&H0000FF&}d");
    s.measured = true;
    const Measure width = [](const std::string &text) {
        double w = 0;
        for (const char c : text)
            w += c == 'c' ? 50 : 10;
        return std::optional<double>(w);
    };
    const auto byWidth = byCharText(s, line, width);
    ASSERT_TRUE(byWidth);
    EXPECT_EQ(*byWidth, "{\\b1\\1c&H000000&}A{\\1c&H000020&}b {\\1c&H00009F&}c\\N{\\1c&H0000FF&}d");
    EXPECT_EQ(byCharText(s, {"{\\b1}", {}, std::nullopt}, {}).error(), Problem::NoText);
}

TEST(GradientPlan, ByCharacterTakesInlineValuesAsStops)
{
    Spec s;
    s.mode = Mode::ByChar;
    s.tags = {{"1c", colour(0, 0, 0), colour(0, 0, 0)}};
    s.measured = false;
    const auto out = byCharText(s, {"abc{\\c&H0000FF&}def", {}, std::nullopt}, {});
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, "{\\1c&H000000&}a{\\1c&H000055&}b{\\1c&H0000AA&}c{\\1c&H0000FF&}d{\\1c&H000080&}e{\\1c&H000000&}"
                    "f");
}

TEST(GradientPlan, ByCharacterKeepsThaiClustersWhole)
{
    Spec s;
    s.mode = Mode::ByChar;
    s.tags = {{"bord", {{0}}, {{2}}}};
    s.measured = false;
    const auto out = byCharText(s, {"\xE0\xB8\x81\xE0\xB8\xB5\xE0\xB8\x82", {}, std::nullopt}, {});
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, "{\\bord0}\xE0\xB8\x81\xE0\xB8\xB5{\\bord2}\xE0\xB8\x82");
}

TEST(GradientPlan, ByLineSetsEachLinesValue)
{
    Spec s;
    s.mode = Mode::ByLine;
    s.tags = {{"fscx", {{100}}, {{200}}}};
    EXPECT_EQ(*byLineText(s, {"{\\fscx80}a{\\fscx90}b", {}, std::nullopt}, 1, 3), "{\\fscx150}ab");
    EXPECT_EQ(*byLineText(s, {"x", {}, std::nullopt}, 0, 1), "{\\fscx100}x");
}

TEST(GradientPlan, MaskReplacesTheTextWithTheDrawing)
{
    Spec s;
    s.mode = Mode::Wedge;
    s.mask = {{0, 100}, {40, 100}, {20, 80}};
    s.spacing = 10;
    const auto out = maskText(s, {"{\\frz5\\pos(1,2)\\clip(m 0 0 l 1 0 1 1)\\1c&H0000FF&\\bord3}Sign{\\i1}", {}, std::nullopt});
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, "{\\frz5\\1c&H0000FF&\\an7\\pos(0,0)\\fscx100\\fscy100\\bord0\\shad0\\p1}"
                    "m 0 100 l 0 98 5 80 10 98 15 80 20 98 25 80 30 98 35 80 40 98 40 100");
    s.mask = {};
    EXPECT_EQ(maskText(s, {"x", {}, std::nullopt}).error(), Problem::NoLength);
}

TEST(GradientPlan, ReplaceTagLeavesTransformsAlone)
{
    EXPECT_EQ(replaceTag("{\\1c&H0&\\t(\\1c&HFF&)}a{\\c&H1&}b", "1c", "\\1c&H00FF00&"),
              "{\\t(\\1c&HFF&)\\1c&H00FF00&}ab");
    hikari::core::StyleValues style;
    style.outline = {0, 0, 255, 0};
    EXPECT_EQ(lerp::format(spec("3c"), *startValue("{\\bord2}a", "3c", style)), "\\3c&HFF0000&");
    EXPECT_EQ(startValue("{\\3c&H00FF00&}a", "3c", style)->v[1], 255);
}
