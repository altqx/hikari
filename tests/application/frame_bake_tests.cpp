#include "hikari/application/frame_bake.h"

#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::application;

namespace {

core::StyleValues style(std::u8string name = u8"Default")
{
    core::StyleValues s;
    s.name = std::move(name);
    s.fontsize = u8"20";
    s.scaleX = s.scaleY = u8"100";
    s.spacing = s.angle = u8"0";
    s.outlineWidth = s.shadow = u8"2";
    s.primary = {255, 255, 255, 0};
    s.secondary = {255, 0, 0, 0};
    return s;
}

std::u8string bake(std::u8string_view text, double now, double duration = 100)
{
    return bakeAnimations(text, style(), {style(), style(u8"Alt")}, BakeFrame{now, duration, 640, 480});
}

}

TEST(FrameBake, TextWithoutAnimationsComesBackUnchanged)
{
    EXPECT_EQ(bake(u8"{\\bord2 }x{comment}", 50), u8"{\\bord2 }x{comment}");
    EXPECT_EQ(bake(u8"{\\move(5,5,5,5)\\fs30}x", 50), u8"{\\move(5,5,5,5)\\fs30}x");
    EXPECT_EQ(bake(u8"plain", 50), u8"plain");
}

TEST(FrameBake, TransformsReachTheirValueWithAcceleration)
{
    EXPECT_EQ(bake(u8"{\\t(0,100,2,\\bord4)}x", 50, 200), u8"{\\bord2.5}x");
    EXPECT_EQ(bake(u8"{\\t(100,200,\\bord4)}x", 50, 300), u8"{\\bord2}x");
    EXPECT_EQ(bake(u8"{\\t(100,200,\\bord4)}x", 200, 300), u8"{\\bord4}x");
    EXPECT_EQ(bake(u8"{\\t(\\fscx200)\\b1}x", 25), u8"{\\b1\\fscx125}x");
    EXPECT_EQ(bake(u8"{\\t(0,50,\\bord4)\\t(50,100,\\bord6)}x", 75), u8"{\\bord5}x");
    EXPECT_EQ(bake(u8"{\\fs10\\t(\\fs30)}a{\\t(\\fs40)}b", 50), u8"{\\fs20}a{\\fs30}b");
    EXPECT_EQ(bake(u8"{\\t(\\an5)}x", 50), u8"x");
    EXPECT_EQ(bake(u8"{\\t(\\an5)}x", 100), u8"{\\an5}x");
}

TEST(FrameBake, ColoursAlphasAndClipsMoveChannelByChannel)
{
    EXPECT_EQ(bake(u8"{\\1c&H0000FF&\\t(\\1c&HFF0000&)}x", 50), u8"{\\1c&H800080&}x");
    EXPECT_EQ(bake(u8"{\\t(\\c&H000000&)}x", 50), u8"{\\c&H808080&}x");
    EXPECT_EQ(bake(u8"{\\t(\\alpha&HFF&)}x", 50), u8"{\\alpha&H80&}x");
    EXPECT_EQ(bake(u8"{\\clip(0,0,100,100)\\t(\\clip(100,100,200,200))}x", 50), u8"{\\clip(50,50,150,150)}x");
    EXPECT_EQ(bake(u8"{\\t(\\clip(0,0,10,10))}x", 50), u8"{\\clip(0,0,325,245)}x");
}

TEST(FrameBake, ResetsStartFromTheirStyle)
{
    EXPECT_EQ(bake(u8"{\\bord5\\r\\t(\\bord4)}x", 50), u8"{\\bord5\\r\\bord3}x");
    EXPECT_EQ(bake(u8"{\\bord8\\t(\\bord4)}x", 50), u8"{\\bord6}x");
}

TEST(FrameBake, MovesFollowTheirTimesOrTheWholeLine)
{
    EXPECT_EQ(bake(u8"{\\move(0,0,100,0,300,100)}x", 200, 400), u8"{\\pos(50,0)}x");
    EXPECT_EQ(bake(u8"{\\move(0,0,100,0,0,0)}x", 25), u8"{\\pos(25,0)}x");
    EXPECT_EQ(bake(u8"{\\move(0,0,100,0)\\bord2\\move(9,9,1,1)}x", 100), u8"{\\pos(100,0)\\bord2}x");
}

TEST(FrameBake, FadesScaleEveryAlpha)
{
    EXPECT_EQ(bake(u8"{\\fade(255,0,255,0,100,200,300)}x", 50, 400), u8"{\\alpha&H80&}x");
    EXPECT_EQ(bake(u8"{\\fade(255,0,255,0,100,200,300)}x", 150, 400), u8"x");
    EXPECT_EQ(bake(u8"{\\fad(100,0)\\1a&H40&}x", 50, 1000), u8"{\\alpha&H80&\\1a&HA0&}x");
    EXPECT_EQ(bake(u8"x{\\fad(0,100)}y", 950, 1000), u8"{\\alpha&H80&}xy");
    EXPECT_EQ(bake(u8"{\\fad(100,0)\\fade(0,0,0,0,0,0,0)}x", 0, 1000), u8"{\\alpha&HFF&}x");
}

TEST(FrameBake, AFadeWritesEachStyleAlphaWhenTheyDiffer)
{
    auto faded = style();
    faded.back.a = 0x40;
    EXPECT_EQ(bakeAnimations(u8"{\\fad(100,0)}x", faded, {faded}, BakeFrame{50, 1000, 640, 480}),
              u8"{\\1a&H80&\\2a&H80&\\3a&H80&\\4a&HA0&}x");
}
