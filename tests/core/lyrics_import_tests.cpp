#include "hikari/core/lyrics_import.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari::core;
using Kind = LyricsProblem::Kind;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
        out[i] = static_cast<std::byte>(text[i]);
    return out;
}

std::vector<std::byte> fixture(const std::string &name)
{
    std::ifstream in(std::string(HIKARI_LYRICS_FIXTURES) + "/" + name, std::ios::binary);
    EXPECT_TRUE(in) << name;
    return bytes(std::string(std::istreambuf_iterator<char>(in), {}));
}

struct Row {
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::string actor;
    std::string text;
    bool operator==(const Row &) const = default;
};

void PrintTo(const Row &row, std::ostream *os)
{
    *os << row.start << "-" << row.end << " [" << row.actor << "] " << row.text;
}

std::string str(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

std::vector<Row> rows(const LoadResult &result)
{
    std::vector<Row> out;
    for (const LineRecord *line : result.document.lines())
        out.push_back({line->start.value.microseconds() / 1000, line->end.value.microseconds() / 1000, str(line->actor),
                       str(line->text)});
    return out;
}

void expectSyllablesFillTheLines(const LoadResult &result)
{
    const std::regex k(R"(\{\\k(\d+)\})");
    for (const LineRecord *line : result.document.lines()) {
        const std::string text = str(line->text);
        if (text.find("{\\k") == std::string::npos)
            continue;
        std::int64_t total = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), k); it != std::sregex_iterator(); ++it)
            total += std::stoll((*it)[1]);
        EXPECT_EQ(total * 10, (line->end.value.microseconds() - line->start.value.microseconds()) / 1000) << text;
    }
}

LyricsProblem problemOf(std::string_view text, std::u8string_view extension)
{
    const auto result = importLyrics(bytes(text), extension);
    EXPECT_FALSE(result.has_value()) << text;
    return result ? LyricsProblem{} : result.error();
}

}

TEST(LyricsImport, Extensions)
{
    EXPECT_TRUE(isLyricsExtension(u8"lrc"));
    EXPECT_TRUE(isLyricsExtension(u8"ttml"));
    EXPECT_FALSE(isLyricsExtension(u8"ass"));
    EXPECT_FALSE(isLyricsExtension(u8"txt"));
}

TEST(LyricsImport, EnhancedLrcBecomesKaraokeLines)
{
    const auto result = importLrc(fixture("enhanced.lrc"));
    ASSERT_TRUE(result.has_value()) << int(result.error().kind) << " line " << result.error().line;
    EXPECT_EQ(result->document.format(), SubtitleFormat::Ass);
    EXPECT_EQ(result->document.scriptInfo(u8"Title"), u8"Twinkle, Twinkle, Little Star");
    const auto spaced = importLrc(bytes("[ti:  My \t  Song ]\n[00:01.00]x\n"));
    ASSERT_TRUE(spaced.has_value());
    EXPECT_EQ(spaced->document.scriptInfo(u8"Title"), u8"My Song");
    const auto kept = importLrc(bytes("[ti:Song]\n[ti: ]\n[ti:\t]\n[00:01.00]x\n"));
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(kept->document.scriptInfo(u8"Title"), u8"Song");
    const auto later = importLrc(bytes("[ti:]\n[ti:First]\n[ti:Second]\n[00:01.00]x\n"));
    ASSERT_TRUE(later.has_value());
    EXPECT_EQ(later->document.scriptInfo(u8"Title"), u8"Second");
    const std::vector<Row> expected{
        {1000, 4000, "", "{\\k20}{\\k70}Twinkle, {\\k70}twinkle, {\\k50}little {\\k90}star"},
        {4500, 8000, "", "{\\k60}How {\\k30}I {\\k80}wonder {\\k40}what {\\k40}you {\\k100}are"},
        {8000, 11200, "v1", "{\\k40}Up {\\k20}a{\\k40}bove {\\k30}the {\\k60}world {\\k50}so {\\k80}high"},
        {11500, 15000, "v2", "{\\k40}Like {\\k20}a {\\k40}dia{\\k50}mond {\\k40}{\\k30}in {\\k30}the {\\k100}sky"},
        {17000, 20000, "v2", "Twinkle, twinkle, little star"},
        {20000, 27500, "v2", "{\\k60}How {\\k30}I {\\k80}wonder {\\k40}what {\\k40}you {\\k500}are"},
    };
    EXPECT_EQ(rows(*result), expected);
    expectSyllablesFillTheLines(*result);
}

TEST(LyricsImport, LineOnlyLrcBecomesPlainLines)
{
    const auto result = importLrc(fixture("line-only.lrc"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->document.scriptInfo(u8"Title"), u8"Sakura");
    const std::vector<Row> expected{
        {2000, 6000, "", "Sakura, sakura"},
        {6000, 10000, "", "Noyama mo sato mo"},
        {10000, 14000, "", "Miwatasu kagiri"},
        {14000, 18000, "", "Kasumi ka kumo ka"},
        {18000, 22000, "", "Asahi ni niou"},
        {22000, 26000, "", "Noyama mo sato mo"},
        {26000, 31000, "", "Sakura, sakura, hana zakari"},
    };
    EXPECT_EQ(rows(*result), expected);
}

TEST(LyricsImport, AmllTtmlBecomesKaraokeLines)
{
    const auto result = importTtml(fixture("amll.ttml"));
    ASSERT_TRUE(result.has_value()) << int(result.error().kind) << " line " << result.error().line;
    EXPECT_EQ(result->document.scriptInfo(u8"Title"), u8"さくら さくら");
    const auto untitled = importTtml(bytes("<tt xmlns:amll=\"http://www.example.com/ns/amll\"><head><metadata><ttm:title> </ttm:title><amll:meta key=\"musicName\" value=\"Song\"/>"
                                           "</metadata></head><body><p begin=\"1\" end=\"2\">x</p></body></tt>"));
    ASSERT_TRUE(untitled.has_value());
    EXPECT_EQ(untitled->document.scriptInfo(u8"Title"), u8"Song");
    const auto named = importTtml(bytes("<tt xmlns:amll=\"http://www.example.com/ns/amll\"><head><metadata><amll:meta key=\"musicName\" value=\"  My \t  Song \"/>"
                                        "</metadata></head><body><p begin=\"1\" end=\"2\">x</p></body></tt>"));
    ASSERT_TRUE(named.has_value());
    EXPECT_EQ(named->document.scriptInfo(u8"Title"), u8"My Song");
    const auto foreign = importTtml(bytes("<tt><head><metadata><f:title xmlns:f=\"urn:other\">Wrong</f:title>"
                                          "<f:x xmlns:f=\"urn:other\"><ttm:title>Inside</ttm:title></f:x><ttm:title>Right</ttm:title>"
                                          "<f:agent xmlns:f=\"urn:other\" xml:id=\"v1\"><ttm:name>Wrong</ttm:name></f:agent>"
                                          "</metadata></head><body><p begin=\"1\" end=\"2\" ttm:agent=\"v1\">x</p></body></tt>"));
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(foreign->document.scriptInfo(u8"Title"), u8"Right");
    EXPECT_EQ(rows(*foreign), (std::vector<Row>{{1000, 2000, "v1", "x"}}));
    const auto xmlWrapped = importTtml(bytes("<tt><head><metadata><xml:x><ttm:title>Wrong</ttm:title></xml:x>"
                                             "<ttm:title>Right</ttm:title></metadata></head><body><p begin=\"1\" end=\"2\">x</p>"
                                             "</body></tt>"));
    ASSERT_TRUE(xmlWrapped.has_value());
    EXPECT_EQ(xmlWrapped->document.scriptInfo(u8"Title"), u8"Right");
    const auto inside = importTtml(bytes("<tt><head><metadata><ttm:title>Right<f:x xmlns:f=\"urn:other\">Wrong</f:x></ttm:title>"
                                         "<ttm:agent xml:id=\"v1\"><ttm:name>Lead<f:x xmlns:f=\"urn:other\">Wrong</f:x></ttm:name>"
                                         "</ttm:agent></metadata></head><body><p begin=\"1\" end=\"2\" ttm:agent=\"v1\">x</p></body>"
                                         "</tt>"));
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(inside->document.scriptInfo(u8"Title"), u8"Right");
    EXPECT_EQ(rows(*inside), (std::vector<Row>{{1000, 2000, "Lead", "x"}}));
    const std::vector<Row> expected{
        {1000, 4000, "v1", "{\\k50}さ{\\k50}く{\\k60}ら {\\k30}{\\k40}さ{\\k30}く{\\k40}ら"},
        {4000, 8000, "v1", "{\\k40}や{\\k40}よ{\\k50}い{\\k30}の {\\k10}{\\k35}そ{\\k55}ら{\\k90}は{\\k50}"},
        {6800, 7900, "v1", "{\\k50}(はな {\\k60}ざかり)"},
        {9000, 12000, "v2", "Hana zakari"},
    };
    EXPECT_EQ(rows(*result), expected);
    expectSyllablesFillTheLines(*result);
}

TEST(LyricsImport, AppleTtmlBecomesKaraokeLines)
{
    const auto result = importTtml(fixture("apple.ttml"));
    ASSERT_TRUE(result.has_value()) << int(result.error().kind) << " line " << result.error().line;
    EXPECT_EQ(result->document.scriptInfo(u8"Title"), u8"Twinkle, Twinkle, Little Star");
    const std::vector<Row> expected{
        {1200, 4600, "Jane Taylor", "{\\k55}When {\\k25}the {\\k60}blazing {\\k50}sun {\\k30}is {\\k120}gone"},
        {5000, 9500, "Jane Taylor", "{\\k50}When {\\k40}he {\\k110}nothing {\\k60}shines {\\k50}up{\\k140}on"},
    };
    EXPECT_EQ(rows(*result), expected);
    expectSyllablesFillTheLines(*result);
}

TEST(LyricsImport, LineOnlyTtmlBecomesPlainLines)
{
    const auto result = importTtml(fixture("line-only.ttml"));
    ASSERT_TRUE(result.has_value()) << int(result.error().kind) << " line " << result.error().line;
    EXPECT_EQ(result->document.scriptInfo(u8"Title"), u8"HikariSub Ass File");
    const std::vector<Row> expected{
        {2000, 6000, "", "Sakura, sakura"},
        {6000, 10000, "", "Noyama mo sato mo\\Nmiwatasu kagiri"},
        {10000, 14000, "", "Kasumi ka kumo ka & asahi ni niou"},
    };
    EXPECT_EQ(rows(*result), expected);
}

TEST(LyricsImport, MalformedFixtureNamesItsLine)
{
    EXPECT_EQ(importLrc(fixture("malformed.lrc")).error(), (LyricsProblem{Kind::TimeGoesBack, 3, u8"<00:04.20>"}));
}

TEST(LyricsImport, MalformedLrcNamesTheLineAtFault)
{
    EXPECT_EQ(problemOf("[00:01.00]ok\n[00:0x.00]bad\n", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 2, u8"[00:0x.00]"}));
    EXPECT_EQ(problemOf("[00:01.00]<00:0x.00>bad", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 1, u8"<00:0x.00>"}));
    EXPECT_EQ(problemOf("[00:01.00]open <00:02.00", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 1, u8"<00:02.00"}));
    EXPECT_EQ(problemOf("[00:01.00]ok\r\n\r\nno time here\r\n", u8"lrc"), (LyricsProblem{Kind::MissingTime, 3, u8"no time here"}));
    EXPECT_EQ(problemOf("[00:01.00]ok\n[Chorus]\n", u8"lrc"), (LyricsProblem{Kind::MissingTime, 2, u8"[Chorus]"}));
    EXPECT_EQ(problemOf("[00:01.00][00:05.00]<00:01.00>a <00:02.00>b", u8"lrc"),
              (LyricsProblem{Kind::RepeatedWordLine, 1, u8"[00:01.00][00:05.00]<00:01.00>a <00:02.00>b"}));
    EXPECT_EQ(problemOf("[00:01.00]ok\n[00:02.00]ok\n[00:03.00]\xff\n", u8"lrc"), (LyricsProblem{Kind::NotUtf8, 3, u8""}));
    EXPECT_EQ(problemOf("[00:05.00]lead <00:04.00>word", u8"lrc"), (LyricsProblem{Kind::TimeGoesBack, 1, u8"<00:04.00>"}));
    EXPECT_EQ(problemOf("[6000:00.01]x", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 1, u8"[6000:00.01]"}));
    EXPECT_EQ(problemOf("[00:01.00]<999999999:00.00>x", u8"lrc"),
              (LyricsProblem{Kind::UnreadableTime, 1, u8"<999999999:00.00>"}));
    EXPECT_EQ(problemOf("[offset:100oops]\n[00:01.00]x", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 1, u8"[offset:100oops]"}));
    EXPECT_EQ(problemOf("[offset:+]\n[00:01.00]x", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 1, u8"[offset:+]"}));
    EXPECT_EQ(problemOf("[offset: - ]\n[00:01.00]x", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 1, u8"[offset: - ]"}));
    EXPECT_TRUE(importLrc(bytes("[offset:]\n[offset: ]\n[00:01.00]x")).has_value());
    EXPECT_EQ(problemOf("[offset:1234567890]\n[00:01.00]x", u8"lrc"),
              (LyricsProblem{Kind::UnreadableTime, 1, u8"[offset:1234567890]"}));
    EXPECT_EQ(problemOf("[offset:-360000001]\n[00:01.00]x", u8"lrc"),
              (LyricsProblem{Kind::UnreadableTime, 1, u8"[offset:-360000001]"}));
    EXPECT_EQ(problemOf("[ti:Only tags]\n[ar:Nobody]\n", u8"lrc"), (LyricsProblem{Kind::NoLyrics, 0, u8""}));
    EXPECT_EQ(problemOf("", u8"lrc"), (LyricsProblem{Kind::NoLyrics, 0, u8""}));
}

