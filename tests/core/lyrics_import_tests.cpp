// KA6: word-timed LRC and TTML lyrics imported as karaoke Lines, from the
// sample files in tests/fixtures/lyrics (see the README there).

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
    std::int64_t start = 0; // milliseconds
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

// Every karaoke Line's syllables add up to its duration.
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

} // namespace

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
    // A title's whitespace runs become one space, as a TTML title's do.
    const auto spaced = importLrc(bytes("[ti:  My \t  Song ]\n[00:01.00]x\n"));
    ASSERT_TRUE(spaced.has_value());
    EXPECT_EQ(spaced->document.scriptInfo(u8"Title"), u8"My Song");
    // An empty title keeps the one before it; a later one with words wins.
    const auto kept = importLrc(bytes("[ti:Song]\n[ti: ]\n[ti:\t]\n[00:01.00]x\n"));
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(kept->document.scriptInfo(u8"Title"), u8"Song");
    const auto later = importLrc(bytes("[ti:]\n[ti:First]\n[ti:Second]\n[00:01.00]x\n"));
    ASSERT_TRUE(later.has_value());
    EXPECT_EQ(later->document.scriptInfo(u8"Title"), u8"Second");
    const std::vector<Row> expected{
        // The first stamp comes 20 cs after the line time: a pause first.
        {1000, 4000, "", "{\\k20}{\\k70}Twinkle, {\\k70}twinkle, {\\k50}little {\\k90}star"},
        // No stamp on the first word: it starts at the line time. No end
        // stamp: the last word lasts until the next line.
        {4500, 8000, "", "{\\k60}How {\\k30}I {\\k80}wonder {\\k40}what {\\k40}you {\\k100}are"},
        // A voice marker is the Line's actor; "a" and "bove" are one word.
        {8000, 11200, "v1", "{\\k40}Up {\\k20}a{\\k40}bove {\\k30}the {\\k60}world {\\k50}so {\\k80}high"},
        // Back-to-back stamps: a pause between "mond" and "in".
        {11500, 15000, "v2", "{\\k40}Like {\\k20}a {\\k40}dia{\\k50}mond {\\k40}{\\k30}in {\\k30}the {\\k100}sky"},
        // A plain line in a word-timed file stays plain. A voice marker
        // holds until another one.
        {17000, 20000, "v2", "Twinkle, twinkle, little star"},
        // The last word of the file: the 5 s of a new Line.
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
    // [offset:+500] shows every line half a second sooner; the line with two
    // times is there twice, in time order; each line lasts until the next.
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
    // An empty title leaves the next one, or musicName, to name the file.
    const auto untitled = importTtml(bytes("<tt xmlns:amll=\"http://www.example.com/ns/amll\"><head><metadata><ttm:title> </ttm:title><amll:meta key=\"musicName\" value=\"Song\"/>"
                                           "</metadata></head><body><p begin=\"1\" end=\"2\">x</p></body></tt>"));
    ASSERT_TRUE(untitled.has_value());
    EXPECT_EQ(untitled->document.scriptInfo(u8"Title"), u8"Song");
    // AMLL's musicName collapses its whitespace, as a <ttm:title> does.
    const auto named = importTtml(bytes("<tt xmlns:amll=\"http://www.example.com/ns/amll\"><head><metadata><amll:meta key=\"musicName\" value=\"  My \t  Song \"/>"
                                        "</metadata></head><body><p begin=\"1\" end=\"2\">x</p></body></tt>"));
    ASSERT_TRUE(named.has_value());
    EXPECT_EQ(named->document.scriptInfo(u8"Title"), u8"My Song");
    // Another namespace's title and agent are not TTML's, nor is what they hold.
    const auto foreign = importTtml(bytes("<tt><head><metadata><f:title xmlns:f=\"urn:other\">Wrong</f:title>"
                                          "<f:x xmlns:f=\"urn:other\"><ttm:title>Inside</ttm:title></f:x><ttm:title>Right</ttm:title>"
                                          "<f:agent xmlns:f=\"urn:other\" xml:id=\"v1\"><ttm:name>Wrong</ttm:name></f:agent>"
                                          "</metadata></head><body><p begin=\"1\" end=\"2\" ttm:agent=\"v1\">x</p></body></tt>"));
    ASSERT_TRUE(foreign.has_value());
    EXPECT_EQ(foreign->document.scriptInfo(u8"Title"), u8"Right");
    EXPECT_EQ(rows(*foreign), (std::vector<Row>{{1000, 2000, "v1", "x"}}));
    // Nor is an xml: element's (xml: is no namespace of TTML's).
    const auto xmlWrapped = importTtml(bytes("<tt><head><metadata><xml:x><ttm:title>Wrong</ttm:title></xml:x>"
                                             "<ttm:title>Right</ttm:title></metadata></head><body><p begin=\"1\" end=\"2\">x</p>"
                                             "</body></tt>"));
    ASSERT_TRUE(xmlWrapped.has_value());
    EXPECT_EQ(xmlWrapped->document.scriptInfo(u8"Title"), u8"Right");
    // Nor is the text of another namespace's element inside a title or name.
    const auto inside = importTtml(bytes("<tt><head><metadata><ttm:title>Right<f:x xmlns:f=\"urn:other\">Wrong</f:x></ttm:title>"
                                         "<ttm:agent xml:id=\"v1\"><ttm:name>Lead<f:x xmlns:f=\"urn:other\">Wrong</f:x></ttm:name>"
                                         "</ttm:agent></metadata></head><body><p begin=\"1\" end=\"2\" ttm:agent=\"v1\">x</p></body>"
                                         "</tt>"));
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(inside->document.scriptInfo(u8"Title"), u8"Right");
    EXPECT_EQ(rows(*inside), (std::vector<Row>{{1000, 2000, "Lead", "x"}}));
    const std::vector<Row> expected{
        // The space between the spans stays after its word; the romanisation
        // and translation spans are not lyrics to time.
        {1000, 4000, "v1", "{\\k50}さ{\\k50}く{\\k60}ら {\\k30}{\\k40}さ{\\k30}く{\\k40}ら"},
        // "ら" starts before "そ" ends: "そ" stops there. The <p> ends after
        // the last word: a pause at the end.
        {4000, 8000, "v1", "{\\k40}や{\\k40}よ{\\k50}い{\\k30}の {\\k10}{\\k35}そ{\\k55}ら{\\k90}は{\\k50}"},
        // The background vocal is a Line of its own, after its main Line.
        {6800, 7900, "v1", "{\\k50}(はな {\\k60}ざかり)"},
        // A <p> with no word times is a plain Line.
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
    // Seconds-only times; the agent's name is the actor.
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
    // Words before the first stamp start at the line time, so the stamp may
    // not be earlier.
    EXPECT_EQ(problemOf("[00:05.00]lead <00:04.00>word", u8"lrc"), (LyricsProblem{Kind::TimeGoesBack, 1, u8"<00:04.00>"}));
    // Past 100 hours is not a time.
    EXPECT_EQ(problemOf("[6000:00.01]x", u8"lrc"), (LyricsProblem{Kind::UnreadableTime, 1, u8"[6000:00.01]"}));
    EXPECT_EQ(problemOf("[00:01.00]<999999999:00.00>x", u8"lrc"),
              (LyricsProblem{Kind::UnreadableTime, 1, u8"<999999999:00.00>"}));
    // An offset is whole signed milliseconds, up to 100 hours.
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
    // An entity on a later line of a text or a value is that line's fault.
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">ok\nnot &nbsp; y</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 4, u8"not &nbsp; y</p>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\" x:a=\"ok\nnot &nbsp;\">x</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 4, u8"not &nbsp;\">"}));
    EXPECT_EQ(problemOf(head + "<!-- open\n</body></tt>", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"<!-- open"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=2>x</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"end=2>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x</p>\n", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 4, u8""}));
    EXPECT_EQ(problemOf("<html>\n<body/></html>", u8"ttml"), (LyricsProblem{Kind::NotTtml, 1, u8"<html>"}));
    // A tt in another namespace is not TTML's; one in TTML's, or in none, is.
    EXPECT_EQ(problemOf("<x:tt xmlns:x=\"urn:other\"><x:body><x:p begin=\"1\" end=\"2\">x</x:p></x:body></x:tt>", u8"ttml"),
              (LyricsProblem{Kind::NotTtml, 1, u8"<x:tt>"}));
    EXPECT_EQ(problemOf("<tt xmlns=\"urn:other\"><body><p begin=\"1\" end=\"2\">x</p></body></tt>", u8"ttml").kind, Kind::NotTtml);
    EXPECT_TRUE(importTtml(bytes("<t:tt xmlns:t=\"http://www.w3.org/ns/ttml\"><t:body><t:p begin=\"1\" end=\"2\">x</t:p>"
                                 "</t:body></t:tt>")).has_value());
    // Another namespace's body or p, and what it holds, is not read.
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
    // A time on a later line of its tag is at fault on its own line.
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
    // Names are compared whole: a local name may repeat under other prefixes.
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" ttm:a=\"1\" end=\"2\" xml:a=\"1\">x" + tail)).has_value());
    EXPECT_EQ(problemOf(head + "<p begin=\"1\"end=\"2\">x" + tail, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"end=\"2\">"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">a ]]> b" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"]]>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"><1x/>" + tail, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"<1x/>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" -end=\"2\">x" + tail, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"-end=\"2\">"}));
    // Names follow XML's grammar, in any script it allows.
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"><$>x</$>" + tail, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"><a$>x</a$>" + tail, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\" $a=\"1\">x" + tail, u8"ttml").kind, Kind::NotWellFormed);
    // And namespaces' grammar: a prefix and a local name, neither empty, at
    // most one colon, each starting as an XML name does.
    EXPECT_EQ(problemOf("<:tt><:body><:p begin=\"1\" end=\"2\">x</:p></:body></:tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 1, u8"<:tt>"}));
    for (const char *bad : {"<a:/>", "<a:b:c/>", "<p:/>", "<x/ :a=\"1\">", "<a:1/>", "<a:-b/>", "<a:.b/>", "<a:\xC2\xB7" "b/>"})
        EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">" + bad + "x" + tail, u8"ttml").kind, Kind::NotWellFormed) << bad;
    for (const char *bad : {" :a=\"1\"", " a:=\"1\"", " a:b:c=\"1\"", " xmlns:a=\"urn:x\" a:1=\"x\"", " xmlns:a=\"urn:x\" a:-x=\"x\""})
        EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\"" + bad + ">x" + tail, u8"ttml").kind, Kind::NotWellFormed) << bad;
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" end=\"2\" xmlns:x=\"urn:x\" x:l\xC3\xA9-g\xC2\xB7.1=\"1\"><\xE6\x97\xA5\xE6\x9C\xAC/>x" + tail))
                    .has_value());
    // Characters XML forbids, as references or written out.
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x &#xFFFE; y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"x &#xFFFE; y</p>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x &#1; y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"x &#1; y</p>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x \x01 y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"U+0001"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x \xEF\xBF\xBF y" + tail, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"U+FFFF"}));
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" end=\"2\">x&#9;y\ty" + tail)).has_value());
    // A character reference may have any number of digits, leading zeros too.
    EXPECT_EQ(rows(*importTtml(bytes(head + "<p begin=\"1\" end=\"2\" xml:id=\"&#x0000000041;\">&#00000000065;&#x000000042;" + tail))),
              (std::vector<Row>{{1000, 2000, "", "AB"}}));
    EXPECT_TRUE(importTtml(bytes("<!DOCTYPE tt [<!ENTITY e '&#0000000065;'>]>" + head + "<p begin=\"1\" end=\"2\">x" + tail))
                    .has_value());
    for (const char *bad : {"&#x110000;", "&#x0000000110000;", "&#99999999999999999999;", "&#x;", "&#;"})
        EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">" + bad + tail, u8"ttml").kind, Kind::NotWellFormed) << bad;
    // A comment may not hold "--" or end with "-".
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
    // <tt> and <body> are two levels; 62 <div>s fill the rest.
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
    // Whitespace collapses across nested elements, as over the whole paragraph.
    const auto plain = importTtml(bytes("<tt><body><p begin=\"1\" end=\"2\">a <span> b </span> c</p></body></tt>"));
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(rows(*plain), (std::vector<Row>{{1000, 2000, "", "a b c"}}));
    // A <br/> inside a timed span breaks the Line there too.
    const auto words = importTtml(bytes("<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">a<br/>b</span> "
                                        "<span begin=\"2\" end=\"3\">c <span>d</span></span></p></body></tt>"));
    ASSERT_TRUE(words.has_value());
    EXPECT_EQ(rows(*words), (std::vector<Row>{{1000, 3000, "", "{\\k100}a\\Nb {\\k100}c d"}}));
}

