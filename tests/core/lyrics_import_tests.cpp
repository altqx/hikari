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
    EXPECT_EQ(problemOf(head + "<p>no time</p></body></tt>", u8"ttml"), (LyricsProblem{Kind::MissingTime, 3, u8"no time"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\"><span begin=\"1\">a</span></p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::MissingTime, 3, u8"a"}));
    EXPECT_EQ(problemOf(head + "<p begin=\"1\" end=\"2\">\xc3\x28</p></body></tt>", u8"ttml"),
              (LyricsProblem{Kind::NotUtf8, 3, u8""}));
    EXPECT_EQ(problemOf(head + "</body></tt>", u8"ttml"), (LyricsProblem{Kind::NoLyrics, 0, u8""}));
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