TEST(LyricsImport, MalformedTtmlNamesTheLineAtFault)
{
    const std::string head = "<tt xmlns=\"http://www.w3.org/ns/ttml\">\n<body>\n";
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x</span>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"</span>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x &nbsp; y</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"x &nbsp; y</p>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">ok\nnot &nbsp; y</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 4, u8"not &nbsp; y</p>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\" x:a=\"ok\nnot &nbsp;\">x</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 4, u8"not &nbsp;\">"}));
    EXPECT_EQ(problemOf(head + "<!-- open\n</body></tt>", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"<!-- open"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=2>x</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"end=2>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x</p>\n", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 4, u8""}));
    EXPECT_EQ(problemOf("<html>\n<body/></html>", u8"ttml"), (LyricsProblem{Kind::NotTtml, 1, u8"<html>"}));
    EXPECT_EQ(problemOf("<x:tt xmlns:x=\"urn:other\"><x:body><x:p begin=\"1\" end=\"2\">x</x:p></x:body></x:tt>", u8"ttml"),
              (LyricsProblem{Kind::NotTtml, 1, u8"<x:tt>"}));
    EXPECT_EQ(problemOf("<tt xmlns=\"urn:other\"><body><p begin=\"1\" end=\"2\">x</p></body></tt>", u8"ttml").kind, Kind::NotTtml);
    EXPECT_TRUE(importTtml(bytes("<t:tt xmlns:t=\"http://www.w3.org/ns/ttml\"><t:body><t:p begin=\"1\" end=\"2\">x</t:p>"
                                 "</t:body></t:tt>")).has_value());
    EXPECT_EQ(problemOf("<tt xmlns:x=\"urn:other\"><x:body><p begin=\"1\" end=\"2\">x</p></x:body></tt>", u8"ttml").kind,
              Kind::NoLyrics);
    EXPECT_EQ(problemOf("<tt xmlns:x=\"urn:other\"><body><x:p begin=\"1\" end=\"2\">x</x:p></body></tt>", u8"ttml").kind,
              Kind::NoLyrics);
    EXPECT_EQ(problemOf(head + "<p>\n<span begin=\"00:01:02:03\" end=\"00:01:03.000\">x</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 4, u8"00:01:02:03"}));
    EXPECT_EQ(problemOf(head + "<p>\n<span begin=\"2.0\" end=\"1.5\">x</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::EndBeforeBegin, 4, u8"1.5"}));
    EXPECT_EQ(problemOf(head + "<p><span begin=\"2\" end=\"3\">a</span>\n<span begin=\"1\" end=\"2\">b</span></p></body></tt>",
                        u8"ttml"),
              (LyricsProblem{Kind::TimeGoesBack, 4, u8"1"}));
    EXPECT_EQ(problemOf(head + "<p\nbegin=\"bad\" end=\"2\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 4, u8"bad"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"99:59:59\"\ndur=\"10\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 4, u8"10"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"2\"\nend=\"1\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::EndBeforeBegin, 4, u8"1"}));
    EXPECT_EQ(problemOf(head + "<div end=\"1\"><p\nbegin=\"2\">x</p></div></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 4, u8"2"}));
    EXPECT_EQ(problemOf(head + "<p><span begin=\"2\" end=\"3\">a</span><span\nbegin=\"1\" end=\"2\">b</span></p></body></tt>",
                        u8"ttml"),
              (LyricsProblem{Kind::TimeGoesBack, 4, u8"1"}));
    EXPECT_EQ(problemOf(head + "<p>no time</p></body></tt>", u8"ttml"), (LyricsProblem{Kind::MissingTime, 3, u8"no time"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\"><span begin=\"1\">a</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::MissingTime, 3, u8"a"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">\xc3\x28</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotUtf8, 3, u8""}));
    EXPECT_EQ(problemOf(head + "</body></tt>", u8"ttml"), (LyricsProblem{Kind::NoLyrics, 0, u8""}));
}

TEST(LyricsImport, TtmlMustBeWellFormedXml)
{
    const std::string head = "<tt xmlns=\"http://www.w3.org/ns/ttml\">\n<body>\n";
    const std::string tail = "</p>\n</body></tt>";
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\" begin=\"3\">x" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"<p begin=\"1\" end=\"2\" begin=\"3\">"}));
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" ttm:a=\"1\" end=\"2\" xml:a=\"1\">x" + tail)).has_value());
    EXPECT_EQ(problemOf(head + "<p begin=\"1\"end=\"2\">x" + tail, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"end=\"2\">"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">a ]]> b" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"]]>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"><1x/>" + tail, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"<1x/>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" -end=\"2\">x" + tail, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"-end=\"2\">"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"><$>x</$>" + tail, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"><a$>x</a$>" + tail, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\" $a=\"1\">x" + tail, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<:tt><:body><:p begin=\"1\" end=\"2\">x</:p></:body></:tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 1, u8"<:tt>"}));
    for (const char *bad : {"<a:/>", "<a:b:c/>", "<p:/>", "<x/ :a=\"1\">", "<a:1/>", "<a:-b/>", "<a:.b/>", "<a:\xC2\xB7" "b/>"})
        EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">" + bad + "x" + tail, u8"ttml").kind, Kind::NotWellFormed) << bad;
    for (const char *bad : {" :a=\"1\"", " a:=\"1\"", " a:b:c=\"1\"", " xmlns:a=\"urn:x\" a:1=\"x\"", " xmlns:a=\"urn:x\" a:-x=\"x\""})
        EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"" + bad + ">x" + tail, u8"ttml").kind, Kind::NotWellFormed) << bad;
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" end=\"2\" xmlns:x=\"urn:x\" x:l\xC3\xA9-g\xC2\xB7.1=\"1\"><\xE6\x97\xA5\xE6\x9C\xAC/>x" + tail))
                    .has_value());
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x &#xFFFE; y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"x &#xFFFE; y</p>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x &#1; y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"x &#1; y</p>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x \x01 y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"U+0001"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x \xEF\xBF\xBF y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"U+FFFF"}));
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" end=\"2\">x&#9;y\ty" + tail)).has_value());
    EXPECT_EQ(rows(*importTtml(bytes(head + "<p begin=\"1\" end=\"2\" xml:id=\"&#x0000000041;\">&#00000000065;&#x000000042;" + tail))),
              (std::vector<Row>{{1000, 2000, "", "AB"}}));
    EXPECT_TRUE(importTtml(bytes("<!DOCTYPE tt [<!ENTITY e '&#0000000065;'>]>" + head + "<p begin=\"1\" end=\"2\">x" + tail))
                    .has_value());
    for (const char *bad : {"&#x110000;", "&#x0000000110000;", "&#99999999999999999999;", "&#x;", "&#;"})
        EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">" + bad + tail, u8"ttml").kind, Kind::NotWellFormed) << bad;
    // XML: a comment may not hold "--" or end with "-".
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">a<!-- bad -- comment -->b" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"<!-- bad -- comment -->"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">a<!-- x --->b" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"<!-- x --->"}));
    EXPECT_EQ(problemOf("<!-- a -- b -->\n" + head + "<p begin=\"1\" end=\"2\">a" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 1, u8"<!-- a -- b -->"}));
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" end=\"2\">a<!-- fine - here -->b" + tail)).has_value());
}

