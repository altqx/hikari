#include "hikari/application/text_on_path.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari::application::text_on_path;

namespace {

double fixedWidth(std::string_view raw)
{
    if (const auto close = raw.rfind('}'); close != std::string_view::npos)
        raw.remove_prefix(close + 1);
    double width = 0;
    for (const unsigned char c : raw)
        width += c == ' ' ? 10.0 : c >= 0x80 ? (c >= 0xC0 ? 12.0 : 0.0) : 10.0 + (c % 7) * 2;
    return width;
}

const Path kExample = Path::fromDrawing("m 100 600 b 100 200 1100 200 1100 600");

struct Expected {
    const char *text;
    double x, y, frz;
};

}

TEST(TextOnPath, LeftPlacementMatchesBezierToTextOnItsExampleCurve)
{
    const std::vector<Expected> golden{
        {"I", 100.14217, 591.77713, 88.010}, {"t", 101.35428, 574.86955, 83.757}, {"r", 105.68822, 549.30437, 76.954},
        {"e", 109.60367, 534.75515, 72.917}, {"a", 115.98965, 516.85201, 67.829}, {"l", 123.88477, 499.65889, 62.865},
        {"l", 131.72513, 485.62187, 58.790}, {"y", 139.87081, 473.09765, 55.158}, {"w", 153.23978, 455.60641, 50.125},
        {"o", 163.84347, 443.65920, 46.730}, {"r", 176.62020, 430.91771, 43.158}, {"k", 187.04706, 421.57787, 40.574},
        {"s", 198.68080, 412.06963, 37.974}, {"!", 213.08207, 401.40779, 35.093},
    };
    // BezierToText takes the first t-step of 0.0001 past each centre, up to 0.25 px past the exact arc length.
    constexpr double kOneSampleStep = 0.25;
    const auto us = units("It really works!", fixedWidth, true);
    const Layout layout = place(kExample, us, {});
    ASSERT_EQ(layout.glyphs.size(), golden.size());
    for (std::size_t i = 0; i < golden.size(); ++i) {
        const Placed &p = layout.glyphs[i];
        EXPECT_EQ(us[p.unit].text, golden[i].text);
        EXPECT_NEAR(p.at.x, golden[i].x, kOneSampleStep) << i;
        EXPECT_NEAR(p.at.y, golden[i].y, kOneSampleStep) << i;
        EXPECT_NEAR(p.rotation, golden[i].frz, 0.1) << i;
    }
    EXPECT_EQ(layout.overflow, 0);
}

TEST(TextOnPath, ArcLengthIsExact)
{
    EXPECT_NEAR(Path::fromDrawing("m 0 0 l 30 40 l 30 100").length(), 110, 1e-9);
    const double k = 55.22847498;
    EXPECT_NEAR(Path::fromDrawing("m 100 0 b 100 " + std::to_string(k) + " " + std::to_string(k) + " 100 0 100").length(),
                50 * std::numbers::pi, 0.05);
    EXPECT_NEAR(Path::fromDrawing("m 0.5 -2.5 l 3.5 1.5").length(), 5, 1e-9) << "decimals and minus signs are read";
    EXPECT_NEAR(Path::fromDrawing("m 0 0 l 20 0", 2).length(), 10, 1e-9) << "the clip's scale divides";
}

TEST(TextOnPath, EverySegmentIsUsedAndTheTextTurnsWithIt)
{
    const Path corner = Path::fromDrawing("m 0 100 l 100 100 l 100 0");
    const Layout layout = place(corner, units("abcdefghijkl", fixedWidth, true), {});
    ASSERT_FALSE(layout.glyphs.empty());
    EXPECT_NEAR(layout.glyphs.front().at.y, 100, 1e-9);
    EXPECT_NEAR(layout.glyphs.front().rotation, 0, 1e-9);
    EXPECT_NEAR(layout.glyphs.back().at.x, 100, 1e-9);
    EXPECT_NEAR(layout.glyphs.back().rotation, 90, 1e-9);
}

