// Y9: subtitles read from a Matroska track, against legacy Demux::GetSubtitles
// and its packet callback (Demux.cpp:66-176, 262-315 at 20d647c4),
// Dialogue::SetRaw (SubsDialogue.cpp:698-823), SubsFile::AddSInfo
// (SubsFile.cpp:1020-1050), SubsTime::raw/NewTime (SubsTime.cpp:85-140) and
// SubsGrid::SetSubsFormat (SubsGridBase.cpp:1093-1105).

#include "hikari/core/ass_save.h"
#include "hikari/core/matroska.h"
#include "hikari/core/srt.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace hikari::core;

namespace {

std::u8string u8(std::string_view s)
{
    return std::u8string(s.begin(), s.end());
}

std::string text(const std::vector<std::byte> &bytes)
{
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::int64_t ms(DocumentTime t)
{
    return t.microseconds() / 1000;
}

std::u8string ass(std::int64_t start, std::int64_t duration, std::string_view line)
{
    return matroskaLine(start, duration, line, MatroskaCodec::Ass);
}

} // namespace

TEST(MatroskaCodec, LegacyCodecType)
{
    // Demux.cpp:111-117: "ass" 0, "ssa" 1, anything else 2.
    EXPECT_EQ(matroskaCodec(u8"ass"), MatroskaCodec::Ass);
    EXPECT_EQ(matroskaCodec(u8"ssa"), MatroskaCodec::Ssa);
    EXPECT_EQ(matroskaCodec(u8"subrip"), MatroskaCodec::Text);
    EXPECT_EQ(matroskaCodec(u8"srt"), MatroskaCodec::Text);
    EXPECT_EQ(matroskaCodec(u8"text"), MatroskaCodec::Text);
    EXPECT_EQ(matroskaCodec(u8"ASS"), MatroskaCodec::Text);
}

TEST(MatroskaLine, AssTimesAddFiveThenTruncateToCentiseconds)
{
    // Demux.cpp:269-276: end = start + duration; both +5 then ZEROIT.
    EXPECT_EQ(ass(1234, 2000, "0,0,Default,,0,0,0,,Hello, world"),
              u8"Dialogue: 0,0:00:01.23,0:00:03.23,Default,,0,0,0,,Hello, world");
    EXPECT_EQ(ass(500, 1000, "1,2,Sign,Actor,10,20,30,fx,{\\pos(1,2)}Sign"),
              u8"Dialogue: 2,0:00:00.50,0:00:01.50,Sign,Actor,10,20,30,fx,{\\pos(1,2)}Sign");
    // 1235 + 5 rounds up a centisecond; 1234 + 5 does not.
    EXPECT_EQ(ass(1235, 10, "0,0,D,,0,0,0,,t"), u8"Dialogue: 0,0:00:01.24,0:00:01.25,D,,0,0,0,,t");
    // 4000 + 994 = 4994, + 5 = 4999: the end truncates to 4.99.
    EXPECT_EQ(ass(4000, 994, "4,0,Default,,0,0,0,,t"), u8"Dialogue: 0,0:00:04.00,0:00:04.99,Default,,0,0,0,,t");
    // 7199996 + 5 crosses into the third hour; the end too.
    EXPECT_EQ(ass(7199996, 1, "3,0,Default,,0,0,0,,large"), u8"Dialogue: 0,2:00:00.00,2:00:00.00,Default,,0,0,0,,large");
    // SSA packets are timed the same way (codecType 1 < 2).
    EXPECT_EQ(matroskaLine(2000, 500, "0,Marked=0,Default,,0000,0000,0000,,SSA line", MatroskaCodec::Ssa),
              u8"Dialogue: 0,0:00:02.00,0:00:02.50,Default,,0000,0000,0000,,SSA line");
}

TEST(MatroskaLine, NegativeTimesAreZeroAndIntWraps)
{
    // ZEROIT truncates towards zero; SubsTime::NewTime makes a negative time 0.
    EXPECT_EQ(ass(-30, 10, "0,0,D,,0,0,0,,t"), u8"Dialogue: 0,0:00:00.00,0:00:00.00,D,,0,0,0,,t");
    // int startTime = Start (Demux.cpp:269): 2^32 + 1000 wraps to 1000.
    EXPECT_EQ(ass((std::int64_t{1} << 32) + 1000, 1000, "0,0,D,,0,0,0,,t"),
              u8"Dialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,t");
    // A time past INT_MAX after +5 wraps negative and becomes 0.
    EXPECT_EQ(ass(2147483646, 0, "0,0,D,,0,0,0,,t"), u8"Dialogue: 0,0:00:00.00,0:00:00.00,D,,0,0,0,,t");
}

TEST(MatroskaLine, ReadOrderAndLayerFields)
{
    // An empty layer field: Find returns 0, `if (pos)` skips it, layer 0 and
    // the leading comma stays (Demux.cpp:288-297).
    EXPECT_EQ(ass(3725, 10, "2,,Default,,0,0,0,,no layer"), u8"Dialogue: 0,0:00:03.73,0:00:03.74,Default,,0,0,0,,no layer");
    // wxString::ToLong (wx 3.3.3) skips leading blanks and keeps a partial number.
    EXPECT_EQ(ass(4500, 99, "8, 3,Default,,0,0,0,,spaced layer"),
              u8"Dialogue: 3,0:00:04.50,0:00:04.60,Default,,0,0,0,,spaced layer");
    EXPECT_EQ(ass(4600, 100, "9,12abc,Default,,0,0,0,,partial layer"),
              u8"Dialogue: 12,0:00:04.60,0:00:04.70,Default,,0,0,0,,partial layer");
    EXPECT_EQ(ass(4600, 100, "9,x,Default,,0,0,0,,no number"), u8"Dialogue: 0,0:00:04.60,0:00:04.70,Default,,0,0,0,,no number");
    // No comma: Find gives -1, Left(-1) and Mid(0) are the whole block.
    EXPECT_EQ(ass(4100, 100, "x"), u8"Dialogue: 0,0:00:04.10,0:00:04.20,x");
    EXPECT_EQ(ass(4200, 100, "56"), u8"Dialogue: 56,0:00:04.20,0:00:04.30,56");
    // An empty Style: the block left after the layer starts with its comma,
    // so nothing is prepended and the Style field is lost (Demux.cpp:297);
    // Dialogue::SetRaw then reads the Name as the Style (proposed departure
    // Y9-empty-style, kept).
    EXPECT_EQ(ass(4000, 994, "4,0,,Actor,0,0,0,,empty style"), u8"Dialogue: 0,0:00:04.00,0:00:04.99,Actor,0,0,0,,empty style");
    // Nothing left after the two fields: the Default block.
    EXPECT_EQ(ass(1000, 0, "1,2,"), u8"Dialogue: 2,0:00:01.00,0:00:01.00,Default,,0000,0000,0000,,");
}

TEST(MatroskaLine, BytesThatAreNotUtf8AreEmpty)
{
    // wxString(Line, wxConvUTF8) is empty for invalid UTF-8 (Demux.cpp:265).
    EXPECT_EQ(ass(4300, 100, "6,0,Default,,0,0,0,,\xff\xfe"),
              u8"Dialogue: 0,0:00:04.30,0:00:04.40,Default,,0000,0000,0000,,");
    EXPECT_EQ(matroskaLine(1000, 1000, "\xc0", MatroskaCodec::Text), u8"00:00:01,000 --> 00:00:02,000\r\n");
    EXPECT_EQ(ass(0, 0, "0,0,D,,0,0,0,,\xc5\xbc\xc3\xb3\xc5\x82w"), u8"Dialogue: 0,0:00:00.00,0:00:00.00,D,,0,0,0,,żółw");
}

TEST(MatroskaLine, TextPacketsKeepMilliseconds)
{
    // Demux.cpp:304-306: no rounding, SRT times, "\r\n" then the packet.
    EXPECT_EQ(matroskaLine(1001, 1499, "Line one\nLine two", MatroskaCodec::Text),
              u8"00:00:01,001 --> 00:00:02,500\r\nLine one\nLine two");
    EXPECT_EQ(matroskaLine(0, 0, "At zero", MatroskaCodec::Text), u8"00:00:00,000 --> 00:00:00,000\r\nAt zero");
    EXPECT_EQ(matroskaLine(3661001, 2999, "a\r\nb", MatroskaCodec::Text), u8"01:01:01,001 --> 01:01:04,000\r\na\r\nb");
    EXPECT_EQ(matroskaLine(-5, 10, "neg", MatroskaCodec::Text), u8"00:00:00,000 --> 00:00:00,005\r\nneg");
}

namespace {

const std::string kPrivate = "[Script Info]\r\n"
                             "; Script generated by a muxer\r\n"
                             "Title: Fixture\r\n"
                             "ScriptType: v4.00+\r\n"
                             "PlayResX: 1920\r\n"
                             "NoColonLine\r\n"
                             "Title: Fixture again\r\n"
                             "YCbCr Matrix: None\r\n"
                             "\r\n"
                             "[V4+ Styles]\r\n"
                             "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                             "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                             "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
                             "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,"
                             "0,1,2,2,2,10,10,10,1\r\n"
                             "Late: after the styles\r\n"
                             "\n"
                             "[Events]\n"
                             "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                             "Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,,header comment\n";

const std::string kAssHeader =
    "[Script Info]\r\n"
    "Title: Fixture again\r\n"
    "ScriptType: v4.00+\r\n"
    "PlayResX: 1920\r\n"
    "NoColonLine: \r\n"
    "YCbCr Matrix: TV.601\r\n"
    "\r\n[V4+ Styles]\r\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
    "MarginV, Encoding\r\n"
    "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\r\n"
    "\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";

} // namespace

TEST(MatroskaDocument, AssHeaderFromTheCodecPrivateData)
{
    // Demux.cpp:128-162: Script Info until the first Style (comments,
    // sections and Format lines skipped; a key seen again keeps its place and
    // takes the new value; a line without ':' is a key with no value), the
    // Styles, the Comment lines, then the packets; YCbCr Matrix None becomes
    // TV.601 in place.
    const std::vector<std::u8string> lines{ass(1234, 2000, "0,0,Default,,0,0,0,,Hello, world"),
                                           ass(500, 1000, "1,2,Sign,Actor,10,20,30,fx,Sign")};
    const Document d = matroskaDocument(kPrivate, lines, MatroskaCodec::Ass);
    EXPECT_EQ(d.format(), SubtitleFormat::Ass);
    EXPECT_EQ(text(encodeAss(d)), kAssHeader +
                                      "Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,,header comment\r\n"
                                      "Dialogue: 0,0:00:01.23,0:00:03.23,Default,,0,0,0,,Hello, world\r\n"
                                      "Dialogue: 2,0:00:00.50,0:00:01.50,Sign,Actor,10,20,30,fx,Sign\r\n");
    ASSERT_EQ(d.lines().size(), 3u);
    EXPECT_TRUE(d.lines()[0]->comment);
    EXPECT_EQ(ms(d.lines()[1]->start.value), 1230);
    EXPECT_EQ(ms(d.lines()[1]->end.value), 3230);
    EXPECT_EQ(d.lines()[2]->layer.value, 2);
    EXPECT_EQ(d.lines()[2]->actor, u8"Actor");
    EXPECT_EQ(d.scriptInfo(u8"Late"), std::nullopt);
    EXPECT_EQ(d.scriptInfo(u8"YCbCr Matrix"), u8"TV.601");
}

TEST(MatroskaDocument, MatrixIsAddedForAssOnly)
{
    // codecType < 1 (Demux.cpp:161): an ASS track without the key gains it at
    // the end; one with a value keeps it; an SSA track is left alone.
    const auto noMatrix = matroskaDocument("[Script Info]\r\nTitle: t\r\n", {}, MatroskaCodec::Ass);
    EXPECT_EQ(noMatrix.scriptInfo(u8"YCbCr Matrix"), u8"TV.601");
    EXPECT_TRUE(text(encodeAss(noMatrix)).starts_with("[Script Info]\r\nTitle: t\r\nYCbCr Matrix: TV.601\r\n\r\n"));
    const auto kept = matroskaDocument("YCbCr Matrix: TV.709\r\n", {}, MatroskaCodec::Ass);
    EXPECT_EQ(kept.scriptInfo(u8"YCbCr Matrix"), u8"TV.709");
    const auto ssa = matroskaDocument("[Script Info]\r\nScriptType: v4.00\r\n", {}, MatroskaCodec::Ssa);
    EXPECT_EQ(ssa.scriptInfo(u8"YCbCr Matrix"), std::nullopt);
}

TEST(MatroskaDocument, SsaStylesAreReadWithTheSsaLayout)
{
    // Styles(next, 2) for SSA (Demux.cpp:141; styles.cpp:288-330): the
    // TertiaryColour is skipped, BackColour is read as the outline and copied
    // to the back colour.
    const std::string priv = "[V4 Styles]\r\n"
                             "Style: Default,Arial,20,16777215,65535,65535,-2147483640,-1,0,1,3,0,2,30,30,30,0,0\r\n";
    const auto d = matroskaDocument(priv, {}, MatroskaCodec::Ssa);
    const std::string out = text(encodeAss(d));
    const auto at = out.find("Style: Default,Arial,20,");
    ASSERT_NE(at, std::string::npos) << out;
    EXPECT_EQ(out.substr(at, out.find("\r\n", at) - at),
              "Style: Default,Arial,20,&H00FFFFFF,&H0000FFFF,&H80000008,&H80000008,-1,0,0,0,100,100,0,0,1,3,0,2,30,30,30,0");
}

TEST(MatroskaDocument, TextTracksAreSrtDocuments)
{
    // No private data is read for a text track; the first SRT Line makes the
    // Document SRT (SetSubsFormat), saved as numbered cues.
    const std::vector<std::u8string> lines{matroskaLine(1001, 1499, "Line one\nLine two", MatroskaCodec::Text),
                                           matroskaLine(0, 0, "At zero", MatroskaCodec::Text)};
    const auto d = matroskaDocument("Title: ignored\r\n", lines, MatroskaCodec::Text);
    EXPECT_EQ(d.format(), SubtitleFormat::Srt);
    ASSERT_EQ(d.lines().size(), 2u);
    EXPECT_EQ(d.lines()[0]->text, u8"Line one\\NLine two");
    EXPECT_EQ(ms(d.lines()[0]->start.value), 1001);
    EXPECT_EQ(ms(d.lines()[0]->end.value), 2500);
    EXPECT_EQ(ms(d.lines()[1]->start.value), 0);
    EXPECT_EQ(text(encodeSrt(d)), "1\r\n00:00:01,001 --> 00:00:02,500\r\nLine one\r\nLine two\r\n\r\n"
                                  "2\r\n00:00:00,000 --> 00:00:00,000\r\nAt zero\r\n\r\n");
}

TEST(MatroskaDocument, PlainTextLinesKeepTheAssFormat)
{
    // "56" becomes "Dialogue: 56,...,56", fewer than nine fields: a plain text
    // Line (Format 0), which SetSubsFormat passes over, so the Document is ASS.
    const std::vector<std::u8string> lines{ass(4200, 100, "56"), ass(1000, 1000, "0,0,Default,,0,0,0,,real")};
    const auto d = matroskaDocument("", lines, MatroskaCodec::Ass);
    EXPECT_EQ(d.format(), SubtitleFormat::Ass);
    ASSERT_EQ(d.lines().size(), 2u);
    EXPECT_EQ(d.lines()[0]->text, u8"Dialogue: 56,0:00:04.20,0:00:04.30,56");
    EXPECT_EQ(d.lines()[0]->style, u8"Default");
    EXPECT_EQ(ms(d.lines()[0]->start.value), 0);
    EXPECT_EQ(d.lines()[1]->text, u8"real");
}

TEST(MatroskaDocument, EmptyTrackIsAnEmptyAssDocument)
{
    const auto d = matroskaDocument("", {}, MatroskaCodec::Ass);
    EXPECT_EQ(d.format(), SubtitleFormat::Ass);
    EXPECT_TRUE(d.lines().empty());
    EXPECT_EQ(d.scriptInfo(u8"YCbCr Matrix"), u8"TV.601");
}

TEST(MatroskaDocument, PrivateDataThatIsNotUtf8IsIgnored)
{
    // wxString(privData, wxConvUTF8) is empty: no header at all.
    const auto d = matroskaDocument("Title: caf\xe9\r\nStyle: X,Arial,20\r\n", {}, MatroskaCodec::Ass);
    EXPECT_EQ(d.scriptInfo(u8"Title"), std::nullopt);
    EXPECT_EQ(text(encodeAss(d)).find("Style: X"), std::string::npos);
}

TEST(MatroskaDocument, EmptyStyleShiftsTheFields)
{
    // "Dialogue: 0,s,e,Actor,0,0,0,,empty style" has nine fields: Dialogue::
    // SetRaw reads Style "Actor", Name "0", Effect "empty style" and no text.
    const auto d = matroskaDocument("", {ass(4000, 994, "4,0,,Actor,0,0,0,,empty style")}, MatroskaCodec::Ass);
    ASSERT_EQ(d.lines().size(), 1u);
    EXPECT_EQ(d.lines()[0]->style, u8"Actor");
    EXPECT_EQ(d.lines()[0]->actor, u8"0");
    EXPECT_EQ(d.lines()[0]->effect, u8"empty style");
    EXPECT_EQ(d.lines()[0]->text, u8"");
}