TEST(LyricsImport, TtmlNestedTooDeepIsRefused)
{
    auto nested = [](std::size_t divs) {
        std::string s = "<tt>\n<body>\n";
        for (std::size_t i = 0; i < divs; ++i)
            s += "<div>";
        s += "<p begin=\"1\" end=\"2\">x</p>";
        for (std::size_t i = 0; i < divs; ++i)
            s += "</div>";
        return s + "</body></tt>";
    };
    EXPECT_TRUE(importTtml(bytes(nested(kMaxTtmlDepth - 3))).has_value());
    EXPECT_EQ(problemOf(nested(kMaxTtmlDepth - 2), u8"ttml"), (LyricsProblem{Kind::TooDeep, 3, u8"<p begin=\"1\" end=\"2\">"}));
    EXPECT_EQ(problemOf(nested(50000), u8"ttml"), (LyricsProblem{Kind::TooDeep, 3, u8"<div>"}));
}

TEST(LyricsImport, TtmlWhitespaceAndBreaksAcrossElements)
{
    const auto plain = importTtml(bytes("<tt><body><p begin=\"1\" end=\"2\">a <span> b </span> c</p></body></tt>"));
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(rows(*plain), (std::vector<Row>{{1000, 2000, "", "a b c"}}));
    const auto words = importTtml(bytes("<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">a<br/>b</span> "
                                        "<span begin=\"2\" end=\"3\">c <span>d</span></span></p></body></tt>"));
    ASSERT_TRUE(words.has_value());
    EXPECT_EQ(rows(*words), (std::vector<Row>{{1000, 3000, "", "{\\k100}a\\Nb {\\k100}c d"}}));
}

TEST(LyricsImport, RepeatedTextCannotOutgrowTheFile)
{
    std::string lrc;
    for (int i = 0; i < 3000; ++i)
        lrc += "[00:01.00]";
    lrc += std::string(2000, 'x') + "\n";
    EXPECT_EQ(problemOf(lrc, u8"lrc"), (LyricsProblem{Kind::TooLarge, 1, u8""}));
    std::string ttml = "<tt><head><ttm:agent xml:id=\"a\"><ttm:name>" + std::string(100000, 'n') +
                       "</ttm:name></ttm:agent></head><body>\n";
    for (int i = 0; i < 2000; ++i)
        ttml += "<p ttm:agent=\"a\" begin=\"1\" end=\"2\">x</p>\n";
    ttml += "</body></tt>";
    EXPECT_EQ(problemOf(ttml, u8"ttml").kind, Kind::TooLarge);
    const auto voices = importLrc(bytes("[00:01.00]v123456789:a\n[00:02.00]v1234567890:b\n"));
    ASSERT_TRUE(voices.has_value());
    EXPECT_EQ(rows(*voices), (std::vector<Row>{{1000, 2000, "v123456789", "a"}, {2000, 7000, "v123456789", "v1234567890:b"}}));
    EXPECT_TRUE(importLrc(bytes("[00:01.00][00:05.00][00:09.00]chorus\n[00:03.00]verse\n")).has_value());
}

TEST(LyricsImport, FilesAndXmlTreesHaveALimit)
{
    const std::string big = "[00:01.00]" + std::string(kMaxLyricsFileSize, 'x');
    EXPECT_EQ(problemOf(big, u8"lrc"), (LyricsProblem{Kind::TooLarge, 0, u8""}));
    EXPECT_EQ(problemOf("<tt>" + std::string(kMaxLyricsFileSize, ' ') + "</tt>", u8"ttml"),
              (LyricsProblem{Kind::TooLarge, 0, u8""}));
    std::string dense = "<tt><body>\n<p begin=\"1\" end=\"2\">";
    for (int i = 0; i < 100000; ++i)
        dense += "a<x/>";
    dense += "</p></body></tt>";
    EXPECT_EQ(problemOf(dense, u8"ttml"), (LyricsProblem{Kind::TooLarge, 2, u8""}));
    const auto lines = [](int n, std::string_view each) {
        std::string out;
        for (int i = 0; i < n; ++i)
            out += each;
        return out;
    };
    ASSERT_TRUE(importLrc(bytes(lines(10000, "[0:0]x\n"))).has_value());
    EXPECT_EQ(problemOf(lines(10001, "[0:0]x\n"), u8"lrc"), (LyricsProblem{Kind::TooLarge, 10001, u8""}));
    EXPECT_EQ(problemOf(lines(149796, "[0:0]x\n"), u8"lrc"), (LyricsProblem{Kind::TooLarge, 10001, u8""}));
    EXPECT_EQ(problemOf("[0:0]x\n" + lines(10000, "[0:0]\n"), u8"lrc"), (LyricsProblem{Kind::TooLarge, 10001, u8""}));
    EXPECT_EQ(problemOf("[0:0][0:1]x\n" + lines(9999, "<0:0>x\n"), u8"lrc"), (LyricsProblem{Kind::TooLarge, 10000, u8""}));
    const std::string p = "<p begin=\"1\" end=\"2\">x</p>";
    ASSERT_TRUE(importTtml(bytes("<tt><body>" + lines(10000, p) + "</body></tt>")).has_value());
    EXPECT_EQ(problemOf("<tt><body>" + lines(10001, p) + "</body></tt>", u8"ttml").kind, Kind::TooLarge);
}

