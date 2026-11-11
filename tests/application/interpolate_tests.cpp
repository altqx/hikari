#include "hikari/application/interpolate.h"
#include "hikari/application/tag_lerp.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace hikari::application;
using namespace hikari::application::interpolate;

namespace {

std::vector<LineIn> run(std::vector<std::string> texts)
{
    std::vector<LineIn> lines;
    for (auto &t : texts)
        lines.push_back(LineIn{std::move(t), {}, 1000, 10, 20});
    return lines;
}

std::vector<std::string> texts(const std::vector<LineIn> &lines, const Options &options = {})
{
    const auto p = plan(lines, options);
    EXPECT_TRUE(p.has_value());
    return p ? p->texts : std::vector<std::string>{};
}

const Change *change(const Plan &p, std::string_view id)
{
    const auto it = std::ranges::find(p.changes, id, &Change::id);
    return it == p.changes.end() ? nullptr : &*it;
}

}

TEST(TagLerpDrawing, ParsesLerpsAndFormatsPointByPoint)
{
    const auto a = lerp::parseDrawing("(m 0 0 l 100 0 100 100)");
    const auto b = lerp::parseDrawing("(m 10 0 l 110 0 110 100)");
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->shape, "m##l####");
    const auto mid = lerp::lerp(*a, *b, 0.5);
    ASSERT_TRUE(mid);
    EXPECT_EQ(lerp::format("clip", *mid), "\\clip(m 5 0 l 105 0 105 100)");
}

TEST(TagLerpDrawing, KeepsTheScaleAndRoundsToWholePoints)
{
    const auto d = lerp::parseDrawing("(2,m 0 0 l 10.6 -3.2)");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->scale, 2);
    EXPECT_EQ(lerp::format("iclip", *d), "\\iclip(2,m 0 0 l 11 -3)");
}

TEST(TagLerpDrawing, RefusesDifferentShapesAndScales)
{
    const auto square = lerp::parseDrawing("(m 0 0 l 10 0 10 10)");
    const auto triangle = lerp::parseDrawing("(m 0 0 l 10 0)");
    const auto scaled = lerp::parseDrawing("(2,m 0 0 l 10 0 10 10)");
    EXPECT_FALSE(lerp::lerp(*square, *triangle, 0.5));
    EXPECT_FALSE(lerp::lerp(*square, *scaled, 0.5));
    EXPECT_FALSE(lerp::parseDrawing("(0,0,10,10)"));
}

TEST(Interpolate, NeedsThreeLines)
{
    EXPECT_EQ(plan(run({"a", "b"}), {}).error(), Problem::TooFewLines);
}

TEST(Interpolate, RefusesARunWhereNothingChanges)
{
    EXPECT_EQ(plan(run({"{\\bord2}a", "{\\bord5}a", "{\\bord2}a"}), {}).error(), Problem::NothingChanges);
}

TEST(Interpolate, WritesEachMiddleLineByItsPlaceInTheRun)
{
    const auto out =
        texts(run({"{\\fscx100}Hi", "{\\fscx100\\bord2}Hi", "Hi", "{\\fscx3}Hi", "{\\fscx200}Hi"}));
    EXPECT_EQ(out, (std::vector<std::string>{"{\\fscx100}Hi", "{\\bord2\\fscx125}Hi", "{\\fscx150}Hi",
                                             "{\\fscx175}Hi", "{\\fscx200}Hi"}));
}

TEST(Interpolate, LeavesTagsThatDoNotChangeAlone)
{
    EXPECT_EQ(texts(run({"{\\bord2\\fs10}a", "{\\bord5}a", "{\\bord2\\fs30}a"}))[1], "{\\bord5\\fs20}a");
}

TEST(Interpolate, LerpsColourAndAlphaPerChannelInTheirSpelling)
{
    const auto out = texts(run({"{\\c&H000000&\\alpha&H00&}a", "a", "{\\c&HFF40FF&\\alpha&HFF&}a"}));
    EXPECT_EQ(out[1], "{\\c&H802080&\\alpha&H80&}a");
}

TEST(Interpolate, AppliesAcceleration)
{
    Options o;
    o.accel = 2;
    EXPECT_EQ(texts(run({"{\\fscx100}a", "a", "{\\fscx200}a"}), o)[1], "{\\fscx125}a");
}

TEST(Interpolate, TakesTheStyleValueForATagOneEndLacks)
{
    EXPECT_EQ(texts(run({"a", "a", "{\\fscx200}a"}))[1], "{\\fscx150}a");
}

TEST(Interpolate, MatchesInlineBlocksByCharacters)
{
    const auto out = texts(run({"{\\fs10}あい{\\fs10}う", "あいう", "{\\fs10}あい{\\fs30}う"}));
    EXPECT_EQ(out[1], "あい{\\fs20}う");
}

TEST(Interpolate, UsesOnlyTheStartBlockWhenTheTextDiffers)
{
    const auto p = plan(run({"{\\fs10}A{\\fs10}B", "AB", "{\\fs20}A{\\fs30}C"}), {});
    ASSERT_TRUE(p);
    EXPECT_TRUE(p->startBlockOnly);
    EXPECT_TRUE(change(*p, "fs@0"));
    EXPECT_FALSE(change(*p, "fs@1"));
    EXPECT_EQ(p->texts[1], "{\\fs15}AB");
}