TEST(TextOnPath, AlignmentOffsetAndSpread)
{
    const Path line = Path::fromDrawing("m 0 0 l 200 0");
    const auto us = units("abc", fixedWidth, true);
    double width = 0;
    for (const Unit &u : us)
        width += u.width;
    EXPECT_NEAR(place(line, us, {Align::Left, 5}).glyphs[0].at.x, 5 + us[0].width / 2, 1e-9);
    EXPECT_NEAR(place(line, us, {Align::Centre, 0}).glyphs[0].at.x, (200 - width) / 2 + us[0].width / 2, 1e-9);
    EXPECT_NEAR(place(line, us, {Align::Right, 5}).glyphs[2].at.x, 195 - us[2].width / 2, 1e-9);
    const Layout spread = place(line, us, {Align::Spread, 0});
    EXPECT_NEAR(spread.glyphs[0].at.x, us[0].width / 2, 1e-9);
    EXPECT_NEAR(spread.glyphs[2].at.x, 200 - us[2].width / 2, 1e-9);
}

TEST(TextOnPath, ReverseRunsThePathTheOtherWay)
{
    const Path line = Path::fromDrawing("m 0 0 l 200 0");
    const auto us = units("ab", fixedWidth, true);
    const Layout layout = place(line, us, {Align::Left, 0, true});
    EXPECT_NEAR(layout.glyphs[0].at.x, 200 - us[0].width / 2, 1e-9);
    EXPECT_NEAR(std::fmod(layout.glyphs[0].rotation + 360, 360), 180, 1e-9);
}

TEST(TextOnPath, TextLongerThanThePathGoesStraightOnFromItsEnd)
{
    const Path line = Path::fromDrawing("m 0 0 l 30 0");
    const auto us = units("abcd", fixedWidth, true);
    const Layout layout = place(line, us, {});
    EXPECT_NEAR(layout.overflow, layout.width - 30, 1e-9);
    EXPECT_GT(layout.glyphs.back().at.x, 30);
    EXPECT_NEAR(layout.glyphs.back().at.y, 0, 1e-9);
}

TEST(TextOnPath, UnitsAreGraphemeClustersWithTheTagsBeforeThem)
{
    const auto us = units("{\\pos(1,2)\\an5\\frz30\\c&H0000FF&}ก\xe0\xb8\xb4 {\\b1}x\\Ny", fixedWidth, true);
    ASSERT_EQ(us.size(), 5u);
    EXPECT_EQ(us[0].text, "ก\xe0\xb8\xb4") << "the mark stays on its base";
    EXPECT_EQ(us[0].tags, "\\c&H0000FF&") << "positioning and rotation are not carried";
    EXPECT_TRUE(us[1].space);
    EXPECT_EQ(us[2].tags, "\\c&H0000FF&\\b1");
    EXPECT_TRUE(us[3].space) << "\\N is a space";
    EXPECT_EQ(units("{\\frz30}x", fixedWidth, false)[0].tags, "\\frz30") << "kept when Rotate is off";
}

TEST(TextOnPath, GlyphTextPutsTheBottomCentreOnThePath)
{
    const Unit u{"\\c&H0000FF&", "x", 10, false};
    EXPECT_EQ(glyphText(u, {0, {10.25, 20}, -12.5}, true), "{\\c&H0000FF&\\an2\\pos(10.25,20)\\frz-12.5}x");
    EXPECT_EQ(glyphText(u, {0, {10.25, 20}, 0}, false), "{\\c&H0000FF&\\an2\\pos(10.25,20)}x");
}

TEST(TextOnPath, SlideMovesTheTextAcrossTheRoomThePathLeaves)
{
    const Path line = Path::fromDrawing("m 0 0 l 200 0");
    const auto us = units("ab", fixedWidth, true);
    const double room = 200 - us[0].width - us[1].width;
    EXPECT_NEAR(place(line, us, {}, 0.0).glyphs[0].at.x, us[0].width / 2, 1e-9);
    EXPECT_NEAR(place(line, us, {}, 1.0).glyphs[0].at.x, room + us[0].width / 2, 1e-9);
    EXPECT_NEAR(place(line, us, {Align::Left, 0, true}, 0.0).glyphs[0].at.x, room + us[0].width / 2, 1e-9)
        << "Reverse slides right to left";
    EXPECT_EQ(slideSteps(48, 2), 24);
    EXPECT_EQ(slideSteps(5, 2), 3);
    EXPECT_EQ(slideSteps(0, 2), 0);
}
