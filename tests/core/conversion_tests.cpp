// Y5: format conversion against legacy SubsGrid::Convert, Dialogue::Convert
// and SaveFile at 20d647c4.

#include "hikari/core/ass_load.h"
#include "hikari/core/conversion.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"
#include "hikari/core/subtitle_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari::core;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return b;
}

std::string encoded(const Document &d)
{
    const auto b = encodeSubtitle(d);
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

constexpr std::string_view kAss =
    "[Script Info]\nPlayResX: 640\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,{\\i1}Later{\\i0} line\\Nsecond\n"
    "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,note\n"
    "Dialogue: 2,0:00:01.00,0:00:02.00,Sign,Ann,0,0,0,,First\n"
    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\p1}m 0 0 l 1 1{\\p0}\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,{\\i1}Later{\\i0} line\\Nsecond\n";

ConversionOptions options()
{
    ConversionOptions o;
    o.style.name = u8"Default";
    o.style.fontname = u8"Garamond";
    o.style.fontsize = u8"40";
    o.style.scaleX = o.style.scaleY = u8"100";
    o.style.spacing = o.style.angle = u8"0";
    o.style.outlineWidth = o.style.shadow = u8"2";
    o.style.alignment = u8"2";
    o.style.marginLeft = o.style.marginRight = o.style.marginVertical = u8"20";
    o.style.encoding = u8"1";
    return o;
}

} // namespace

TEST(Conversion, AssToSrtDropsCommentsDrawingsAndDuplicates)
{
    const auto ass = loadAss(bytes(kAss)).document;
    const auto ids = ass.lines();
    const auto r = convertDocument(ass, SubtitleFormat::Srt, options());
    ASSERT_TRUE(r);
    EXPECT_EQ(r->document.format(), SubtitleFormat::Srt);
    EXPECT_EQ(encoded(r->document), "1\r\n00:00:01,000 --> 00:00:02,000\r\nFirst\r\n\r\n"
                                    "2\r\n00:00:05,000 --> 00:00:06,000\r\n<i>Later</i> line\r\nsecond\r\n\r\n");
    // Survivors keep their LineIds: First, and the later of the two equal Lines.
    const auto lines = r->document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0]->id, ids[2]->id);
    EXPECT_EQ(lines[1]->id, ids[4]->id);
    ConversionReport expected;
    expected.commentsRemoved = 1;
    expected.duplicatesRemoved = 1;
    expected.emptyRemoved = 1;
    expected.drawingsCleared = 1;
    expected.textChanged = 3;
    expected.fieldsDropped = 1;
    expected.reordered = true;
    expected.headerDropped = true;
    EXPECT_EQ(r->report, expected);
}

TEST(Conversion, AssToLineFormats)
{
    const auto ass = loadAss(bytes(kAss)).document;
    const auto mdvd = convertDocument(ass, SubtitleFormat::MicroDvd, options());
    ASSERT_TRUE(mdvd);
    // Frames rounded up at the conversion rate; ASS tags removed, \N as |.
    EXPECT_EQ(encoded(mdvd->document), "{24}{48}First\r\n{120}{144}Later line|second\r\n");
    ASSERT_TRUE(mdvd->document.frameRate());
    EXPECT_EQ(mdvd->document.lines()[0]->start.value.microseconds(), 1'001'001); // frame 24 at 23.976
    EXPECT_EQ(encoded(convertDocument(ass, SubtitleFormat::TMPlayer, options())->document),
              "00:00:01:First\r\n00:00:05:Later line|second\r\n");
    EXPECT_EQ(encoded(convertDocument(ass, SubtitleFormat::Mpl2, options())->document),
              "[10][20]First\r\n[50][60]Later line|second\r\n");
}

TEST(Conversion, SrtToAssWithTheConversionStyleAndPrefix)
{
    const auto srt = loadSrt(bytes("1\r\n00:00:01,234 --> 00:00:02,000\r\n<i>Hi</i>\r\nthere\r\n\r\n")).document;
    auto o = options();
    o.prefix = u8"{\\an8}";
    o.resolutionWidth = u8"1920";
    o.resolutionHeight = u8"1080";
    const auto r = convertDocument(srt, SubtitleFormat::Ass, o);
    ASSERT_TRUE(r);
    const std::string file = encoded(r->document);
    EXPECT_NE(file.find("PlayResX: 1920\r\nPlayResY: 1080\r\n"), std::string::npos) << file;
    EXPECT_NE(file.find("\r\nStyle: Default,Garamond,40,"), std::string::npos) << file;
    // Times keep centiseconds (ZEROIT); SRT markup becomes tags after the prefix.
    EXPECT_NE(file.find("Dialogue: 0,0:00:01.23,0:00:02.00,Default,,0,0,0,,{\\an8}{\\i1}Hi{\\i0}\\Nthere\r\n"),
              std::string::npos)
        << file;
    EXPECT_EQ(r->report.timesChanged, 1);
    EXPECT_EQ(r->report.fieldsDropped, 1);
    EXPECT_EQ(r->document.lines()[0]->id, srt.lines()[0]->id);
}

TEST(Conversion, NewEndTimesFromTheTextLength)
{
    // TMPlayer always gets them; the previous end is cut at the next start.
    const auto tmp = loadLineFormats(bytes("00:00:01:short\n00:00:01:a longer line of text here\n")).document;
    const auto r = convertDocument(tmp, SubtitleFormat::Ass, options());
    ASSERT_TRUE(r);
    const std::string file = encoded(r->document);
    EXPECT_NE(file.find("Dialogue: 0,0:00:01.00,0:00:01.00,Default,,0,0,0,,short\r\n"
                        "Dialogue: 0,0:00:01.00,0:00:03.86,Default,,0,0,0,,a longer line of text here\r\n"),
              std::string::npos)
        << file;
    // From ASS only with the option, and never to TMPlayer.
    const auto ass = loadAss(bytes(kAss)).document;
    auto o = options();
    o.newEndTimes = true;
    o.timePerCharacter = 200;
    // Legacy sets them in Document order before sorting: the first "Later"
    // Line's end is cut to the start of the Line after it (1 s), so it no
    // longer equals its twin and both stay, one ending before it starts.
    EXPECT_EQ(encoded(convertDocument(ass, SubtitleFormat::Mpl2, o)->document),
              "[10][20]First\r\n[50][10]Later line|second\r\n[50][84]Later line|second\r\n");
    EXPECT_EQ(encoded(convertDocument(ass, SubtitleFormat::TMPlayer, o)->document),
              "00:00:01:First\r\n00:00:05:Later line|second\r\n");
}

TEST(Conversion, RefusedForTheSameFormatPlainTextOrAnInvalidFps)
{
    const auto ass = loadAss(bytes(kAss)).document;
    EXPECT_FALSE(convertDocument(ass, SubtitleFormat::Ass, options()));
    EXPECT_FALSE(convertDocument(ass, SubtitleFormat::PlainText, options()));
    auto o = options();
    o.fps = 0.5;
    EXPECT_FALSE(convertDocument(ass, SubtitleFormat::Srt, o));
}