TEST(Interpolate, LerpsPosition)
{
    EXPECT_EQ(texts(run({"{\\pos(0,0)}x", "{\\pos(3,3)}x", "{\\pos(100,50)}x"}))[1], "{\\pos(50,25)}x");
}

TEST(Interpolate, TakesTheMoveStartThenTheDefaultForPosition)
{
    EXPECT_EQ(texts(run({"{\\move(0,0,10,10)}x", "{\\move(0,0,10,10)}x", "{\\pos(100,0)}x"}))[1], "{\\pos(50,0)}x");
    EXPECT_EQ(texts(run({"x", "x", "{\\pos(30,20)}x"}))[1], "{\\pos(20,20)}x");
}

TEST(Interpolate, LerpsRectAndVectorClips)
{
    EXPECT_EQ(texts(run({"{\\clip(0,0,100,100)}x", "x", "{\\clip(10,10,110,110)}x"}))[1], "{\\clip(5,5,105,105)}x");
    EXPECT_EQ(texts(run({"{\\iclip(m 0 0 l 100 0 100 100)}x", "x", "{\\iclip(m 10 0 l 110 0 110 100)}x"}))[1],
              "{\\iclip(m 5 0 l 105 0 105 100)}x");
}

TEST(Interpolate, SaysWhyAClipCannotBeInterpolated)
{
    const auto shapes = plan(run({"{\\clip(m 0 0 l 10 0 10 10)}x", "x", "{\\clip(m 0 0 l 10 0)}x"}), {});
    ASSERT_TRUE(shapes);
    EXPECT_EQ(change(*shapes, "clip")->skip, Skip::DifferentShapes);
    EXPECT_EQ(shapes->texts[1], "x");

    const auto mixed = plan(run({"{\\clip(0,0,10,10)\\fs1}x", "x", "{\\clip(m 0 0 l 10 0)\\fs3}x"}), {});
    EXPECT_EQ(change(*mixed, "clip")->skip, Skip::RectAndDrawing);
    const auto oneSided = plan(run({"{\\clip(0,0,10,10)\\fs1}x", "x", "{\\fs3}x"}), {});
    EXPECT_EQ(change(*oneSided, "clip")->skip, Skip::OneSidedClip);
}

TEST(Interpolate, RotatesTheShortWayWhenAsked)
{
    const auto lines = run({"{\\frz350}x", "x", "{\\frz10}x"});
    EXPECT_EQ(texts(lines)[1], "{\\frz180}x");
    Options o;
    o.shortestRotation = true;
    EXPECT_EQ(texts(lines, o)[1], "{\\frz0}x");
}

TEST(Interpolate, KeepsTheSpellingOfTheEnds)
{
    EXPECT_EQ(texts(run({"{\\fr10}x", "x", "{\\fr30}x"}))[1], "{\\fr20}x");
}

TEST(Interpolate, LeavesOutExcludedChanges)
{
    Options o;
    o.excluded = {"fscx@0"};
    EXPECT_EQ(texts(run({"{\\fscx100\\fs1}a", "{\\fscx7}a", "{\\fscx200\\fs3}a"}), o)[1], "{\\fscx7\\fs2}a");
}

TEST(Interpolate, StripsTheTagFromTheMiddleLinesTransforms)
{
    EXPECT_EQ(texts(run({"{\\fscx100}x", "{\\t(0,100,\\fscx50\\bord3)}x", "{\\fscx200}x"}))[1],
              "{\\t(0,100,\\bord3)\\fscx150}x");
}

TEST(Interpolate, LerpsTransformsThatHaveTheSameShape)
{
    EXPECT_EQ(texts(run({"{\\t(0,100,\\fscx100)}x", "x", "{\\t(100,300,\\fscx200)}x"}))[1],
              "{\\t(50,200,\\fscx150)}x");
    const auto p = plan(run({"{\\t(0,100,\\fscx100)}x", "x", "{\\t(\\frz3)}x"}), {});
    EXPECT_EQ(change(*p, "t@0")->skip, Skip::DifferentTransforms);
}

TEST(Interpolate, CascadesTransformsOverEachLine)
{
    EXPECT_EQ(cascadeTransforms("{\\t(\\fscx200)\\t(0,5,2,\\frz30)}x", 1000),
              "{\\t(0,500,\\fscx200)\\t(500,1000,2,\\frz30)}x");
    EXPECT_EQ(cascadeTransforms("{\\fs3}x", 1000), "{\\fs3}x");
    Options o;
    o.cascade = true;
    const auto out = texts(run({"{\\t(\\fs1)\\t(\\fs2)}x", "{\\t(\\fs1)\\t(\\fs2)}x", "{\\t(\\fs1)\\t(\\fs2)}x"}), o);
    EXPECT_EQ(out[0], "{\\t(0,500,\\fs1)\\t(500,1000,\\fs2)}x");
}
