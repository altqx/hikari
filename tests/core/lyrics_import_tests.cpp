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
    EXPECT_EQ(problemOf(nested(100000), u8"ttml"), (LyricsProblem{Kind::TooDeep, 3, u8"<div>"}));
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