TEST(LyricsImport, TtmlHeadNamesShareTheBudget)
{
    std::string head;
    for (int i = 0; i < 30; ++i)
        head += "<agent xml:id=\"a" + std::to_string(i) + "\"><name>";
    head += std::string(200000, 'n');
    for (int i = 0; i < 30; ++i)
        head += "</name></agent>";
    EXPECT_EQ(problemOf("<tt><head>" + head + "</head><body><p begin=\"1\" end=\"2\">x</p></body></tt>", u8"ttml").kind,
              Kind::TooLarge);
    const std::string braces = "<!DOCTYPE tt [<!ENTITY a0 '" + std::string(1000, '{') +
                               "'><!ENTITY a1 '&a0;&a0;&a0;&a0;&a0;&a0;&a0;&a0;&a0;&a0;'>"
                               "<!ENTITY a2 '&a1;&a1;&a1;&a1;&a1;&a1;&a1;&a1;&a1;&a1;'>]>";
    EXPECT_EQ(problemOf(braces + "<tt><body><p begin=\"1\" end=\"2\">&a2;&a2;</p></body></tt>", u8"ttml").kind, Kind::TooLarge);
    EXPECT_EQ(problemOf(braces + "<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">&a2;&a2;</span></p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::TooLarge);
    std::string refs;
    for (int i = 0; i < 40; ++i)
        refs += "&e;";
    const std::string named = "<!DOCTYPE tt [<!ENTITY e '" + std::string(20000, 'n') +
                              "'>]><tt xmlns:amll=\"http://www.example.com/ns/amll\"><head><metadata><amll:meta key=\"musicName\" value=\"" + refs +
                              "\"/></metadata></head><body><p begin=\"1\" end=\"2\">";
    EXPECT_EQ(problemOf(named + "&e;&e;&e;</p></body></tt>", u8"ttml").kind, Kind::TooLarge);
    const auto titled = importTtml(bytes(named + "x</p></body></tt>"));
    ASSERT_TRUE(titled.has_value());
    EXPECT_EQ(titled->document.scriptInfo(u8"Title")->size(), 800000u);
    const auto one = importTtml(bytes("<tt><head><ttm:agent xml:id=\"v1\"><ttm:name>" + std::string(200000, 'n') +
                                      "</ttm:name></ttm:agent></head><body><p begin=\"1\" end=\"2\" ttm:agent=\"v1\">x</p></body></tt>"));
    ASSERT_TRUE(one.has_value());
    EXPECT_EQ(rows(*one).front().actor.size(), 200000u);
}

TEST(LyricsImport, TtmlTimesPast100HoursAreRefused)
{
    const std::string head = "<tt><body>\n";
    EXPECT_EQ(problemOf(head + "<p begin=\"101h\" end=\"102h\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 2, u8"101h"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"999999999999999\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 2, u8"999999999999999"}));
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"99h\" end=\"100h\">x</p></body></tt>")).has_value());
    const auto padded = importTtml(bytes("<tt><body><p begin=\"000000000000000000001s\" "
                                         "end=\"00:00:0000000000000000000002.5000000000000000000\">x</p></body></tt>"));
    ASSERT_TRUE(padded.has_value());
    EXPECT_EQ(rows(*padded), (std::vector<Row>{{1000, 2500, "", "x"}}));
    const std::string huge(400, '9');
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"" + huge + "s\">x</p></body></tt>", u8"ttml").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf(head + "<p begin=\"99h\" dur=\"2h\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 2, u8"2h"}));
    const auto last = importLrc(bytes("[99:59:59.99]x\n"));
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(rows(*last), (std::vector<Row>{{359'999'990, 360'000'000, "", "x"}}));
    const auto shifted = importLrc(bytes("[offset:-99999999]\n[99:00:00.00]x\n"));
    ASSERT_TRUE(shifted.has_value());
    EXPECT_EQ(rows(*shifted).front().start, 360'000'000);
    const auto lrcPadded = importLrc(bytes("[offset:+0000000000000500]\n[0000000001:00.00]a <0000000001:000000000001.5>b\n"));
    ASSERT_TRUE(lrcPadded.has_value());
    EXPECT_EQ(rows(*lrcPadded), (std::vector<Row>{{59'500, 66'000, "", "{\\k150}a {\\k500}b"}}));
    EXPECT_EQ(problemOf("[" + huge + ":00.00]x\n", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[00:" + huge + ".00]x\n", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[offset:-" + huge + "]\n[00:01.00]x\n", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[00:0000000060.00]x\n", u8"lrc").kind, Kind::UnreadableTime);
}

TEST(LyricsImport, LyricsTextStaysLiteral)
{
    const std::string wj = "\u2060";
    const auto lrc = importLrc(bytes("[00:01.00]<00:01.00>{a}\\N <00:02.00>b\\\n[00:03.00]{\\i1}x\\n\\h\n"));
    ASSERT_TRUE(lrc.has_value());
    EXPECT_EQ(rows(*lrc), (std::vector<Row>{{1000, 3000, "", "{\\k100}\uFF5Ba\uFF5D\\" + wj + "N {\\k100}b\\" + wj},
                                           {3000, 8000, "", "\uFF5B\\i1\uFF5Dx\\" + wj + "n\\" + wj + "h"}}));
    const auto ttml = importTtml(bytes("<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">{x}</span> "
                                       "<span begin=\"2\" end=\"3\">a\\h</span><br/>c\\</p></body></tt>"));
    ASSERT_TRUE(ttml.has_value());
    EXPECT_EQ(rows(*ttml),
              (std::vector<Row>{{1000, 3000, "", "{\\k100}\uFF5Bx\uFF5D {\\k100}a\\" + wj + "h\\Nc\\" + wj}}));
}

TEST(LyricsImport, LrcLinesLastUntilTheNextLaterTime)
{
    const auto result = importLrc(bytes("[00:01.00][00:04.00]a\n[00:01.00]b\n[00:02.00]c\n"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(rows(*result), (std::vector<Row>{{1000, 2000, "", "a"}, {1000, 2000, "", "b"}, {2000, 4000, "", "c"},
                                               {4000, 9000, "", "a"}}));
}

TEST(LyricsImport, TtmlTimesAndWordEnds)
{
    const auto result = importTtml(bytes("<tt><body><p begin=\"1500ms\" dur=\"2s\">"
                                         "<span begin=\"1.5s\">a</span> <span begin=\"0.04m\" dur=\"0.5s\">b</span>"
                                         "</p></body></tt>"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(rows(*result), (std::vector<Row>{{1500, 3500, "", "{\\k90}a {\\k50}b{\\k60}"}}));
    const std::string p = "<tt><body><p begin=\"";
    for (const char *bad : {"1.", "1.s", ".5s", "1:02.", "0:.5", "."})
        EXPECT_EQ(problemOf(p + bad + "\" end=\"9\">x</p></body></tt>", u8"ttml"),
                  (LyricsProblem{Kind::UnreadableTime, 1, std::u8string(reinterpret_cast<const char8_t *>(bad))}))
            << bad;
    EXPECT_TRUE(importTtml(bytes(p + "0.5s\" dur=\"1.0\">x</p></body></tt>")).has_value());
}

TEST(LyricsImport, ClockMinutesAndSecondsStayUnder60)
{
    EXPECT_EQ(problemOf("[00:99.00]x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[1:60:00.00]x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[00:01.00]<00:61.00>x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_TRUE(importLrc(bytes("[75:02.00]x\n[1:02:03.50]y\n[1:02:04.123]z")).has_value());
    EXPECT_EQ(problemOf("[00:01.1234]x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[00:01:1234]x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[00:01.00]<00:01.5000>x", u8"lrc").kind, Kind::UnreadableTime);
    const std::string ttml = "<tt><body><p begin=\"";
    EXPECT_EQ(problemOf(ttml + "0:99\" end=\"1:40\">x</p></body></tt>", u8"ttml").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf(ttml + "1:60:00\" end=\"2:00:00\">x</p></body></tt>", u8"ttml").kind, Kind::UnreadableTime);
    EXPECT_TRUE(importTtml(bytes(ttml + "1:59.5\" end=\"1:02:03.5\">x</p></body></tt>")).has_value());
}

TEST(LyricsImport, LinesEndAtLfCrlfOrALoneCr)
{
    // Old Mac LRC files end their lines with CR alone.
    const auto lrc = importLrc(bytes("[00:01.00]a\r[00:02.00]b\r\n[00:03.00]c\n"));
    ASSERT_TRUE(lrc.has_value());
    EXPECT_EQ(rows(*lrc).size(), 3u);
    EXPECT_EQ(problemOf("[00:01.00]a\r[00:0x.00]b", u8"lrc").line, 2u);
    EXPECT_EQ(problemOf("<tt>\r<body>\r<p begin=\"x\" end=\"2\">a</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 3, u8"x"}));
    EXPECT_EQ(problemOf("<tt>\r\n<body>\r\n<p begin=\"1\" end=\"2\"><$>a</p></body></tt>", u8"ttml").line, 3u);
    EXPECT_EQ(problemOf("<tt>\r<body>\r<p begin=\"1\" end=\"2\">\x01</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"U+0001"}));
}

TEST(LyricsImport, TtmlPrologIsChecked)
{
    const std::string root = "<tt><body><p begin=\"1\" end=\"2\">x</p></body></tt>";
    const auto imports = [&](const std::string &prolog) { return importTtml(bytes(prolog + root)).has_value(); };
    EXPECT_TRUE(imports("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE tt>\n"));
    EXPECT_TRUE(imports("\xEF\xBB\xBF<?xml version=\"1.0\"?><!-- c --><?app data?><!DOCTYPE tt [<!ENTITY a \"]>\"><!-- ]> -->]>"));
    EXPECT_EQ(problemOf("<!DOCTYPE tt [<!ENTITY a \"]>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_TRUE(imports("<!DOCTYPE tt [ ] >"));
    EXPECT_TRUE(imports("<?xml version='1.1' encoding=\"utf-8\" standalone='no' ?>"));
    EXPECT_TRUE(imports("<?xml version=\"1.0\" encoding=\"Utf-8\"?>"));
    for (const char *other : {"UTF-16", "ISO-8859-1", "Shift_JIS", "utf8"})
        EXPECT_EQ(problemOf("<?xml version=\"1.0\" encoding=\"" + std::string(other) + "\"?>" + root, u8"ttml"),
                  (LyricsProblem{Kind::NotUtf8, 1, u8""}))
            << other;
    EXPECT_EQ(problemOf("<?xml version=\"1.1\"?><tt><body><p begin=\"1\" end=\"2\">&#1;</p></body></tt>", u8"ttml").kind,
              Kind::NotWellFormed);
    for (const char *bad : {"<?xml?>", "<?xml garbage?>", "<?xml version=\"2.0\"?>", "<?xml version=\"1.\"?>",
                            "<?xml encoding=\"UTF-8\" version=\"1.0\"?>", "<?xml version=\"1.0\" standalone=\"maybe\"?>",
                            "<?xml version=\"1.0\" encoding=\"8bit\"?>", "<?xml version=\"1.0\"encoding=\"UTF-8\"?>",
                            "<?xml version=\"1.0\" extra=\"x\"?>", "<?xml version=\"1.0\""})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    EXPECT_TRUE(imports("<!DOCTYPE tt SYSTEM \"ttml.dtd\">"));
    EXPECT_TRUE(imports("<!DOCTYPE tt PUBLIC '-//W3C//DTD TTML//EN' 'ttml.dtd' [ ]>"));
    EXPECT_TRUE(imports("<!DOCTYPE tt[]>"));
    for (const char *bad : {"<!DOCTYPE tt garbage>", "<!DOCTYPE tt SYSTEM>", "<!DOCTYPE tt PUBLIC \"x\">",
                            "<!DOCTYPE tt SYSTEM \"a\" \"b\">", "<!DOCTYPE tt SYSTEM\"a\">", "<!DOCTYPE tt [] SYSTEM \"a\">"})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    EXPECT_EQ(problemOf("<!DOCTYPE tt ]>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE tt []]>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE tt [] x>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE tt [][]>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_TRUE(imports("<!DOCTYPE tt [\n <!ELEMENT tt ANY>\n <!ATTLIST p begin CDATA #IMPLIED>\n"
                        " <!ENTITY % e \"<!-- x -->\"> %e; <!NOTATION n SYSTEM 'n'> <?pi x?> <!-- c -->\n]>"));
    for (const char *bad : {"<!DOCTYPE tt [garbage]>", "<!DOCTYPE tt [<!ELEMENT>]>", "<!DOCTYPE tt [<!ELEMENTtt ANY>]>",
                            "<!DOCTYPE tt [<!ELEMENT tt <x>>]>", "<!DOCTYPE tt [%e]>", "<!DOCTYPE tt [<!FOO x>]>",
                            "<!DOCTYPE tt [<!ENTITY a 'x'>"})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    EXPECT_TRUE(imports("<!DOCTYPE tt [<!ELEMENT tt (head?, body)> <!ELEMENT p (#PCDATA | span | br)*>"
                        " <!ELEMENT x (#PCDATA)> <!ELEMENT y ((a | b)+, c*)> <!ELEMENT z EMPTY>"
                        " <!ATTLIST p begin CDATA #IMPLIED end CDATA #REQUIRED role (a|b) 'a' n NOTATION (x) #FIXED \"x\">"
                        " <!ATTLIST q>"
                        " <!ENTITY e 'v'> <!ENTITY f SYSTEM 'f.xml'> <!ENTITY g PUBLIC 'id' 'g.png' NDATA png>"
                        " <!ENTITY % p PUBLIC 'id' 'p.dtd'> <!NOTATION png PUBLIC 'image/png'> <!NOTATION j SYSTEM 'j'>]>"));
    for (const char *bad : {"<!DOCTYPE tt [<!ELEMENT tt ??" "?>]>", "<!DOCTYPE tt [<!ELEMENT tt (a | b, c)>]>",
                            "<!DOCTYPE tt [<!ELEMENT tt (#PCDATA | a)>]>", "<!DOCTYPE tt [<!ELEMENT tt (a>]>",
                            "<!DOCTYPE tt [<!ELEMENT tt ()>]>", "<!DOCTYPE tt [<!ATTLIST p begin STRING #IMPLIED>]>",
                            "<!DOCTYPE tt [<!ATTLIST p begin CDATA>]>", "<!DOCTYPE tt [<!ATTLIST p a CDATA '<'>]>",
                            "<!DOCTYPE tt [<!ENTITY e>]>", "<!DOCTYPE tt [<!ENTITY e junk>]>",
                            "<!DOCTYPE tt [<!ENTITY % p 'v' NDATA x>]>", "<!DOCTYPE tt [<!NOTATION n>]>",
                            "<!DOCTYPE tt [<!NOTATION n FOO 'x'>]>"})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    EXPECT_TRUE(imports("<!DOCTYPE tt [<!ENTITY e 'a &amp; &#x41; &#65; &g; b'> <!ATTLIST p a CDATA 'a %b &amp; &#9;'>"
                        " <!ATTLIST p refs ENTITIES #IMPLIED ref ENTITY #IMPLIED> <!ENTITY g PUBLIC '-//W3C//x (1) +:=?;!*#@$_%' 'u'>]>"));
    for (const char *bad : {"<!DOCTYPE tt [<!ENTITY e '&broken'>]>", "<!DOCTYPE tt [<!ENTITY e 'a & b'>]>",
                            "<!DOCTYPE tt [<!ENTITY e '%p'>]>", "<!DOCTYPE tt [<!ENTITY e '50%'>]>",
                            // XML: no parameter-entity reference inside the internal subset's declarations.
                            "<!DOCTYPE tt [<!ENTITY e '%p;'>]>",
                            "<!DOCTYPE tt [<!ENTITY e '&#1;'>]>", "<!DOCTYPE tt [<!ENTITY e '&#xZ;'>]>",
                            "<!DOCTYPE tt [<!ENTITY e '&1a;'>]>", "<!DOCTYPE tt [<!ATTLIST p a CDATA '&x'>]>",
                            "<!DOCTYPE tt [<!ATTLIST p a CDATA '&#xFFFE;'>]>", "<!DOCTYPE tt [<!ENTITY g PUBLIC 'a{b' 'u'>]>",
                            "<!DOCTYPE tt [<!NOTATION n PUBLIC 'a\\b'>]>"})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    EXPECT_EQ(problemOf("<!DOCTYPE>" + root, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 1, u8"<!DOCTYPE>"}));
    EXPECT_EQ(problemOf("<!DOCTYPEtt>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE html>\n" + root, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 2, u8"<tt>"}));
    EXPECT_EQ(problemOf("<!DOCTYPE tt>\n<!DOCTYPE tt>\n" + root, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 2, u8"<!DOCTYPE tt>"}));
    EXPECT_EQ(problemOf(root + "\n<!DOCTYPE tt>", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 2, u8"<!DOCTYPE tt>"}));
    EXPECT_EQ(problemOf("<!-- c -->\n<?xml version=\"1.0\"?>" + root, u8"ttml").line, 2u);
    EXPECT_EQ(problemOf(" <?xml version=\"1.0\"?>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<?XML version=\"1.0\"?>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<? x?>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<tt><body><p begin=\"1\" end=\"2\">x<?xml a?></p></body></tt>", u8"ttml").kind,
              Kind::NotWellFormed);
}

TEST(LyricsImport, TtmlParagraphsTakeTheirAncestorsTimes)
{
    const auto timed = [](std::string_view body) {
        const auto result = importTtml(bytes("<tt><body>" + std::string(body) + "</body></tt>"));
        return result ? rows(*result) : std::vector<Row>{};
    };
    EXPECT_EQ(timed("<div begin=\"1\" end=\"3\"><p>hello</p></div>"), (std::vector<Row>{{1000, 3000, "", "hello"}}));
    EXPECT_EQ(timed("<div begin=\"1\" dur=\"1\"><div><p>a</p></div></div>"), (std::vector<Row>{{1000, 2000, "", "a"}}));
    EXPECT_EQ(timed("<div begin=\"1\" end=\"10\"><p dur=\"2\">x</p></div>"), (std::vector<Row>{{1000, 3000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"1\"><div dur=\"3\"><p>y</p></div></div>"), (std::vector<Row>{{1000, 4000, "", "y"}}));
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\">a <f:span xmlns:f=\"urn:other\" begin=\"1\" end=\"2\">x</f:span></p>"),
              (std::vector<Row>{{0, 4000, "", "a"}}));
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\"><span begin=\"0\" end=\"1\">a<f:i xmlns:f=\"urn:o\">x</f:i></span>"
                    "<span begin=\"1\" end=\"4\">b</span></p>"),
              (std::vector<Row>{{0, 4000, "", "{\\k100}a{\\k300}b"}}));
    EXPECT_EQ(problemOf("<tt><body dur=\"bogus\"><p begin=\"1\" end=\"2\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 1, u8"bogus"}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"4\"><span dur=\"1\">a</span></p>"), (std::vector<Row>{{1000, 4000, "", "{\\k100}a{\\k200}"}}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"4\"><span begin=\"1\" end=\"2\">a</span><span end=\"3\">b</span></p>"),
              (std::vector<Row>{{1000, 4000, "", "{\\k100}a{\\k100}b{\\k100}"}}));
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\"><span end=\"3\">hello <span begin=\"2\" end=\"3\">world</span></span></p>"),
              (std::vector<Row>{{0, 4000, "", "{\\k200}hello {\\k100}world{\\k100}"}}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"5\" dur=\"2\">x</p>"), (std::vector<Row>{{1000, 3000, "", "x"}}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"2\" dur=\"5\">x</p>"), (std::vector<Row>{{1000, 2000, "", "x"}}));
    EXPECT_EQ(timed("<p begin=\"99:59:59\" end=\"99:59:59.5\" dur=\"10\">x</p>"),
              (std::vector<Row>{{359'999'000, 359'999'500, "", "x"}}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"9\"><span begin=\"1\" end=\"2\">a</span><span dur=\"2\"><span begin=\"2\">b</span>"
                    "</span></p>"),
              (std::vector<Row>{{1000, 9000, "", "{\\k100}a{\\k100}b{\\k600}"}}));
    EXPECT_EQ(timed("<div begin=\"1\" end=\"3\"><p begin=\"2\">a</p><p end=\"2\">b</p></div>"),
              (std::vector<Row>{{2000, 3000, "", "a"}, {1000, 2000, "", "b"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p begin=\"3\" dur=\"5\">x</p></div>"), (std::vector<Row>{{3000, 4000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p begin=\"3\" end=\"8\">x</p></div>"), (std::vector<Row>{{3000, 4000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><div dur=\"9\"><p>x</p></div></div>"), (std::vector<Row>{{0, 4000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p begin=\"1\" end=\"8\"><span begin=\"1\" end=\"2\">a</span></p></div>"),
              (std::vector<Row>{{1000, 4000, "", "{\\k100}a{\\k200}"}}));
    EXPECT_EQ(timed("<p begin=\"0\" end=\"6\"><span begin=\"0\" end=\"3\">a <span begin=\"1\" dur=\"9\"><span begin=\"1\">b</span>"
                    "</span></span></p>"),
              (std::vector<Row>{{0, 6000, "", "{\\k100}a {\\k200}b{\\k300}"}}));
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\"><span begin=\"3\" end=\"8\">x</span></p>"),
              (std::vector<Row>{{0, 4000, "", "{\\k300}{\\k100}x"}}));
    EXPECT_EQ(timed("<p begin=\"2\" end=\"4\"><span begin=\"1\" end=\"3\">x</span></p>"),
              (std::vector<Row>{{2000, 4000, "", "{\\k100}x{\\k100}"}}));
    EXPECT_EQ(timed("<div begin=\"2\" end=\"4\"><p begin=\"1\" end=\"3\">x</p></div>"), (std::vector<Row>{{2000, 3000, "", "x"}}));
    EXPECT_EQ(problemOf("<tt><body><p begin=\"0\" end=\"4\"><span begin=\"5\" end=\"6\">x</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 1, u8"5"}));
    EXPECT_EQ(problemOf("<tt><body><p begin=\"2\" end=\"4\"><span begin=\"0\" end=\"1\">x</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 1, u8"1"}));
    EXPECT_EQ(problemOf("<tt><body><p begin=\"0\" end=\"4\"><span begin=\"5\" end=\"6\">a<span begin=\"5\">b</span></span>"
                        "</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 1, u8"5"}));
    EXPECT_EQ(problemOf("<tt><body><div begin=\"0\" end=\"2\">\n<div begin=\"3\"><p>x</p></div></div></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 2, u8"3"}));
    // AMLL writes background vocals over their main Line, starting before it or ending after it.
    EXPECT_EQ(timed("<p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"3\">a</span><span ttm:role=\"x-bg\" begin=\"0.5\" "
                    "end=\"3.5\"><span begin=\"0.5\" end=\"3.5\">b</span></span></p>"),
              (std::vector<Row>{{1000, 3000, "", "{\\k200}a"}, {500, 3500, "", "{\\k300}b"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p><span begin=\"1\" end=\"2\">a</span> <span begin=\"2\">b</span></p></div>"),
              (std::vector<Row>{{1000, 4000, "", "{\\k100}a {\\k200}b"}}));
    EXPECT_EQ(problemOf("<tt><body>\n<div begin=\"1\" end=\"3\">\n<p begin=\"5\">x</p></div></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 3, u8"5"}));
    EXPECT_EQ(problemOf("<tt><body><div><p>x</p></div></body></tt>", u8"ttml").kind, Kind::MissingTime);
    // TTML2: a body without a begin starts at the document's beginning.
    EXPECT_EQ(rows(*importTtml(bytes("<tt><body dur=\"2s\"><p>x</p></body></tt>"))), (std::vector<Row>{{0, 2000, "", "x"}}));
    EXPECT_EQ(rows(*importTtml(bytes("<tt><body><div end=\"3\"><p>x</p><p end=\"1\">y</p></div></body></tt>"))),
              (std::vector<Row>{{0, 3000, "", "x"}, {0, 1000, "", "y"}}));
}

TEST(LyricsImport, TtmlTextBeforeTheFirstWordJoinsItAsWritten)
{
    const auto text = [](std::string_view p) {
        const auto result = importTtml(bytes("<tt><body>" + std::string(p) + "</body></tt>"));
        return result && !rows(*result).empty() ? rows(*result).front().text : std::string("(none)");
    };
    EXPECT_EQ(text("<p begin=\"2\" end=\"3\">hello<span begin=\"2\" end=\"3\">world</span></p>"),
              "{\\k100}helloworld");
    EXPECT_EQ(text("<p begin=\"2\" end=\"3\">hello <span begin=\"2\" end=\"3\">world</span></p>"),
              "{\\k100}hello world");
    EXPECT_EQ(text("<p begin=\"2\" end=\"3\">hello<span begin=\"2\" end=\"3\"> world</span></p>"),
              "{\\k100}hello world");
}

TEST(LyricsImport, TtmlEmptyAndNestedTimedSpans)
{
    const auto text = [](std::string_view p) {
        const auto result = importTtml(bytes("<tt><body>" + std::string(p) + "</body></tt>"));
        return result && !rows(*result).empty() ? rows(*result).front().text : std::string("(none)");
    };
    EXPECT_EQ(problemOf("<tt><body><p><span begin=\"1\" end=\"2\"/></p></body></tt>", u8"ttml").kind, Kind::NoLyrics);
    EXPECT_EQ(problemOf("<tt><body><p><span begin=\"1\" end=\"2\">  </span></p></body></tt>", u8"ttml").kind,
              Kind::NoLyrics);
    EXPECT_EQ(text("<p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">a</span><span begin=\"2\" end=\"3\"> </span></p>"),
              "{\\k100}a{\\k100}");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"1\" end=\"2\"><span begin=\"1\">x</span></span></p>"),
              "{\\k100}{\\k100}x{\\k200}");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"1\" end=\"3\">hello <span begin=\"2\" end=\"3\">world</span>"
                   "</span></p>"),
              "{\\k100}{\\k100}hello {\\k100}world{\\k100}");
    EXPECT_EQ(text("<p begin=\"1\" end=\"4\"><span begin=\"1\" end=\"4\">hello <span begin=\"2\" end=\"3\">world</span>"
                   " again</span></p>"),
              "{\\k100}hello {\\k100}world {\\k100}again");
    EXPECT_EQ(text("<p begin=\"0\" end=\"3\"><span begin=\"1\" end=\"3\"><span>hel</span><span begin=\"2\" end=\"3\">lo</span>"
                   "</span></p>"),
              "{\\k100}{\\k100}hel{\\k100}lo");
    EXPECT_EQ(text("<p begin=\"0\" end=\"3\"><span begin=\"1\" end=\"3\">hel<span>lo</span><span begin=\"2\" end=\"3\">world"
                   "</span></span></p>"),
              "{\\k100}{\\k100}hello{\\k100}world");
    EXPECT_EQ(text("<p begin=\"1\" end=\"3\">hel<span begin=\"2\" end=\"3\">lo</span></p>"), "{\\k100}hel{\\k100}lo");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\">hello <span begin=\"2\" end=\"3\">world</span></p>"),
              "{\\k200}hello {\\k100}world{\\k100}");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"0\" end=\"1\">a</span> b <span begin=\"2\" end=\"4\">c</span>"
                   "</p>"),
              "{\\k100}a {\\k100}b {\\k200}c");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"0\" end=\"3\">a</span> b <span begin=\"2\" end=\"4\">c</span>"
                   "</p>"),
              "{\\k200}a {\\k200}b c");
    EXPECT_EQ(text("<p begin=\"0\" end=\"2\"><span begin=\"0\" end=\"2\">a</span> b</p>"), "{\\k200}a b");
    EXPECT_EQ(text("<p begin=\"0\"><span begin=\"0\" end=\"2\">a</span> b</p>"), "{\\k200}a b");
}

TEST(LyricsImport, ActorsKeepAMarkerLikeStart)
{
    const auto result = importTtml(bytes("<tt><head><metadata><ttm:agent xml:id=\"v1\"><ttm:name>[hidden]Singer</ttm:name>"
                                         "</ttm:agent></metadata></head><body><p begin=\"1\" end=\"2\" ttm:agent=\"v1\">a</p>"
                                         "<p begin=\"2\" end=\"3\" ttm:agent=\"[bookmark]v2\">b</p></body></tt>"));
    ASSERT_TRUE(result.has_value());
    const auto lines = result->document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(str(lines[0]->actor), "⁠[hidden]Singer");
    EXPECT_EQ(lines[0]->visibility, LineVisibility::Visible);
    EXPECT_EQ(str(lines[1]->actor), "⁠[bookmark]v2");
    EXPECT_FALSE(lines[1]->bookmark);
}

TEST(LyricsImport, TtmlEntitiesTheFileDeclaresAreRead)
{
    const auto read = [](std::string_view subset, std::string_view body) {
        const auto result = importTtml(bytes("<!DOCTYPE tt [" + std::string(subset) + "]><tt><body>" + std::string(body) + "</body></tt>"));
        return result ? rows(*result) : std::vector<Row>{};
    };
    EXPECT_EQ(read("<!ENTITY lyric 'hello'>", "<p begin=\"1\" end=\"2\">&lyric;</p>"), (std::vector<Row>{{1000, 2000, "", "hello"}}));
    EXPECT_EQ(read("<!ENTITY lo 'lo'>", "<p begin=\"1\" end=\"2\">hel&lo; you</p>"), (std::vector<Row>{{1000, 2000, "", "hello you"}}));
    EXPECT_EQ(read("<!ENTITY w '<span begin=\"1\" end=\"2\">a</span>'>",
                   "<p begin=\"1\" end=\"3\">&w;<span begin=\"2\" end=\"3\">b</span></p>"),
              (std::vector<Row>{{1000, 3000, "", "{\\k100}a{\\k100}b"}}));
    EXPECT_EQ(read("<!ENTITY a 'x&b;'><!ENTITY b 'y&#38;amp;'>", "<p begin=\"1\" end=\"2\">&a;</p>"),
              (std::vector<Row>{{1000, 2000, "", "xy&"}}));
    EXPECT_EQ(read("<!ENTITY nbsp '&#160;'>", "<p begin=\"1\" end=\"2\">a&nbsp;b</p>"),
              (std::vector<Row>{{1000, 2000, "", "a\u00A0b"}}));
    EXPECT_EQ(read("<!ENTITY e 'a'><!ENTITY e 'b'>", "<p begin=\"1\" end=\"2\">&e;</p>"), (std::vector<Row>{{1000, 2000, "", "a"}}));
    EXPECT_EQ(read("<!ENTITY t '1.5s'><!ENTITY u '&t;'>", "<p begin=\"&u;\" end=\"2s\">x</p>"),
              (std::vector<Row>{{1500, 2000, "", "x"}}));
    EXPECT_EQ(read("<!ATTLIST p begin CDATA '1s' end CDATA #FIXED '2s'>", "<p>x</p><p begin=\"3s\" end=\"4s\">y</p>"),
              (std::vector<Row>{{1000, 2000, "", "x"}, {3000, 4000, "", "y"}}));
    EXPECT_EQ(read("<!ATTLIST p xml:space (default|preserve) 'preserve'>", "<p begin=\"1\" end=\"2\">a  b</p>"),
              (std::vector<Row>{{1000, 2000, "", "a  b"}}));
    EXPECT_EQ(problemOf("<!DOCTYPE tt [<!ATTLIST tt xmlns CDATA #FIXED 'urn:other'>]><tt><body/></tt>", u8"ttml").kind,
              Kind::NotTtml);
    EXPECT_EQ(problemOf("<!DOCTYPE tt [<!ATTLIST p a:x CDATA 'default'>]><tt><body><p begin=\"1\" end=\"2\" "
                        "xmlns:a=\"urn:x\" xmlns:b=\"urn:x\" b:x=\"given\">x</p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::NotWellFormed);
    EXPECT_EQ(read("<!ATTLIST p xml:space NMTOKEN #IMPLIED>", "<p begin=\"1\" end=\"2\" xml:space=\" preserve  \">a  b</p>"),
              (std::vector<Row>{{1000, 2000, "", "a  b"}}));
    EXPECT_EQ(read("<!ENTITY % defs '<!ENTITY lyric \"hello\">'> %defs;", "<p begin=\"1\" end=\"2\">&lyric;</p>"),
              (std::vector<Row>{{1000, 2000, "", "hello"}}));
    EXPECT_EQ(read("<!ENTITY % times '<!ATTLIST p begin CDATA \"1s\" end CDATA \"2s\">'>%times;", "<p>x</p>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    EXPECT_EQ(read("<!ENTITY % inner '<!ENTITY e \"a\">'><!ENTITY % outer '&#37;inner; <!-- c -->'>%outer;",
                   "<p begin=\"1\" end=\"2\">&e;</p>"),
              (std::vector<Row>{{1000, 2000, "", "a"}}));
    EXPECT_TRUE(read("<!ENTITY % ext SYSTEM 'x.dtd'>%ext;<!ENTITY e 'a'>", "<p begin=\"1\" end=\"2\">&e;</p>").empty());
    EXPECT_TRUE(read("%p;<!ENTITY e 'a'>", "<p begin=\"1\" end=\"2\">&e;</p>").empty());
    const auto standalone = importTtml(bytes("<?xml version=\"1.0\" standalone=\"yes\"?>"
                                             "<!DOCTYPE tt [<!ENTITY % p SYSTEM 'p.dtd'>%p;<!ENTITY e 'a'>]>"
                                             "<tt><body><p begin=\"1\" end=\"2\">&e;</p></body></tt>"));
    ASSERT_TRUE(standalone.has_value());
    EXPECT_EQ(rows(*standalone), (std::vector<Row>{{1000, 2000, "", "a"}}));
    EXPECT_EQ(problemOf("<?xml version=\"1.0\" standalone=\"yes\"?><!DOCTYPE tt [%missing;<!ENTITY e 'a'>]>"
                        "<tt><body><p begin=\"1\" end=\"2\">&e;</p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::NotWellFormed);
    // XML normalizes a line end to LF in an entity value and to a space in an attribute value.
    EXPECT_EQ(read("<!ENTITY singer 'Lead\r\nVoice'><!ENTITY two 'Lead&#13;\rVoice'>",
                   "<p begin=\"1\" end=\"2\" ttm:agent=\"&singer;\">x</p><p begin=\"3\" end=\"4\" ttm:agent=\"&two;\">y</p>"),
              (std::vector<Row>{{1000, 2000, "Lead Voice", "x"}, {3000, 4000, "Lead  Voice", "y"}}));
    EXPECT_EQ(read("<!ENTITY e 'a\r\nb'>", "<p begin=\"1\" end=\"2\" xml:space=\"preserve\">&e;</p>"),
              (std::vector<Row>{{1000, 2000, "", "a\\Nb"}}));

    const auto problem = [](std::string_view subset, std::string_view body) {
        return problemOf("<!DOCTYPE tt [" + std::string(subset) + "]>\n<tt><body>\n" + std::string(body) + "</body></tt>", u8"ttml");
    };
    const std::string p = "<p begin=\"1\" end=\"2\">x &e;</p>";
    EXPECT_EQ(problem("<!ENTITY e '&f;'><!ENTITY f '&e;'>", p), (LyricsProblem{Kind::NotWellFormed, 1, u8"<!DOCTYPE tt [<!ENTITY e '&f;'>"}));
    for (const char *subset : {"<!ENTITY e SYSTEM 'e.xml'>", "<!ENTITY e '<span>'>", "<!ENTITY e '</p><p>'>", "<!ENTITY e '<?xml version=\"1.0\"?>'>"})
        EXPECT_EQ(problem(subset, p).kind, Kind::NotWellFormed) << subset;
    EXPECT_EQ(problem("<!ENTITY e '<span>'>", "<p begin=\"1\" end=\"2\">\n&e;</span></p>"), (LyricsProblem{Kind::NotWellFormed, 4, u8"&e;</span>"}));
    EXPECT_EQ(problem("<!ENTITY e '<'>", "<p begin=\"&e;\" end=\"2\">x</p>").kind, Kind::NotWellFormed);
    EXPECT_EQ(problem("<!ENTITY e '&e;'>", "<p begin=\"&e;\" end=\"2\">x</p>").kind, Kind::NotWellFormed);
    EXPECT_EQ(problem("<!ATTLIST p begin CDATA '&e;'>", "<p end=\"2\">x</p>").kind, Kind::NotWellFormed);
    for (const char *subset : {"<!ENTITY % r '&#37;r;'>%r;", "<!ENTITY % half \"<!ENTITY e \">%half; 'a'>",
                               "<!ENTITY % close ']>'>%close;", "<!ENTITY % pi '<?xml version=\"1.0\"?>'>%pi;"})
        EXPECT_EQ(problem(subset, p).kind, Kind::NotWellFormed) << subset;
    const std::string unused = "<p begin=\"1\" end=\"2\">x</p>";
    for (const char *subset : {"<!ENTITY loop '&loop;'>", "<!ENTITY loop '&#38; &loop;'>", "<!ENTITY a 'x&b;'><!ENTITY b '&c;y'><!ENTITY c '<s>&a;</s>'>",
                               "<!ENTITY % loop '&#37;loop;'>", "<!ENTITY % a '&#37;b;'><!ENTITY % b '&#37;a;'>"})
        EXPECT_EQ(problem(subset, unused).kind, Kind::NotWellFormed) << subset;
    for (const char *subset : {"<!ENTITY a '&missing;'>", "<!ENTITY a 'x'><!ENTITY b '&#38;missing;'>"})
        EXPECT_EQ(problem(subset, unused).kind, Kind::NotWellFormed) << subset;
    EXPECT_EQ(problemOf("<?xml version=\"1.0\" standalone=\"yes\"?><!DOCTYPE tt SYSTEM 'tt.dtd' [<!ENTITY a '&missing;'>]>"
                        "<tt><body><p begin=\"1\" end=\"2\">x</p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<?xml version=\"1.0\" standalone=\"yes\"?><!DOCTYPE tt [<!ENTITY % a '&#37;missing;'>]>"
                        "<tt><body><p begin=\"1\" end=\"2\">x</p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::NotWellFormed);
    for (const char *doctype : {"<!DOCTYPE tt SYSTEM 'tt.dtd' [<!ENTITY a '&missing;'>]>",
                                "<!DOCTYPE tt [<!ENTITY % p SYSTEM 'p.dtd'>%p;<!ENTITY a '&missing;'>]>",
                                "<!DOCTYPE tt [<!ENTITY % p ''>%p;<!ENTITY a '&missing;'>]>",
                                "<!DOCTYPE tt [<!ENTITY a '&b; &lt;&gt;&amp;&quot;&apos; &#38;#60;'><!ENTITY b 'x'>]>",
                                "<!DOCTYPE tt [<!ENTITY % a '&#37;missing;'>]>"})
        EXPECT_TRUE(importTtml(bytes(std::string(doctype) + "<tt><body><p begin=\"1\" end=\"2\">x</p></body></tt>")).has_value())
            << doctype;
    EXPECT_EQ(read("<!ENTITY a '&b;&c;'><!ENTITY b '&c;'><!ENTITY c 'x'><!ENTITY % a '&#37;b;&#37;b;'><!ENTITY % b ''>",
                   "<p begin=\"1\" end=\"2\">&a;</p>"),
              (std::vector<Row>{{1000, 2000, "", "xx"}}));
    // XML lets the five predefined entities be declared only as themselves.
    for (const char *subset : {"<!ENTITY amp 'wrong'>", "<!ENTITY lt '<'>", "<!ENTITY amp '&#38;'>", "<!ENTITY gt '&#60;'>",
                               "<!ENTITY quot SYSTEM 'q.xml'>", "<!ENTITY apos ''>"})
        EXPECT_EQ(problem(subset, unused).kind, Kind::NotWellFormed) << subset;
    EXPECT_EQ(read("<!ENTITY lt '&#38;#60;'><!ENTITY amp '&#38;#x26;'><!ENTITY gt '>'><!ENTITY quot '&#34;'><!ENTITY apos \"'\">",
                   "<p begin=\"1\" end=\"2\">&lt;&amp;&gt;&quot;&apos;</p>"),
              (std::vector<Row>{{1000, 2000, "", "<&>\"'"}}));
    // Namespaces in XML: entity, notation and PI target names are NCNames.
    for (const char *subset : {"<!ATTLIST p a:b:c CDATA 'x'>", "<!ATTLIST p a:1 CDATA 'x'>", "<!ELEMENT a:1 ANY>", "<!ATTLIST a:b:c x CDATA 'x'>", "<!ATTLIST p :x CDATA 'x'>",
                               "<!ELEMENT a:b:c ANY>", "<!ELEMENT p (a:b:c)*>", "<!ENTITY a:b 'x'>", "<!ENTITY % a:b 'x'>",
                               "<!NOTATION a:b SYSTEM 'x'>", "<?a:b x?>", "<!ENTITY e SYSTEM 'e' NDATA a:b>"})
        EXPECT_EQ(problem(subset, unused).kind, Kind::NotWellFormed) << subset;
    EXPECT_EQ(read("<!ELEMENT tt:p (a:b)*><!ATTLIST q a:b CDATA 'x'><!NOTATION n SYSTEM 'n'>", unused),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    std::string laughs = "<!ENTITY a0 '0123456789'>";
    for (int i = 1; i <= 9; ++i) {
        laughs += "<!ENTITY a" + std::to_string(i) + " '";
        for (int j = 0; j < 10; ++j)
            laughs += "&a" + std::to_string(i - 1) + ";";
        laughs += "'>";
    }
    EXPECT_EQ(problem(laughs, "<p begin=\"1\" end=\"2\">&a9;</p>").kind, Kind::TooLarge);
    EXPECT_EQ(problem(laughs, "<p begin=\"1\" end=\"2\" a=\"&a9;\">x</p>").kind, Kind::TooLarge);
    EXPECT_EQ(problem(laughs + "<!ATTLIST p a CDATA '&a9;'>", "<p begin=\"1\" end=\"2\">x</p>").kind, Kind::TooLarge);
    std::string parameters = "<!ENTITY % p0 '<!-- 0123456789 -->'>";
    for (int i = 1; i <= 9; ++i) {
        parameters += "<!ENTITY % p" + std::to_string(i) + " '";
        for (int j = 0; j < 10; ++j)
            parameters += "&#37;p" + std::to_string(i - 1) + ";";
        parameters += "'>";
    }
    EXPECT_EQ(problem(parameters + "%p9;", "<p begin=\"1\" end=\"2\">x</p>").kind, Kind::TooLarge);
    std::string defaults = "<!ATTLIST br";
    for (int i = 0; i < 100; ++i)
        defaults += " n" + std::to_string(i) + std::string(8000, 'a') + " CDATA ''";
    defaults += ">";
    std::string breaks = "<p begin=\"1\" end=\"2\">x";
    for (int i = 0; i < 30000; ++i)
        breaks += "<br/>";
    EXPECT_EQ(problem(defaults, breaks + "</p>").kind, Kind::TooLarge);
    std::string list = "<!ENTITY % l '<!ATTLIST p";
    for (int i = 0; i < 20000; ++i)
        list += " a" + std::to_string(i) + " CDATA #IMPLIED";
    list += ">'>";
    for (int i = 0; i < 17; ++i)
        list += "%l;";
    const auto replayed = std::chrono::steady_clock::now();
    EXPECT_EQ(read(list, "<p begin=\"1\" end=\"2\">x</p>"), (std::vector<Row>{{1000, 2000, "", "x"}}));
    EXPECT_LT(std::chrono::steady_clock::now() - replayed, std::chrono::seconds(5));
    std::string many = "<!ATTLIST br";
    for (int i = 0; i < 20000; ++i)
        many += " a" + std::to_string(i) + " CDATA #IMPLIED";
    EXPECT_EQ(problem(many + ">", breaks + "</p>").kind, Kind::TooLarge);
}

TEST(LyricsImport, TtmlXmlSpacePreserveKeepsWhitespace)
{
    const auto text = [](std::string_view body) {
        const auto result = importTtml(bytes("<tt><body" + std::string(body) + "</body></tt>"));
        return result && !rows(*result).empty() ? rows(*result).front().text : std::string("(none)");
    };
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\">a  \t b</p>"), "a b");
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" xml:space=\"preserve\">a  \t b</p>"), "a    b");
    EXPECT_EQ(text(" xml:space=\"preserve\"><div><p begin=\"1\" end=\"2\">a  b\nc</p></div>"), "a  b\\Nc");
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" xml:space=\"preserve\">a  <span xml:space=\"default\">b   c</span></p>"),
              "a  b c");
    EXPECT_EQ(text("><p begin=\"1\" end=\"3\" xml:space=\"preserve\"><span begin=\"1\" end=\"2\">a</span>  "
                   "<span begin=\"2\" end=\"3\">b</span></p>"),
              "{\\k100}a  {\\k100}b");
    // ASS trims a Line's ends, so kept spaces there are hard spaces.
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" xml:space=\"preserve\">  a  </p>"), "\\h\\ha\\h\\h");
    EXPECT_EQ(text("><p begin=\"1\" end=\"3\" xml:space=\"preserve\">  <span begin=\"1\" end=\"2\">a</span> "
                   "<span begin=\"2\" end=\"3\">b</span>  </p>"),
              "{\\k100}\\h\\ha {\\k100}b\\h\\h");
    const auto blank = importTtml(bytes("<tt><body><p begin=\"1\" end=\"2\" xml:space=\"preserve\">   </p>"
                                        "<p begin=\"2\" end=\"3\">x</p></body></tt>"));
    ASSERT_TRUE(blank.has_value());
    EXPECT_EQ(rows(*blank).size(), 1u);
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\"><span xml:space=\"preserve\">  a  </span></p>"), "\\h\\ha\\h\\h");
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\"> x <span xml:space=\"preserve\">  </span> y </p>"), "x    y");
    EXPECT_EQ(text("><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\" xml:space=\"preserve\"> a</span> "
                   "<span begin=\"2\" end=\"3\">b</span></p>"),
              "{\\k100}\\ha {\\k100}b");
    EXPECT_EQ(text("><p><span begin=\"1\" end=\"2\"><span xml:space=\"preserve\">  a</span></span></p>"),
              "{\\k100}\\h\\ha");
    EXPECT_EQ(text("><p><span begin=\"1\" end=\"2\">a</span><span begin=\"2\" end=\"3\"> <span "
                   "xml:space=\"preserve\">  b</span></span></p>"),
              "{\\k100}a   {\\k100}b");
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" xmlns:x=\"urn:x\" x:space=\"preserve\">a  b</p>"), "a b");
}

TEST(LyricsImport, TtmlAttributesAreReadByNamespace)
{
    const auto lines = [](std::string_view tt) {
        const auto result = importTtml(bytes(std::string(tt)));
        return result ? rows(*result) : std::vector<Row>{};
    };
    // Real TTML files leave ttm: undeclared.
    EXPECT_EQ(problemOf("<tt><body><p begin=\"1\" end=\"2\" foo:a=\"x\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 1, u8"<p begin=\"1\" end=\"2\" foo:a=\"x\">"}));
    EXPECT_EQ(problemOf("<tt><body><p begin=\"1\" end=\"2\">x<foo:b/></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 1, u8"<foo:b/>"}));
    EXPECT_EQ(lines("<tt><body><p begin=\"1\" end=\"2\">a<span ttm:role=\"x-roman\">b</span></p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "a"}}));
    EXPECT_EQ(lines("<tt xmlns:foo=\"urn:x\"><body><p foo:begin=\"10\" begin=\"1\" end=\"2\">x</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    EXPECT_EQ(lines("<tt xmlns:foo=\"urn:x\"><body><p begin=\"1\" foo:end=\"0\" end=\"2\">x</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    // Namespaces in XML: one attribute twice under two prefixes for its namespace is malformed.
    EXPECT_EQ(problemOf("<tt xmlns:a=\"http://www.w3.org/ns/ttml#metadata\" xmlns:b=\"http://www.w3.org/ns/ttml#metadata\">"
                        "<body><p begin=\"1\" end=\"2\"><span a:role=\"x-translation\" b:role=\"x-bg\">x</span></p></body></tt>",
                        u8"ttml").kind,
              Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<tt xmlns:a=\"urn:x\" xmlns:b=\"urn:x\"><body><p begin=\"1\" end=\"2\" a:k=\"1\" b:k=\"2\">x</p>"
                        "</body></tt>", u8"ttml").kind,
              Kind::NotWellFormed);
    EXPECT_EQ(lines("<tt xmlns:a=\"urn:x\" xmlns:b=\"urn:y\"><body><p begin=\"1\" end=\"2\" a:k=\"1\" b:k=\"2\">x</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    EXPECT_EQ(lines("<tt xmlns:m=\"http://www.w3.org/ns/ttml#metadata\"><body><p begin=\"1\" end=\"2\">a"
                    "<span m:role=\"x-translation\">b</span></p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "a"}}));
    EXPECT_EQ(lines("<tt xmlns:foo=\"urn:x\"><body><p begin=\"1\" end=\"2\">a <span foo:role=\"x-translation\">b</span>"
                    "</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "a b"}}));
    // Namespaces in XML reserve the xml and xmlns prefixes and namespaces.
    for (const char *bad : {" xmlns:xml=\"urn:other\"", " xmlns:xmlns=\"http://www.w3.org/2000/xmlns/\"",
                            " xmlns:xmlns=\"urn:x\"", " xmlns:x=\"http://www.w3.org/XML/1998/namespace\"",
                            " xmlns=\"http://www.w3.org/XML/1998/namespace\"", " xmlns:x=\"http://www.w3.org/2000/xmlns/\"",
                            " xmlns=\"http://www.w3.org/2000/xmlns/\"", " xmlns:x=\"\""}) {
        EXPECT_EQ(problemOf("<tt><body><p begin=\"1\" end=\"2\"" + std::string(bad) + ">x</p></body></tt>", u8"ttml").kind,
                  Kind::NotWellFormed)
            << bad;
        EXPECT_EQ(problemOf("<tt><body>\n<p begin=\"1\" end=\"2\"" + std::string(bad) + " xml:id=\"a\">x</p></body></tt>",
                            u8"ttml")
                      .line,
                  2u)
            << bad;
    }
    EXPECT_EQ(problemOf("<tt><body><p begin=\"1\" end=\"2\">x<xmlns:a/></p></body></tt>", u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(lines("<tt xmlns:xml=\"http://www.w3.org/XML/1998/namespace\" xmlns=\"\"><body><p begin=\"1\" end=\"2\">x</p>"
                    "</body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    std::string many = "<tt xmlns:n=\"urn:" + std::string(100'000, 'x') + "\"><body><p begin=\"1\" end=\"2\"";
    for (int i = 0; i < 8'000; ++i)
        many += " n:a" + std::to_string(i) + "=\"\"";
    EXPECT_EQ(lines(many + ">x</p></body></tt>"), (std::vector<Row>{{1000, 2000, "", "x"}}));
    std::string declared = "<tt";
    for (int i = 0; i < 2'000; ++i)
        declared += " xmlns:n" + std::to_string(i) + "=\"urn:" + std::to_string(i) + "\"";
    declared += "><body><p begin=\"1\" end=\"2\"";
    for (int i = 0; i < 30'000; ++i)
        declared += " n" + std::to_string(i % 2'000) + ":a" + std::to_string(i) + "=\"\"";
    EXPECT_EQ(lines(declared + ">x</p></body></tt>"), (std::vector<Row>{{1000, 2000, "", "x"}}));
}

TEST(LyricsImport, TtmlBackgroundVocalsNameTheirOwnAgent)
{
    const std::string head = "<tt><head><metadata>"
                             "<ttm:agent xml:id=\"v1\"><ttm:name>Lead</ttm:name></ttm:agent>"
                             "<ttm:agent xml:id=\"v2\"><ttm:name>Choir</ttm:name></ttm:agent>"
                             "</metadata></head><body>";
    const auto actors = [&](std::string_view background) {
        const auto result = importTtml(bytes(head + "<p begin=\"1\" end=\"3\" ttm:agent=\"v1\">"
                                             "<span begin=\"1\" end=\"2\">a</span><span ttm:role=\"x-bg\"" +
                                             std::string(background) + "><span begin=\"2\" end=\"3\">b</span></span>"
                                             "</p></body></tt>"));
        std::vector<std::string> out;
        if (result)
            for (const Row &row : rows(*result))
                out.push_back(row.actor);
        return out;
    };
    EXPECT_EQ(actors(" ttm:agent=\"v2\""), (std::vector<std::string>{"Lead", "Choir"}));
    EXPECT_EQ(actors(""), (std::vector<std::string>{"Lead", "Lead"}));
    const auto nested = importTtml(bytes("<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"3\">lead"
                                         "<span ttm:role=\"x-bg\"><span begin=\"2\" end=\"3\">back</span></span></span>"
                                         "</p></body></tt>"));
    ASSERT_TRUE(nested.has_value());
    ASSERT_EQ(rows(*nested).size(), 2u);
    EXPECT_EQ(rows(*nested)[1].text, "{\\k100}back");
    const auto timedBy = [](std::string_view p) {
        const auto result = importTtml(bytes("<tt><body>" + std::string(p) + "</body></tt>"));
        return result && rows(*result).size() == 2 ? rows(*result)[1] : Row{-1, -1, "", ""};
    };
    EXPECT_EQ(timedBy("<p begin=\"0\" end=\"4\"><span begin=\"1\" end=\"2\">lead<span ttm:role=\"x-bg\">back</span></span></p>"),
              (Row{1000, 2000, "", "back"}));
    EXPECT_EQ(timedBy("<p begin=\"0\" end=\"4\"><span begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">lead</span>"
                      "<span><span ttm:role=\"x-bg\">back</span></span></span></p>"),
              (Row{1000, 3000, "", "back"}));
    EXPECT_EQ(timedBy("<p begin=\"0\" end=\"4\"><span begin=\"1\" end=\"2\">lead</span><span ttm:role=\"x-bg\">back</span></p>"),
              (Row{0, 4000, "", "back"}));
}

TEST(LyricsImport, LrcAngleAndSquareBracketsThatAreNotStamps)
{
    // lyrsync writes a lowercase key tag such as [class:...] after a line's time.
    {
        const auto result = importLrc(bytes("[ti:Song][00:01.00][Verse: 1]Hello\n[00:03.00][class:shhh]Bye\n"
                                            "[00:05.00][Part:A]x\n[00:07.00]"));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(rows(*result), (std::vector<Row>{{1000, 3000, "", "[Verse: 1]Hello"}, {3000, 5000, "", "Bye"},
                                                   {5000, 7000, "", "[Part:A]x"}}));
    }
    for (const char *line : {"[00:01.00]<00:01.00>a<00:02.00>   b<00:03.00>", "[00:01.00]<00:01.00>a   <00:02.00>b<00:03.00>",
                             "[00:01.00]<00:01.00>a <00:02.00>  b<00:03.00>"}) {
        const auto result = importLrc(bytes(line));
        ASSERT_TRUE(result.has_value()) << line;
        EXPECT_EQ(rows(*result).front().text, "{\\k100}a   {\\k100}b") << line;
    }
    {
        const auto result = importLrc(bytes("[00:01.00]Meet me <12: tonight>\n[00:03.00]"));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(rows(*result), (std::vector<Row>{{1000, 3000, "", "Meet me <12: tonight>"}}));
    }
    const auto plain = importLrc(bytes("[00:01.00]I <3 you, <12 roses>\n"));
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(rows(*plain).front().text, "I <3 you, <12 roses>");
    const auto words = importLrc(bytes("[00:01.00]<00:01.00>I <00:02.00><3\n"));
    ASSERT_TRUE(words.has_value());
    EXPECT_EQ(rows(*words).front().text, "{\\k100}I {\\k500}<3");
    EXPECT_EQ(problemOf("[00:01.00]ok\n[1st verse]\n", u8"lrc"), (LyricsProblem{Kind::MissingTime, 2, u8"[1st verse]"}));
}

TEST(LyricsImport, LrcOffsets)
{
    const auto none = importLrc(bytes("[offset:]\n[00:01.00]x\n"));
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ(rows(*none).front().start, 1000);
    const auto sooner = importLrc(bytes("[offset: +250 ]\n[00:01.00]x\n"));
    ASSERT_TRUE(sooner.has_value());
    EXPECT_EQ(rows(*sooner).front().start, 750);
}

TEST(LyricsImport, LrcTimeForms)
{
    const auto result = importLrc(bytes("[00:01]a\n[00:02.5]b\n[00:03:25]c\n[0:00:04.75]d\n"));
    ASSERT_TRUE(result.has_value());
    const auto r = rows(*result);
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0].start, 1000);
    EXPECT_EQ(r[1].start, 2500);
    EXPECT_EQ(r[2].start, 3250);
    EXPECT_EQ(r[3].start, 4750);
}