TEST(LyricsImport, RepeatedTextCannotOutgrowTheFile)
{
    // One long lyric under thousands of line times.
    std::string lrc;
    for (int i = 0; i < 3000; ++i)
        lrc += "[00:01.00]";
    lrc += std::string(2000, 'x') + "\n";
    EXPECT_EQ(problemOf(lrc, u8"lrc"), (LyricsProblem{Kind::TooLarge, 1, u8""}));
    // An agent's long name on thousands of Lines.
    std::string ttml = "<tt><head><ttm:agent xml:id=\"a\"><ttm:name>" + std::string(100000, 'n') +
                       "</ttm:name></ttm:agent></head><body>\n";
    for (int i = 0; i < 2000; ++i)
        ttml += "<p ttm:agent=\"a\" begin=\"1\" end=\"2\">x</p>\n";
    ttml += "</body></tt>";
    EXPECT_EQ(problemOf(ttml, u8"ttml").kind, Kind::TooLarge);
    // A voice marker is at most nine digits; a longer one is lyrics text.
    const auto voices = importLrc(bytes("[00:01.00]v123456789:a\n[00:02.00]v1234567890:b\n"));
    ASSERT_TRUE(voices.has_value());
    EXPECT_EQ(rows(*voices), (std::vector<Row>{{1000, 2000, "v123456789", "a"}, {2000, 7000, "v123456789", "v1234567890:b"}}));
    // A chorus under a few line times is fine.
    EXPECT_TRUE(importLrc(bytes("[00:01.00][00:05.00][00:09.00]chorus\n[00:03.00]verse\n")).has_value());
}

