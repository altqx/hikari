// KA6: word-timed LRC and TTML lyrics imported as karaoke Lines, from the
// sample files in tests/fixtures/lyrics (see the README there).

#include "hikari/core/lyrics_import.h"

#include <gtest/gtest.h>

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
    EXPECT_EQ(problemOf(head + "<!-- open\n</body></tt>", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 3, u8"<!-- open"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=2>x</p>\n</body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotWellFormed, 3, u8"end=2>"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">x</p>\n", u8"ttml"), (LyricsProblem{Kind::NotWellFormed, 4, u8""}));
    EXPECT_EQ(problemOf("<html>\n<body/></html>", u8"ttml"), (LyricsProblem{Kind::NotTtml, 1, u8"<html>"}));
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
              (LyricsProblem{Kind::EndBeforeBegin, 4, u8"2"}));
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
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"1\" end=\"2\" x:l\xC3\xA9-g\xC2\xB7.1=\"1\"><\xE6\x97\xA5\xE6\x9C\xAC/>x" + tail))
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
        ttml += "<p agent=\"a\" begin=\"1\" end=\"2\">x</p>\n";
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

TEST(LyricsImport, TtmlTimesPast100HoursAreRefused)
{
    const std::string head = "<tt><body>\n";
    EXPECT_EQ(problemOf(head + "<p begin=\"101h\" end=\"102h\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 2, u8"101h"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"999999999999999\">x</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::UnreadableTime, 2, u8"999999999999999"}));
    EXPECT_TRUE(importTtml(bytes(head + "<p begin=\"99h\" end=\"100h\">x</p></body></tt>")).has_value());
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
    EXPECT_EQ(problemOf("<!DOCTYPE tt ]>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE tt []]>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE tt [] x>" + root, u8"ttml").kind, Kind::NotWellFormed);
    EXPECT_EQ(problemOf("<!DOCTYPE tt [][]>" + root, u8"ttml").kind, Kind::NotWellFormed);
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
    EXPECT_EQ(timed("<div begin=\"1\" end=\"3\"><p begin=\"2\">a</p><p end=\"2\">b</p></div>"),
              (std::vector<Row>{{2000, 3000, "", "a"}, {1000, 2000, "", "b"}}));
    // A word-timed Line spans its words; the ancestor ends its last word.
    EXPECT_EQ(timed("<div begin=\"0\" end=\"4\"><p><span begin=\"1\" end=\"2\">a</span> <span begin=\"2\">b</span></p></div>"),
              (std::vector<Row>{{1000, 4000, "", "{\\k100}a {\\k200}b"}}));
    // Its own begin after the end it inherits; no times anywhere.
    EXPECT_EQ(problemOf("<tt><body>\n<div begin=\"1\" end=\"3\">\n<p begin=\"5\">x</p></div></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::EndBeforeBegin, 3, u8"5"}));
    EXPECT_EQ(problemOf("<tt><body><div><p>x</p></div></body></tt>", u8"ttml").kind, Kind::MissingTime);
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
    // A paragraph's own text still joins its first word as written.
    EXPECT_EQ(text("<p begin=\"1\" end=\"3\">hel<span begin=\"2\" end=\"3\">lo</span></p>"), "{\\k100}{\\k100}hello");
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
    EXPECT_EQ(text("><p begin=\"1\" end=\"2\" x:space=\"preserve\">a  b</p>"), "a b");
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
    const auto nested = importTtml(bytes("<tt><body><p begin=\"1\" end=\"3\"><span begin=\"1\" end=\"2\">lead"
                                         "<span ttm:role=\"x-bg\"><span begin=\"2\" end=\"3\">back</span></span></span>"
                                         "</p></body></tt>"));
    ASSERT_TRUE(nested.has_value());
    ASSERT_EQ(rows(*nested).size(), 2u);
    EXPECT_EQ(rows(*nested)[1].text, "{\\k100}back");
}

TEST(LyricsImport, LrcAngleAndSquareBracketsThatAreNotStamps)
{
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
