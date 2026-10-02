// C4-ssa: Style values as the legacy Styles::parseStyle reads ASS and SSA v4.

#include "hikari/core/ass_load.h"
#include "hikari/core/style.h"

#include <QFile>
#include <gtest/gtest.h>

#include <cstring>

using namespace hikari::core;

namespace {

std::vector<std::byte> readFile(const QString &path)
{
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    const QByteArray d = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(d.size()));
    std::memcpy(out.data(), d.constData(), out.size());
    return out;
}

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

} // namespace

TEST(Style, AssLayout)
{
    const auto v = legacy::decodeStyle(
        u8"Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,-1,0,0,0,100,100,0,0,3,2,0,2,10,10,10,1",
        false);
    EXPECT_TRUE(v.complete);
    EXPECT_EQ(v.name, u8"Default");
    EXPECT_EQ(v.primary, (Colour{255, 255, 255, 0}));
    EXPECT_EQ(v.secondary, (Colour{255, 0, 0, 0}));
    EXPECT_EQ(v.back, (Colour{0, 0, 0, 0x80}));
    EXPECT_TRUE(v.bold);            // any token but "0"
    EXPECT_TRUE(v.borderStyle);     // only "3"
    EXPECT_EQ(v.alignment, u8"2");
    EXPECT_FALSE(v.ssaTertiaryColour);
}

TEST(Style, SsaV4CaptureInput)
{
    // tools/legacy-capture/inputs/ssa-v4.ssa: decimal and hex colours, SSA
    // alignment numbering, Tertiary colour and AlphaLevel with no ASS field.
    const auto doc = loadAss(readFile(QStringLiteral(HIKARI_CAPTURE_INPUTS "/ssa-v4.ssa"))).document;
    const auto styles = decodeStyles(doc);
    ASSERT_EQ(styles.size(), 3u);
    const auto &d = styles[0];
    EXPECT_TRUE(d.complete);
    EXPECT_EQ(d.primary, (Colour{255, 255, 255, 0}));   // 16777215
    EXPECT_EQ(d.secondary, (Colour{255, 255, 0, 0}));   // 65535
    EXPECT_EQ(d.outline, (Colour{255, 0, 0, 0}));       // SSA BackColour 255
    EXPECT_EQ(d.back, d.outline);
    EXPECT_EQ(d.ssaTertiaryColour, u8"65280");
    EXPECT_EQ(d.ssaAlphaLevel, u8"0");
    EXPECT_TRUE(d.bold);
    EXPECT_FALSE(d.italic);
    EXPECT_FALSE(d.underline);
    EXPECT_EQ(d.scaleX, u8"100");
    EXPECT_EQ(d.marginLeft, u8"10");
    EXPECT_EQ(d.marginRight, u8"20");
    EXPECT_EQ(d.marginVertical, u8"30");
    EXPECT_EQ(d.encoding, u8"0");
    EXPECT_EQ(styles[1].alignment, u8"8"); // SSA 6, top centre
    EXPECT_TRUE(styles[1].borderStyle);
    EXPECT_EQ(styles[1].back, (Colour{0x80, 0, 0, 0}));
    EXPECT_EQ(styles[2].alignment, u8"4"); // SSA 9, middle left
}

TEST(Style, SsaLayoutStaysOnForLaterSections)
{
    // The legacy loader never switches back once it has seen "[V4 Styles]".
    const auto doc = loadAss(bytesOf("[V4 Styles]\nStyle: A,Arial,20,1,2,3,4,0,0,1,2,2,6,1,2,3,0,0\n"
                                     "[V4+ Styles]\nStyle: B,Arial,20,1,2,3,4,0,0,1,2,2,6,1,2,3,0,0\n"))
                         .document;
    const auto styles = decodeStyles(doc);
    ASSERT_EQ(styles.size(), 2u);
    EXPECT_TRUE(styles[1].complete);
    EXPECT_EQ(styles[1].alignment, u8"8");
}

TEST(Style, ShortLinesAndNames)
{
    const auto shortLine = legacy::decodeStyle(u8"Style: S,Arial,20,&H00FFFFFF&", false);
    EXPECT_FALSE(shortLine.complete);
    EXPECT_EQ(shortLine.fontname, u8"Arial");
    EXPECT_EQ(legacy::decodeStyle(u8"Style:NoSpace,Arial", false).name, u8"");     // AfterFirst(' ')
    EXPECT_EQ(legacy::decodeStyle(u8"Style:  Two", false).name, u8" Two");
    // A trailing comma adds no empty token, so the line is one field short.
    EXPECT_FALSE(legacy::decodeStyle(
                     u8"Style: T,Arial,20,1,2,3,4,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,", false)
                     .complete);
}

TEST(Style, LegacyColourParsing)
{
    EXPECT_EQ(legacy::colour(u8"&H0000FF&"), (Colour{255, 0, 0, 0}));
    EXPECT_EQ(legacy::colour(u8"&h40ff0000"), (Colour{0, 0, 255, 0x40}));
    EXPECT_EQ(legacy::colour(u8"#FF8000"), (Colour{255, 0x80, 0, 0})); // HTML order
    EXPECT_EQ(legacy::colour(u8"-"), (Colour{}));                      // IsNumber, ToLong fails
}