TEST(LyricsImport, FilesAndXmlTreesHaveALimit)
{
    // Over 1 MiB is more than any lyrics file.
    const std::string big = "[00:01.00]" + std::string(kMaxLyricsFileSize, 'x');
    EXPECT_EQ(problemOf(big, u8"lrc"), (LyricsProblem{Kind::TooLarge, 0, u8""}));
    EXPECT_EQ(problemOf("<tt>" + std::string(kMaxLyricsFileSize, ' ') + "</tt>", u8"ttml"),
              (LyricsProblem{Kind::TooLarge, 0, u8""}));
    // Elements packed far denser than lyrics need (two nodes per five bytes)
    // stop while the tree is built.
    std::string dense = "<tt><body>\n<p begin=\"1\" end=\"2\">";
    for (int i = 0; i < 100000; ++i)
        dense += "a<x/>";
    dense += "</p></body></tt>";
    EXPECT_EQ(problemOf(dense, u8"ttml"), (LyricsProblem{Kind::TooLarge, 2, u8""}));
}

TEST(LyricsImport, TtmlHeadNamesShareTheBudget)
{
    // Agents nested in agents' names (unprefixed, as some files write
    // them, so each name holds the ones inside): each name read is taken
    // from the Lines' budget, so a long name copied for each is refused.
    std::string head;
    for (int i = 0; i < 30; ++i)
        head += "<agent xml:id=\"a" + std::to_string(i) + "\"><name>";
    head += std::string(200000, 'n');
    for (int i = 0; i < 30; ++i)
        head += "</name></agent>";
    EXPECT_EQ(problemOf("<tt><head>" + head + "</head><body><p begin=\"1\" end=\"2\">x</p></body></tt>", u8"ttml").kind,
              Kind::TooLarge);
    // Lyric text is paid for before it is made: escaped ("{" becomes three
    // bytes) and copied into a Line, entity text is refused past the budget.
    const std::string braces = "<!DOCTYPE tt [<!ENTITY a0 '" + std::string(1000, '{') +
                               "'><!ENTITY a1 '&a0;&a0;&a0;&a0;&a0;&a0;&a0;&a0;&a0;&a0;'>"
                               "<!ENTITY a2 '&a1;&a1;&a1;&a1;&a1;&a1;&a1;&a1;&a1;&a1;'>]>";
    EXPECT_EQ(problemOf(braces + "<tt><body><p begin=\"1\" end=\"2\">&a2;&a2;</p></body></tt>", u8"ttml").kind, Kind::TooLarge);
    EXPECT_EQ(problemOf(braces + "<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">&a2;&a2;</span></p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::TooLarge);
    // AMLL's musicName too, however long an entity makes it: with the
    // lyrics beside it, it is past the budget.
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
    // One such name is read.
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
    // Any number of digits, leading zeros too; a value too large is refused
    // however long it is.
    const auto padded = importTtml(bytes("<tt><body><p begin=\"000000000000000000001s\" "
                                         "end=\"00:00:0000000000000000000002.5000000000000000000\">x</p></body></tt>"));
    ASSERT_TRUE(padded.has_value());
    EXPECT_EQ(rows(*padded), (std::vector<Row>{{1000, 2500, "", "x"}}));
    const std::string huge(400, '9');
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"" + huge + "s\">x</p></body></tt>", u8"ttml").kind, Kind::UnreadableTime);
    // A begin and a dur adding up past 100 hours too.
    EXPECT_EQ(problemOf(head + "<p begin=\"99h\" dur=\"2h\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 2, u8"2h"}));
    // A last Line's five seconds stop at 100 hours too.
    const auto last = importLrc(bytes("[99:59:59.99]x\n"));
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(rows(*last), (std::vector<Row>{{359'999'990, 360'000'000, "", "x"}}));
    // An LRC offset cannot move a time past 100 hours either.
    const auto shifted = importLrc(bytes("[offset:-99999999]\n[99:00:00.00]x\n"));
    ASSERT_TRUE(shifted.has_value());
    EXPECT_EQ(rows(*shifted).front().start, 360'000'000);
    // LRC stamps and offsets are digits of any length too: padding is read,
    // and a value too large is refused however long it is.
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
    // Braces become fullwidth; a backslash before N, n or h, or at the end,
    // gets a word joiner so it starts no ASS escape. Other backslashes stay.
    const std::string wj = "\u2060";
    const auto lrc = importLrc(bytes("[00:01.00]<00:01.00>{a}\\N <00:02.00>b\\\n[00:03.00]{\\i1}x\\n\\h\n"));
    ASSERT_TRUE(lrc.has_value());
    EXPECT_EQ(rows(*lrc), (std::vector<Row>{{1000, 3000, "", "{\\k100}\uFF5Ba\uFF5D\\" + wj + "N {\\k100}b\\" + wj},
                                           {3000, 8000, "", "\uFF5B\\i1\uFF5Dx\\" + wj + "n\\" + wj + "h"}}));
    // A <br/> still breaks the Line.
    const auto ttml = importTtml(bytes("<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">{x}</span> "
                                       "<span begin=\"2\" end=\"3\">a\\h</span><br/>c\\</p></body></tt>"));
    ASSERT_TRUE(ttml.has_value());
    EXPECT_EQ(rows(*ttml),
              (std::vector<Row>{{1000, 3000, "", "{\\k100}\uFF5Bx\uFF5D {\\k100}a\\" + wj + "h\\Nc\\" + wj}}));
}

TEST(LyricsImport, LrcLinesLastUntilTheNextLaterTime)
{
    // Lines sharing a time all last until the next later one.
    const auto result = importLrc(bytes("[00:01.00][00:04.00]a\n[00:01.00]b\n[00:02.00]c\n"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(rows(*result), (std::vector<Row>{{1000, 2000, "", "a"}, {1000, 2000, "", "b"}, {2000, 4000, "", "c"},
                                               {4000, 9000, "", "a"}}));
}

TEST(LyricsImport, TtmlTimesAndWordEnds)
{
    // dur instead of end, offset times, and a word without an end lasting
    // until the next word.
    const auto result = importTtml(bytes("<tt><body><p begin=\"1500ms\" dur=\"2s\">"
                                         "<span begin=\"1.5s\">a</span> <span begin=\"0.04m\" dur=\"0.5s\">b</span>"
                                         "</p></body></tt>"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(rows(*result), (std::vector<Row>{{1500, 3500, "", "{\\k90}a {\\k50}b{\\k60}"}}));
    // A decimal point has digits on both sides, in offset and clock times.
    const std::string p = "<tt><body><p begin=\"";
    for (const char *bad : {"1.", "1.s", ".5s", "1:02.", "0:.5", "."})
        EXPECT_EQ(problemOf(p + bad + "\" end=\"9\">x</p></body></tt>", u8"ttml"),
                  (LyricsProblem{Kind::UnreadableTime, 1, std::u8string(reinterpret_cast<const char8_t *>(bad))}))
            << bad;
    EXPECT_TRUE(importTtml(bytes(p + "0.5s\" dur=\"1.0\">x</p></body></tt>")).has_value());
}

TEST(LyricsImport, ClockMinutesAndSecondsStayUnder60)
{
    // The first part may run past 59; a clock's minutes and seconds may not.
    EXPECT_EQ(problemOf("[00:99.00]x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[1:60:00.00]x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_EQ(problemOf("[00:01.00]<00:61.00>x", u8"lrc").kind, Kind::UnreadableTime);
    EXPECT_TRUE(importLrc(bytes("[75:02.00]x\n[1:02:03.50]y\n[1:02:04.123]z")).has_value());
    // A fraction is tenths to milliseconds; a fourth digit would be cut.
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
    // XML's line ends: a fault after CR-only and CRLF lines is on its own line.
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
    // A "]" closes only the "[" that opened the internal subset, and ">" follows it.
    EXPECT_TRUE(imports("<!DOCTYPE tt [ ] >"));
    // An XML declaration: version 1.n, then an encoding name and standalone,
    // in that order.
    EXPECT_TRUE(imports("<?xml version='1.1' encoding=\"utf-8\" standalone='no' ?>"));
    // The file is read as UTF-8, so a declaration naming another encoding is
    // not read (in any case, "utf-8" is).
    EXPECT_TRUE(imports("<?xml version=\"1.0\" encoding=\"Utf-8\"?>"));
    for (const char *other : {"UTF-16", "ISO-8859-1", "Shift_JIS", "utf8"})
        EXPECT_EQ(problemOf("<?xml version=\"1.0\" encoding=\"" + std::string(other) + "\"?>" + root, u8"ttml"),
                  (LyricsProblem{Kind::NotUtf8, 1, u8""}))
            << other;
    // A 1.1 document is read as XML 1.0, so what only 1.1 allows is broken.
    EXPECT_EQ(problemOf("<?xml version=\"1.1\"?><tt><body><p begin=\"1\" end=\"2\">&#1;</p></body></tt>", u8"ttml").kind,
              Kind::NotWellFormed);
    for (const char *bad : {"<?xml?>", "<?xml garbage?>", "<?xml version=\"2.0\"?>", "<?xml version=\"1.\"?>",
                            "<?xml encoding=\"UTF-8\" version=\"1.0\"?>", "<?xml version=\"1.0\" standalone=\"maybe\"?>",
                            "<?xml version=\"1.0\" encoding=\"8bit\"?>", "<?xml version=\"1.0\"encoding=\"UTF-8\"?>",
                            "<?xml version=\"1.0\" extra=\"x\"?>", "<?xml version=\"1.0\""})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    // After the name only an external id, an internal subset and ">".
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
    // The subset holds markup declarations, PIs, comments, parameter-entity
    // references and whitespace, nothing else.
    EXPECT_TRUE(imports("<!DOCTYPE tt [\n <!ELEMENT tt ANY>\n <!ATTLIST p begin CDATA #IMPLIED>\n"
                        " <!ENTITY % e \"<!-- x -->\"> %e; <!NOTATION n SYSTEM 'n'> <?pi x?> <!-- c -->\n]>"));
    for (const char *bad : {"<!DOCTYPE tt [garbage]>", "<!DOCTYPE tt [<!ELEMENT>]>", "<!DOCTYPE tt [<!ELEMENTtt ANY>]>",
                            "<!DOCTYPE tt [<!ELEMENT tt <x>>]>", "<!DOCTYPE tt [%e]>", "<!DOCTYPE tt [<!FOO x>]>",
                            "<!DOCTYPE tt [<!ENTITY a 'x'>"})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    // Each declaration by its production.
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
    // Literals by their grammar: references in entity and attribute values
    // as XML writes them, a public id of PubidChars; ENTITIES is a type.
    EXPECT_TRUE(imports("<!DOCTYPE tt [<!ENTITY e 'a &amp; &#x41; &#65; &g; b'> <!ATTLIST p a CDATA 'a %b &amp; &#9;'>"
                        " <!ATTLIST p refs ENTITIES #IMPLIED ref ENTITY #IMPLIED> <!ENTITY g PUBLIC '-//W3C//x (1) +:=?;!*#@$_%' 'u'>]>"));
    for (const char *bad : {"<!DOCTYPE tt [<!ENTITY e '&broken'>]>", "<!DOCTYPE tt [<!ENTITY e 'a & b'>]>",
                            "<!DOCTYPE tt [<!ENTITY e '%p'>]>", "<!DOCTYPE tt [<!ENTITY e '50%'>]>",
                            // No parameter-entity reference inside the internal subset's declarations.
                            "<!DOCTYPE tt [<!ENTITY e '%p;'>]>",
                            "<!DOCTYPE tt [<!ENTITY e '&#1;'>]>", "<!DOCTYPE tt [<!ENTITY e '&#xZ;'>]>",
                            "<!DOCTYPE tt [<!ENTITY e '&1a;'>]>", "<!DOCTYPE tt [<!ATTLIST p a CDATA '&x'>]>",
                            "<!DOCTYPE tt [<!ATTLIST p a CDATA '&#xFFFE;'>]>", "<!DOCTYPE tt [<!ENTITY g PUBLIC 'a{b' 'u'>]>",
                            "<!DOCTYPE tt [<!NOTATION n PUBLIC 'a\\b'>]>"})
        EXPECT_EQ(problemOf(bad + root, u8"ttml").kind, Kind::NotWellFormed) << bad;
    // A DOCTYPE needs the root's name, comes once and only before the root.
    EXPECT_EQ(problemOf("<!DOCTYPE>" + root, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 1, u8"<!DOCTYPE>"}));
    EXPECT_EQ(problemOf("<!DOCTYPEtt>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE html>\n" + root, u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 2, u8"<tt>"}));
    EXPECT_EQ(problemOf("<!DOCTYPE tt>\n<!DOCTYPE tt>\n" + root, u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 2, u8"<!DOCTYPE tt>"}));
    EXPECT_EQ(problemOf(root + "\n<!DOCTYPE tt>", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 2, u8"<!DOCTYPE tt>"}));
    // The XML declaration only at the very start, in lower case; a
    // processing instruction needs a target.
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
    // A <p> without times is active over its nearest timed ancestor's.
    EXPECT_EQ(timed("<div begin=\"1\" end=\"3\"><p>hello</p></div>"), (std::vector<Row>{{1000, 3000, "", "hello"}}));
    EXPECT_EQ(timed("<div begin=\"1\" dur=\"1\"><div><p>a</p></div></div>"), (std::vector<Row>{{1000, 2000, "", "a"}}));
    // A dur without a begin of its own counts from the begin it inherits.
    EXPECT_EQ(timed("<div begin=\"1\" end=\"10\"><p dur=\"2\">x</p></div>"), (std::vector<Row>{{1000, 3000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"1\"><div dur=\"3\"><p>y</p></div></div>"), (std::vector<Row>{{1000, 4000, "", "y"}}));
    // Another namespace's element gives no text, times or words.
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\">a <f:span xmlns:f=\"urn:other\" begin=\"1\" end=\"2\">x</f:span></p>"),
              (std::vector<Row>{{0, 4000, "", "a"}}));
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\"><span begin=\"0\" end=\"1\">a<f:i xmlns:f=\"urn:o\">x</f:i></span>"
                    "<span begin=\"1\" end=\"4\">b</span></p>"),
              (std::vector<Row>{{0, 4000, "", "{\\k100}a{\\k300}b"}}));
    // A dur is read even with no begin to count from.
    EXPECT_EQ(problemOf("<tt><body dur=\"bogus\"><p begin=\"1\" end=\"2\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 1, u8"bogus"}));
    // A word's span with only a dur or an end counts from the begin it
    // inherits, or from the end of the word before it.
    EXPECT_EQ(timed("<p begin=\"1\" end=\"4\"><span dur=\"1\">a</span></p>"), (std::vector<Row>{{1000, 4000, "", "{\\k100}a{\\k200}"}}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"4\"><span begin=\"1\" end=\"2\">a</span><span end=\"3\">b</span></p>"),
              (std::vector<Row>{{1000, 4000, "", "{\\k100}a{\\k100}b{\\k100}"}}));
    // A span around timed spans with only an end starts at the begin it
    // inherits, so its own text is a word from there.
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\"><span end=\"3\">hello <span begin=\"2\" end=\"3\">world</span></span></p>"),
              (std::vector<Row>{{0, 4000, "", "{\\k200}hello {\\k100}world{\\k100}"}}));
    // With an end too, the earlier of the two ends it.
    EXPECT_EQ(timed("<p begin=\"1\" end=\"5\" dur=\"2\">x</p>"), (std::vector<Row>{{1000, 3000, "", "x"}}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"2\" dur=\"5\">x</p>"), (std::vector<Row>{{1000, 2000, "", "x"}}));
    // A dur past the latest time does not matter when the end comes first.
    EXPECT_EQ(timed("<p begin=\"99:59:59\" end=\"99:59:59.5\" dur=\"10\">x</p>"),
              (std::vector<Row>{{359'999'000, 359'999'500, "", "x"}}));
    EXPECT_EQ(timed("<p begin=\"1\" end=\"9\"><span begin=\"1\" end=\"2\">a</span><span dur=\"2\"><span begin=\"2\">b</span>"
                    "</span></p>"),
              (std::vector<Row>{{1000, 9000, "", "{\\k100}a{\\k100}b{\\k600}"}})); // the span ends at 1 + 2 s
    EXPECT_EQ(timed("<div begin=\"1\" end=\"3\"><p begin=\"2\">a</p><p end=\"2\">b</p></div>"),
              (std::vector<Row>{{2000, 3000, "", "a"}, {1000, 2000, "", "b"}}));
    // A child's end or dur stops at the end it inherits: its interval lies
    // within its parent's.
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p begin=\"3\" dur=\"5\">x</p></div>"), (std::vector<Row>{{3000, 4000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p begin=\"3\" end=\"8\">x</p></div>"), (std::vector<Row>{{3000, 4000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><div dur=\"9\"><p>x</p></div></div>"), (std::vector<Row>{{0, 4000, "", "x"}}));
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p begin=\"1\" end=\"8\"><span begin=\"1\" end=\"2\">a</span></p></div>"),
              (std::vector<Row>{{1000, 4000, "", "{\\k100}a{\\k200}"}}));
    EXPECT_EQ(timed("<p begin=\"0\" end=\"6\"><span begin=\"0\" end=\"3\">a <span begin=\"1\" dur=\"9\"><span begin=\"1\">b</span>"
                    "</span></span></p>"),
              (std::vector<Row>{{0, 6000, "", "{\\k100}a {\\k200}b{\\k300}"}}));
    // So does a word's, and a begin before the one it inherits starts there.
    EXPECT_EQ(timed("<p begin=\"0\" end=\"4\"><span begin=\"3\" end=\"8\">x</span></p>"),
              (std::vector<Row>{{0, 4000, "", "{\\k300}{\\k100}x"}}));
    EXPECT_EQ(timed("<p begin=\"2\" end=\"4\"><span begin=\"1\" end=\"3\">x</span></p>"),
              (std::vector<Row>{{2000, 4000, "", "{\\k100}x{\\k100}"}}));
    EXPECT_EQ(timed("<div begin=\"2\" end=\"4\"><p begin=\"1\" end=\"3\">x</p></div>"), (std::vector<Row>{{2000, 3000, "", "x"}}));
    // One wholly outside the times it inherits is wrong, at its begin or end.
    EXPECT_EQ(problemOf("<tt><body><p begin=\"0\" end=\"4\"><span begin=\"5\" end=\"6\">x</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 1, u8"5"}));
    EXPECT_EQ(problemOf("<tt><body><p begin=\"2\" end=\"4\"><span begin=\"0\" end=\"1\">x</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 1, u8"1"}));
    EXPECT_EQ(problemOf("<tt><body><p begin=\"0\" end=\"4\"><span begin=\"5\" end=\"6\">a<span begin=\"5\">b</span></span>"
                        "</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 1, u8"5"}));
    EXPECT_EQ(problemOf("<tt><body><div begin=\"0\" end=\"2\">\n<div begin=\"3\"><p>x</p></div></div></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 2, u8"3"}));
    // Background vocals keep their own times, as AMLL writes them over their
    // main Line, starting before it or ending after it.
    EXPECT_EQ(timed("<p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"3\">a</span><span ttm:role=\"x-bg\" begin=\"0.5\" "
                    "end=\"3.5\"><span begin=\"0.5\" end=\"3.5\">b</span></span></p>"),
              (std::vector<Row>{{1000, 3000, "", "{\\k200}a"}, {500, 3500, "", "{\\k300}b"}}));
    // A word-timed Line spans its words; the ancestor ends its last word.
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p><span begin=\"1\" end=\"2\">a</span> <span begin=\"2\">b</span></p></div>"),
              (std::vector<Row>{{1000, 4000, "", "{\\k100}a {\\k200}b"}}));
    // Its own begin after the end it inherits; no times anywhere.
    EXPECT_EQ(problemOf("<tt><body>\n<div begin=\"1\" end=\"3\">\n<p begin=\"5\">x</p></div></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::OutsideParent, 3, u8"5"}));
    EXPECT_EQ(problemOf("<tt><body><div><p>x</p></div></body></tt>", u8"ttml").kind, Kind::MissingTime);
    // A body without a begin starts at the document's beginning (TTML2's
    // body), so a dur or an end alone times what it holds.
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
    // A timed span without lyrics is no word, and no lyrics at all is NoLyrics.
    EXPECT_EQ(problemOf("<tt><body><p><span begin=\"1\" end=\"2\"/></p></body></tt>", u8"ttml").kind, Kind::NoLyrics);
    EXPECT_EQ(problemOf("<tt><body><p><span begin=\"1\" end=\"2\">  </span></p></body></tt>", u8"ttml").kind,
              Kind::NoLyrics);
    EXPECT_EQ(text("<p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">a</span><span begin=\"2\" end=\"3\"> </span></p>"),
              "{\\k100}a{\\k100}");
    // A word without its own end ends with the timed span around it.
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"1\" end=\"2\"><span begin=\"1\">x</span></span></p>"),
              "{\\k100}{\\k100}x{\\k200}");
    // Text in a timed span beside its timed words is a word that span times:
    // from its begin, or from the end of the word before it, to its end.
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"1\" end=\"3\">hello <span begin=\"2\" end=\"3\">world</span>"
                   "</span></p>"),
              "{\\k100}{\\k100}hello {\\k100}world{\\k100}");
    EXPECT_EQ(text("<p begin=\"1\" end=\"4\"><span begin=\"1\" end=\"4\">hello <span begin=\"2\" end=\"3\">world</span>"
                   " again</span></p>"),
              "{\\k100}hello {\\k100}world {\\k100}again");
    EXPECT_EQ(text("<p begin=\"0\" end=\"3\"><span begin=\"1\" end=\"3\"><span>hel</span><span begin=\"2\" end=\"3\">lo</span>"
                   "</span></p>"),
              "{\\k100}{\\k100}hel{\\k100}lo");
    // The span's text going on through an untimed element is the same word.
    EXPECT_EQ(text("<p begin=\"0\" end=\"3\"><span begin=\"1\" end=\"3\">hel<span>lo</span><span begin=\"2\" end=\"3\">world"
                   "</span></span></p>"),
              "{\\k100}{\\k100}hello{\\k100}world");
    // So is a paragraph's own text beside its timed words, from the
    // paragraph's begin; text with no time of its own joins the word
    // beside it as written.
    EXPECT_EQ(text("<p begin=\"1\" end=\"3\">hel<span begin=\"2\" end=\"3\">lo</span></p>"), "{\\k100}hel{\\k100}lo");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\">hello <span begin=\"2\" end=\"3\">world</span></p>"),
              "{\\k200}hello {\\k100}world{\\k100}");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"0\" end=\"1\">a</span> b <span begin=\"2\" end=\"4\">c</span>"
                   "</p>"),
              "{\\k100}a {\\k100}b {\\k200}c");
    EXPECT_EQ(text("<p begin=\"0\" end=\"4\"><span begin=\"0\" end=\"3\">a</span> b <span begin=\"2\" end=\"4\">c</span>"
                   "</p>"),
              "{\\k200}a {\\k200}b c"); // b would begin after c does
    EXPECT_EQ(text("<p begin=\"0\" end=\"2\"><span begin=\"0\" end=\"2\">a</span> b</p>"), "{\\k200}a b");
    EXPECT_EQ(text("<p begin=\"0\"><span begin=\"0\" end=\"2\">a</span> b</p>"), "{\\k200}a b");
}

TEST(LyricsImport, ActorsKeepAMarkerLikeStart)
{
    // The loader reads "[hidden]" and the other markers in a Name that starts
    // with "["; a word joiner keeps such an agent's name as written.
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
    // In text: as text, inside a word, holding markup, naming another, and
    // with its character references read when declared.
    EXPECT_EQ(read("<!ENTITY lyric 'hello'>", "<p begin=\"1\" end=\"2\">&lyric;</p>"), (std::vector<Row>{{1000, 2000, "", "hello"}}));
    EXPECT_EQ(read("<!ENTITY lo 'lo'>", "<p begin=\"1\" end=\"2\">hel&lo; you</p>"), (std::vector<Row>{{1000, 2000, "", "hello you"}}));
    EXPECT_EQ(read("<!ENTITY w '<span begin=\"1\" end=\"2\">a</span>'>",
                   "<p begin=\"1\" end=\"3\">&w;<span begin=\"2\" end=\"3\">b</span></p>"),
              (std::vector<Row>{{1000, 3000, "", "{\\k100}a{\\k100}b"}}));
    EXPECT_EQ(read("<!ENTITY a 'x&b;'><!ENTITY b 'y&#38;amp;'>", "<p begin=\"1\" end=\"2\">&a;</p>"),
              (std::vector<Row>{{1000, 2000, "", "xy&"}}));
    EXPECT_EQ(read("<!ENTITY nbsp '&#160;'>", "<p begin=\"1\" end=\"2\">a&nbsp;b</p>"),
              (std::vector<Row>{{1000, 2000, "", "a\u00A0b"}}));
    // The first declaration of a name binds.
    EXPECT_EQ(read("<!ENTITY e 'a'><!ENTITY e 'b'>", "<p begin=\"1\" end=\"2\">&e;</p>"), (std::vector<Row>{{1000, 2000, "", "a"}}));
    // In an attribute value, as text.
    EXPECT_EQ(read("<!ENTITY t '1.5s'><!ENTITY u '&t;'>", "<p begin=\"&u;\" end=\"2s\">x</p>"),
              (std::vector<Row>{{1500, 2000, "", "x"}}));
    // Attribute defaults, and values of a tokenized type normalized.
    EXPECT_EQ(read("<!ATTLIST p begin CDATA '1s' end CDATA #FIXED '2s'>", "<p>x</p><p begin=\"3s\" end=\"4s\">y</p>"),
              (std::vector<Row>{{1000, 2000, "", "x"}, {3000, 4000, "", "y"}}));
    EXPECT_EQ(read("<!ATTLIST p xml:space (default|preserve) 'preserve'>", "<p begin=\"1\" end=\"2\">a  b</p>"),
              (std::vector<Row>{{1000, 2000, "", "a  b"}}));
    EXPECT_EQ(problemOf("<!DOCTYPE tt [<!ATTLIST tt xmlns CDATA #FIXED 'urn:other'>]><tt><body/></tt>", u8"ttml").kind,
              Kind::NotTtml); // a default namespace declaration too
    // A default is an attribute of the element: no two by expanded name.
    EXPECT_EQ(problemOf("<!DOCTYPE tt [<!ATTLIST p a:x CDATA 'default'>]><tt><body><p begin=\"1\" end=\"2\" "
                        "xmlns:a=\"urn:x\" xmlns:b=\"urn:x\" b:x=\"given\">x</p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::NotWellFormed);
    EXPECT_EQ(read("<!ATTLIST p xml:space NMTOKEN #IMPLIED>", "<p begin=\"1\" end=\"2\" xml:space=\" preserve  \">a  b</p>"),
              (std::vector<Row>{{1000, 2000, "", "a  b"}}));
    // An internal parameter entity's declarations are read where it is
    // referenced, one naming another through a character reference too.
    EXPECT_EQ(read("<!ENTITY % defs '<!ENTITY lyric \"hello\">'> %defs;", "<p begin=\"1\" end=\"2\">&lyric;</p>"),
              (std::vector<Row>{{1000, 2000, "", "hello"}}));
    EXPECT_EQ(read("<!ENTITY % times '<!ATTLIST p begin CDATA \"1s\" end CDATA \"2s\">'>%times;", "<p>x</p>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    EXPECT_EQ(read("<!ENTITY % inner '<!ENTITY e \"a\">'><!ENTITY % outer '&#37;inner; <!-- c -->'>%outer;",
                   "<p begin=\"1\" end=\"2\">&e;</p>"),
              (std::vector<Row>{{1000, 2000, "", "a"}}));
    // Past a parameter-entity reference never read (an external or an
    // undeclared one), declarations are only checked, unless the file is
    // standalone.
    EXPECT_TRUE(read("<!ENTITY % ext SYSTEM 'x.dtd'>%ext;<!ENTITY e 'a'>", "<p begin=\"1\" end=\"2\">&e;</p>").empty());
    EXPECT_TRUE(read("%p;<!ENTITY e 'a'>", "<p begin=\"1\" end=\"2\">&e;</p>").empty());
    const auto standalone = importTtml(bytes("<?xml version=\"1.0\" standalone=\"yes\"?>"
                                             "<!DOCTYPE tt [<!ENTITY % p SYSTEM 'p.dtd'>%p;<!ENTITY e 'a'>]>"
                                             "<tt><body><p begin=\"1\" end=\"2\">&e;</p></body></tt>"));
    ASSERT_TRUE(standalone.has_value());
    EXPECT_EQ(rows(*standalone), (std::vector<Row>{{1000, 2000, "", "a"}}));
    // ...where a parameter entity no declaration names is broken.
    EXPECT_EQ(problemOf("<?xml version=\"1.0\" standalone=\"yes\"?><!DOCTYPE tt [%missing;<!ENTITY e 'a'>]>"
                        "<tt><body><p begin=\"1\" end=\"2\">&e;</p></body></tt>",
                        u8"ttml")
                  .kind,
              Kind::NotWellFormed);
    // A line end written in an entity value is one line feed, in an
    // attribute value one space; a "&#13;" is a character of its own, so
    // it and a line end are two spaces.
    EXPECT_EQ(read("<!ENTITY singer 'Lead\r\nVoice'><!ENTITY two 'Lead&#13;\rVoice'>",
                   "<p begin=\"1\" end=\"2\" ttm:agent=\"&singer;\">x</p><p begin=\"3\" end=\"4\" ttm:agent=\"&two;\">y</p>"),
              (std::vector<Row>{{1000, 2000, "Lead Voice", "x"}, {3000, 4000, "Lead  Voice", "y"}}));
    EXPECT_EQ(read("<!ENTITY e 'a\r\nb'>", "<p begin=\"1\" end=\"2\" xml:space=\"preserve\">&e;</p>"),
              (std::vector<Row>{{1000, 2000, "", "a\\Nb"}}));

    // Refused: an entity naming itself, an external one, one that opens
    // or closes an element it does not close or open, a "<" into an
    // attribute value, an XML declaration inside one; reported at the
    // reference.
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
    // A parameter entity naming itself, holding part of a declaration or
    // the subset's "]".
    for (const char *subset : {"<!ENTITY % r '&#37;r;'>%r;", "<!ENTITY % half \"<!ENTITY e \">%half; 'a'>",
                               "<!ENTITY % close ']>'>%close;", "<!ENTITY % pi '<?xml version=\"1.0\"?>'>%pi;"})
        EXPECT_EQ(problem(subset, p).kind, Kind::NotWellFormed) << subset;
    // Entities naming themselves are refused though the file never uses
    // them, directly or through others, general or parameter; one named
    // twice on a path is no loop.
    const std::string unused = "<p begin=\"1\" end=\"2\">x</p>";
    for (const char *subset : {"<!ENTITY loop '&loop;'>", "<!ENTITY loop '&#38; &loop;'>", "<!ENTITY a 'x&b;'><!ENTITY b '&c;y'><!ENTITY c '<s>&a;</s>'>",
                               "<!ENTITY % loop '&#37;loop;'>", "<!ENTITY % a '&#37;b;'><!ENTITY % b '&#37;a;'>"})
        EXPECT_EQ(problem(subset, unused).kind, Kind::NotWellFormed) << subset;
    // An entity naming one no declaration gives is malformed, used or not,
    // where the subset is all there is (or the file is standalone); past an
    // external subset or a parameter-entity reference it may be declared
    // there. Declared later in the subset is declared.
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
    // The five predefined entities may be declared only as what they are:
    // "<" and "&" escaped, the others escaped or not.
    for (const char *subset : {"<!ENTITY amp 'wrong'>", "<!ENTITY lt '<'>", "<!ENTITY amp '&#38;'>", "<!ENTITY gt '&#60;'>",
                               "<!ENTITY quot SYSTEM 'q.xml'>", "<!ENTITY apos ''>"})
        EXPECT_EQ(problem(subset, unused).kind, Kind::NotWellFormed) << subset;
    EXPECT_EQ(read("<!ENTITY lt '&#38;#60;'><!ENTITY amp '&#38;#x26;'><!ENTITY gt '>'><!ENTITY quot '&#34;'><!ENTITY apos \"'\">",
                   "<p begin=\"1\" end=\"2\">&lt;&amp;&gt;&quot;&apos;</p>"),
              (std::vector<Row>{{1000, 2000, "", "<&>\"'"}}));
    // Names in declarations are XML names with at most one colon, and none
    // at all where the namespaces spec says NCName: entities, notations,
    // processing instruction targets.
    for (const char *subset : {"<!ATTLIST p a:b:c CDATA 'x'>", "<!ATTLIST p a:1 CDATA 'x'>", "<!ELEMENT a:1 ANY>", "<!ATTLIST a:b:c x CDATA 'x'>", "<!ATTLIST p :x CDATA 'x'>",
                               "<!ELEMENT a:b:c ANY>", "<!ELEMENT p (a:b:c)*>", "<!ENTITY a:b 'x'>", "<!ENTITY % a:b 'x'>",
                               "<!NOTATION a:b SYSTEM 'x'>", "<?a:b x?>", "<!ENTITY e SYSTEM 'e' NDATA a:b>"})
        EXPECT_EQ(problem(subset, unused).kind, Kind::NotWellFormed) << subset;
    EXPECT_EQ(read("<!ELEMENT tt:p (a:b)*><!ATTLIST q a:b CDATA 'x'><!NOTATION n SYSTEM 'n'>", unused),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    // Entities naming entities stop at the budget, in text and attributes.
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
    // Defaults count all they take, a long name too.
    std::string defaults = "<!ATTLIST br";
    for (int i = 0; i < 100; ++i)
        defaults += " n" + std::to_string(i) + std::string(8000, 'a') + " CDATA ''";
    defaults += ">";
    std::string breaks = "<p begin=\"1\" end=\"2\">x";
    for (int i = 0; i < 30000; ++i)
        breaks += "<br/>";
    EXPECT_EQ(problem(defaults, breaks + "</p>").kind, Kind::TooLarge);
    // Declarations replayed are found by name, and those an element looks
    // at are paid for too, so neither runs on.
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
    // Inherited from the body or a div; a line end is a line break.
    EXPECT_EQ(text(" xml:space=\"preserve\"><div><p begin=\"1\" end=\"2\">a  b\nc</p></div>"), "a  b\\Nc");
    // xml:space="default" inside switches back.
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" xml:space=\"preserve\">a  <span xml:space=\"default\">b   c</span></p>"),
              "a  b c");
    // Spaces between preserved words stay with the word before.
    EXPECT_EQ(text("><p begin=\"1\" end=\"3\" xml:space=\"preserve\"><span begin=\"1\" end=\"2\">a</span>  "
                   "<span begin=\"2\" end=\"3\">b</span></p>"),
              "{\\k100}a  {\\k100}b");
    // ASS trims a Line's ends, so kept spaces there are hard spaces.
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" xml:space=\"preserve\">  a  </p>"), "\\h\\ha\\h\\h");
    EXPECT_EQ(text("><p begin=\"1\" end=\"3\" xml:space=\"preserve\">  <span begin=\"1\" end=\"2\">a</span> "
                   "<span begin=\"2\" end=\"3\">b</span>  </p>"),
              "{\\k100}\\h\\ha {\\k100}b\\h\\h");
    // A paragraph of nothing but kept spaces is still no lyric.
    const auto blank = importTtml(bytes("<tt><body><p begin=\"1\" end=\"2\" xml:space=\"preserve\">   </p>"
                                        "<p begin=\"2\" end=\"3\">x</p></body></tt>"));
    ASSERT_TRUE(blank.has_value());
    EXPECT_EQ(rows(*blank).size(), 1u);
    // The text that gives an edge decides: a kept space from a nested scope
    // is a hard space at the Line's end too, and a space inside the Line.
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\"><span xml:space=\"preserve\">  a  </span></p>"), "\\h\\ha\\h\\h");
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\"> x <span xml:space=\"preserve\">  </span> y </p>"), "x    y");
    EXPECT_EQ(text("><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\" xml:space=\"preserve\"> a</span> "
                   "<span begin=\"2\" end=\"3\">b</span></p>"),
              "{\\k100}\\ha {\\k100}b");
    // So does a kept space a timed word's text starts with from a scope
    // inside the word's span.
    EXPECT_EQ(text("><p><span begin=\"1\" end=\"2\"><span xml:space=\"preserve\">  a</span></span></p>"),
              "{\\k100}\\h\\ha");
    EXPECT_EQ(text("><p><span begin=\"1\" end=\"2\">a</span><span begin=\"2\" end=\"3\"> <span "
                   "xml:space=\"preserve\">  b</span></span></p>"),
              "{\\k100}a   {\\k100}b");
    // Only xml:space counts, not another prefix's "space".
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" xmlns:x=\"urn:x\" x:space=\"preserve\">a  b</p>"), "a b");
}

TEST(LyricsImport, TtmlAttributesAreReadByNamespace)
{
    const auto lines = [](std::string_view tt) {
        const auto result = importTtml(bytes(std::string(tt)));
        return result ? rows(*result) : std::vector<Row>{};
    };
    // Every prefix is declared (but ttm:, which such files leave undeclared).
    EXPECT_EQ(problemOf("<tt><body><p begin=\"1\" end=\"2\" foo:a=\"x\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 1, u8"<p begin=\"1\" end=\"2\" foo:a=\"x\">"}));
    EXPECT_EQ(problemOf("<tt><body><p begin=\"1\" end=\"2\">x<foo:b/></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 1, u8"<foo:b/>"}));
    EXPECT_EQ(lines("<tt><body><p begin=\"1\" end=\"2\">a<span ttm:role=\"x-roman\">b</span></p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "a"}}));
    // Timing attributes are unprefixed: an extension's "begin" is not one.
    EXPECT_EQ(lines("<tt xmlns:foo=\"urn:x\"><body><p foo:begin=\"10\" begin=\"1\" end=\"2\">x</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    EXPECT_EQ(lines("<tt xmlns:foo=\"urn:x\"><body><p begin=\"1\" foo:end=\"0\" end=\"2\">x</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    // One attribute given twice under two prefixes for its namespace is
    // malformed XML, as one given twice by name is.
    EXPECT_EQ(problemOf("<tt xmlns:a=\"http://www.w3.org/ns/ttml#metadata\" xmlns:b=\"http://www.w3.org/ns/ttml#metadata\">"
                        "<body><p begin=\"1\" end=\"2\"><span a:role=\"x-translation\" b:role=\"x-bg\">x</span></p></body></tt>",
                        u8"ttml").kind,
              Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<tt xmlns:a=\"urn:x\" xmlns:b=\"urn:x\"><body><p begin=\"1\" end=\"2\" a:k=\"1\" b:k=\"2\">x</p>"
                        "</body></tt>", u8"ttml").kind,
              Kind::NotWellFormed);
    EXPECT_EQ(lines("<tt xmlns:a=\"urn:x\" xmlns:b=\"urn:y\"><body><p begin=\"1\" end=\"2\" a:k=\"1\" b:k=\"2\">x</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "x"}}));
    // ttm:role and ttm:agent by TTML's metadata namespace, under any prefix;
    // another namespace's "role" is not one.
    EXPECT_EQ(lines("<tt xmlns:m=\"http://www.w3.org/ns/ttml#metadata\"><body><p begin=\"1\" end=\"2\">a"
                    "<span m:role=\"x-translation\">b</span></p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "a"}}));
    EXPECT_EQ(lines("<tt xmlns:foo=\"urn:x\"><body><p begin=\"1\" end=\"2\">a <span foo:role=\"x-translation\">b</span>"
                    "</p></body></tt>"),
              (std::vector<Row>{{1000, 2000, "", "a b"}}));
    // Namespaces in XML's reserved prefixes: xml only for its own namespace,
    // never xmlns, neither namespace for another prefix or the default; no
    // empty namespace for a prefix, no element in the xmlns namespace.
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
    // A long namespace on many attributes is not copied for each of them.
    std::string many = "<tt xmlns:n=\"urn:" + std::string(100'000, 'x') + "\"><body><p begin=\"1\" end=\"2\"";
    for (int i = 0; i < 8'000; ++i)
        many += " n:a" + std::to_string(i) + "=\"\"";
    EXPECT_EQ(lines(many + ">x</p></body></tt>"), (std::vector<Row>{{1000, 2000, "", "x"}}));
    // Nor is every declaration searched for each one: thousands of prefixes
    // on tens of thousands of attributes read at once.
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
    // A background vocal inside a word's span is a Line of its own too.
    const auto nested = importTtml(bytes("<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"3\">lead"
                                         "<span ttm:role=\"x-bg\"><span begin=\"2\" end=\"3\">back</span></span></span>"
                                         "</p></body></tt>"));
    ASSERT_TRUE(nested.has_value());
    ASSERT_EQ(rows(*nested).size(), 2u);
    EXPECT_EQ(rows(*nested)[1].text, "{\\k100}back");
    // An untimed one takes the times of the span around it, not the paragraph's.
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
    // After a line's time a tag has a lowercase key (lyrsync's "class");
    // another bracket there is lyrics text.
    {
        const auto result = importLrc(bytes("[ti:Song][00:01.00][Verse: 1]Hello\n[00:03.00][class:shhh]Bye\n"
                                            "[00:05.00][Part:A]x\n[00:07.00]"));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(rows(*result), (std::vector<Row>{{1000, 3000, "", "[Verse: 1]Hello"}, {3000, 5000, "", "Bye"},
                                                   {5000, 7000, "", "[Part:A]x"}}));
    }
    // Spaces between words stay as written, before or after the stamp.
    for (const char *line : {"[00:01.00]<00:01.00>a<00:02.00>   b<00:03.00>", "[00:01.00]<00:01.00>a   <00:02.00>b<00:03.00>",
                             "[00:01.00]<00:01.00>a <00:02.00>  b<00:03.00>"}) {
        const auto result = importLrc(bytes(line));
        ASSERT_TRUE(result.has_value()) << line;
        EXPECT_EQ(rows(*result).front().text, "{\\k100}a   {\\k100}b") << line;
    }
    // A colon must have a digit after it to make a stamp.
    {
        const auto result = importLrc(bytes("[00:01.00]Meet me <12: tonight>\n[00:03.00]"));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(rows(*result), (std::vector<Row>{{1000, 3000, "", "Meet me <12: tonight>"}}));
    }
    // Only a stamp's shape (digits, then a colon) is a stamp.
    const auto plain = importLrc(bytes("[00:01.00]I <3 you, <12 roses>\n"));
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(rows(*plain).front().text, "I <3 you, <12 roses>");
    const auto words = importLrc(bytes("[00:01.00]<00:01.00>I <00:02.00><3\n"));
    ASSERT_TRUE(words.has_value());
    EXPECT_EQ(rows(*words).front().text, "{\\k100}I {\\k500}<3");
    // "[1st verse]" is lyrics with no time, like "[Chorus]".
    EXPECT_EQ(problemOf("[00:01.00]ok\n[1st verse]\n", u8"lrc"), (LyricsProblem{Kind::MissingTime, 2, u8"[1st verse]"}));
}

TEST(LyricsImport, LrcOffsets)
{
    // An empty offset is none; one with spaces and a sign reads.
    const auto none = importLrc(bytes("[offset:]\n[00:01.00]x\n"));
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ(rows(*none).front().start, 1000);
    const auto sooner = importLrc(bytes("[offset: +250 ]\n[00:01.00]x\n"));
    ASSERT_TRUE(sooner.has_value());
    EXPECT_EQ(rows(*sooner).front().start, 750);
}

TEST(LyricsImport, LrcTimeForms)
{
    // mm:ss, mm:ss.f, mm:ss:ff and h:mm:ss.ff all read.
    const auto result = importLrc(bytes("[00:01]a\n[00:02.5]b\n[00:03:25]c\n[0:00:04.75]d\n"));
    ASSERT_TRUE(result.has_value());
    const auto r = rows(*result);
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0].start, 1000);
    EXPECT_EQ(r[1].start, 2500);
    EXPECT_EQ(r[2].start, 3250);
    EXPECT_EQ(r[3].start, 4750);
}
