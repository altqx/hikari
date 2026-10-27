// KA7: the karaoke checks, on the sample file tests/fixtures/karaoke/checks.ass
// (one Line per check) and on single Lines.

#include "hikari/core/ass_load.h"
#include "hikari/core/karaoke_checks.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari::core;
using Kind = KaraokeProblem::Kind;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
        out[i] = static_cast<std::byte>(text[i]);
    return out;
}

Document sample()
{
    std::ifstream in(HIKARI_KARAOKE_FIXTURE, std::ios::binary);
    EXPECT_TRUE(in);
    return loadAss(bytes(std::string(std::istreambuf_iterator<char>(in), {}))).document;
}

Document oneLine(std::string_view times, std::string_view text, std::string_view effect = {})
{
    std::string ass = "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                      "Dialogue: 0,";
    ass += times;
    ass += ",Default,,0,0,0,";
    ass += effect;
    ass += ",";
    ass += text;
    ass += "\n";
    return loadAss(bytes(ass)).document;
}

// What a problem says, without the Line id.
struct Found {
    std::size_t row = 0;
    Kind kind = Kind::Unclosed;
    std::int64_t totalMs = 0, durationMs = 0;
    std::vector<std::size_t> syllables;
    bool operator==(const Found &) const = default;
};

void PrintTo(const Found &f, std::ostream *os)
{
    *os << "row " << f.row << " kind " << int(f.kind) << " " << f.totalMs << "/" << f.durationMs << " syllables";
    for (auto s : f.syllables)
        *os << " " << s;
}

std::vector<Found> found(const Document &document)
{
    std::vector<Found> out;
    const auto lines = document.lines();
    for (const KaraokeProblem &p : checkKaraoke(document).problems) {
        EXPECT_EQ(p.line, lines.at(p.row)->id);
        out.push_back({p.row, p.kind, p.totalMs, p.durationMs, p.syllables});
    }
    return out;
}

} // namespace

TEST(KaraokeChecks, EachCheckFindsItsLineInTheSample)
{
    const std::vector<Found> expected{
        // Row 0, a template Line, and row 1, a sound one: nothing.
        {2, Kind::Unclosed, 0, 0, {}},                 // "{\k100tle": the block never closes
        {3, Kind::Unclosed, 0, 0, {}},                 // "{\k}": a tag with no number
        {4, Kind::TotalNotDuration, 2500, 3000, {}},   // \k, \kf and \ko add up to 2.50 s of 3 s
        {5, Kind::ZeroLength, 0, 0, {2}},              // "{\K0}"
        {6, Kind::TotalNotDuration, 1300, 1000, {}},   // the second syllable starts at the end...
        {6, Kind::OutsideLine, 0, 0, {2}},             // ...so it is outside the Line too
        {7, Kind::OutsideLine, 0, 0, {1, 2}},          // "{\k-20}": both start before the Line
        // Rows 8-10: a timed pause, a plain Line and other tags beside \k: nothing.
    };
    EXPECT_EQ(found(sample()), expected);
}

TEST(KaraokeChecks, TemplateCodeAndMixinLinesAreSkipped)
{
    for (const char *effect : {"template line", "Template pre-line all", "code once", "mixin syl", "  template"})
        EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k0}", effect)).problems.empty()) << effect;
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k0}{\\k100}", "karaoke")),
              (std::vector<Found>{{0, Kind::ZeroLength, 0, 0, {1}}}));
}

// The templater's output, Effect "fx": Classic fill's Lines last longer than their syllables.
TEST(KaraokeChecks, GeneratedFxLinesAreSkipped)
{
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.60", "{\\k30}{\\kf50}a{\\kf50}b", "fx")).problems.empty());
    for (const char *effect : {"fx2", "Fx", "karaoke"})
        EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.60", "{\\k30}{\\kf50}a{\\kf50}b", effect)),
                  (std::vector<Found>{{0, Kind::TotalNotDuration, 1300, 1600, {}}}))
            << effect;
}

TEST(KaraokeChecks, OnlyKaraokeLinesAreChecked)
{
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\i1 no karaoke here")).problems.empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "plain \\k50 outside a block")).problems.empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\kt50}absolute time only")).problems.empty());
    // An unclosed "{" after the syllables: its \k is literal text.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}one {\\k20 two")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
}

TEST(KaraokeChecks, SyllablesAddUpInCentiseconds)
{
    // Several tags in one block are each a syllable.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.50", "{\\k50\\k50}a{\\kf50}b")).problems.empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.50", "{\\k50}a{\\k50}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 1000, 1500, {}}}));
    // A syllable ending exactly at the Line's end is inside; a zero-length
    // one at the end is only zero-length.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\k0}")),
              (std::vector<Found>{{0, Kind::ZeroLength, 0, 0, {2}}}));
    // Each end is taken in whole milliseconds before their distance, as the
    // audio display's karaoke takes them: 999 us to 1.001 s is 0 to 1001 ms.
    {
        Document document = oneLine("0:00:00.00,0:00:01.00", "{\\k100}a");
        ASSERT_TRUE(document.editLine(document.lines().front()->id, [](LineRecord &line) {
            line.start.value = DocumentTime(999);
            line.end.value = DocumentTime(1'001'000);
        }));
        EXPECT_EQ(found(document), (std::vector<Found>{{0, Kind::TotalNotDuration, 1000, 1001, {}}}));
    }
    // A syllable that starts inside the Line and ends after it is outside it.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k80}a{\\k30}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 1100, 1000, {}}, {0, Kind::OutsideLine, 0, 0, {2}}}));
    // A syllable that runs backward is outside the Line, even when its ends
    // stay inside and the total still matches.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k50}a{\\k-20}b{\\k70}c")),
              (std::vector<Found>{{0, Kind::OutsideLine, 0, 0, {2}}}));
}

TEST(KaraokeChecks, ALineOfManySyllablesListsTheFirstOnes)
{
    // A problem lists the first kListedSyllables syllables and counts them all,
    // so a Line of a great many tags makes one short row.
    std::string text;
    for (int i = 0; i < 200'000; ++i)
        text += "{\\k0}x";
    const auto problems = checkKaraoke(oneLine("0:00:00.00,0:00:01.00", text.c_str())).problems;
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_EQ(problems[0].kind, Kind::TotalNotDuration);
    EXPECT_EQ(problems[1].kind, Kind::ZeroLength);
    EXPECT_EQ(problems[1].syllables.size(), kListedSyllables);
    EXPECT_EQ(problems[1].syllables.front(), 1u);
    EXPECT_EQ(problems[1].syllables.back(), kListedSyllables);
    EXPECT_EQ(problems[1].syllableCount, 200'000u);
}

TEST(KaraokeChecks, ADocumentOfManyBadLinesListsTheFirstProblems)
{
    // The first kListedProblems are listed and the rest counted, so a
    // Document of a great many bad Lines makes a short list.
    std::string ass = "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (int i = 0; i < 3'000; ++i)
        ass += "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\k0}a\n"; // two problems each
    const KaraokeCheck check = checkKaraoke(loadAss(bytes(ass)).document);
    ASSERT_EQ(check.problems.size(), kListedProblems);
    EXPECT_EQ(check.unlisted, 6'000u - kListedProblems);
    EXPECT_EQ(check.problems.back().row, kListedProblems / 2 - 1);
    const KaraokeCheck all = checkKaraoke(loadAss(bytes(ass)).document, 10'000);
    EXPECT_EQ(all.problems.size(), 6'000u);
    EXPECT_EQ(all.unlisted, 0u);
}

TEST(KaraokeChecks, SignedAndOversizedValuesRead)
{
    // "+" may lead the number, as "-" may (though the audio editor reads
    // neither).
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+50}a{\\kf+50}b")), (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    // A huge value counts as 10^9 centiseconds, so the sum cannot overflow.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k99999999999999999999999}a{\\k99999999999999999999999}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 20'000'000'000, 1000, {}}, {0, Kind::OutsideLine, 0, 0, {1, 2}}}));
    // Parentheses as the tag parser reads them; a fraction is dropped, as the
    // karaoke the application plays drops it (50.5 + 49.5 is 99 cs).
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k(50)}a{\\kf(+50)}b")), (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    // Inside parentheses, whitespace before the number is skipped, as atoi
    // skips it; outside them a value starts at once.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k( 50)}a{\\kf(\t +50)}b")), (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k(  )}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k50.5}a{\\K49.5}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 990, 1000, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k12.34}a")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 120, 1000, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k(}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    // Other tags that start with k are not karaoke; a bare "\\k" or "\\ko" is
    // one with no number.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\keep}text {\\kfoo\\Kx}more")).problems.empty());
    // Where the editor reads karaoke it splits at "\\keep" too (TagsOnly...).
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k50\\keep}a{\\k50}b")),
              (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}}));
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\keep unclosed")).problems.empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\ko\\i1}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    // The text ending in a tag's name, after a tag the editor cannot read:
    // "\\kf" is a tag with no number, "\\kfo" no karaoke tag at all.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+100}a{\\kf")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}, {0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+100}a{\\kfo")),
              (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
}

TEST(KaraokeChecks, TagsOnlyTheAudioEditorReadsAreFlagged)
{
    // The editor reads \\kf?o?[0-9] on the lower-cased text, so "\\kfo50",
    // "\\KF50" and "\\kO50" are syllables to it; they play as no karaoke.
    for (const char *text : {"{\\kfo50}x", "{\\KF50}x", "{\\kO50}x", "{\\KFO50}x"})
        EXPECT_EQ(found(oneLine("0:00:00.00,0:00:00.50", text)), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}})) << text;
    // Its test lower-cases as QChar does, so a Kelvin sign for the K makes
    // it read the Line as karaoke; its split reads "\\k" and "\\K" as
    // written, so beside a tag it splits at the Line is as playback's.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:00.50", "{\\\xE2\x84\xAA" "50}x")), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}}));
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:00.50", "{\\k+50}a{\\\xE2\x84\xAA" "50}b")).problems.empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\\xE2\x84\xAA}x \xE2\x84\xAA" "50")).problems.empty());
    // Beside real syllables, the editor splits where playback does not.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\kfo50}b")), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}}));
    // Once it reads the Line as karaoke, it splits at every "\\k" or "\\K"
    // in a block and reads the number with atoi up to the next "}" or "\\":
    // a tag playback skips is a syllable to it, and a number in parentheses
    // is 0 to it (playback reads "\\k(50)" as 50).
    for (const char *text : {"{\\k100}a{\\keep}b", "{\\k50}a{\\kt50}b", "{\\k50}a{\\k(50)}b", "{\\k50}a{\\k 50}b",
                             "{\\k50}a{\\KF50}b", "{\\k100}a{\\keep"})
        EXPECT_TRUE(std::ranges::any_of(found(oneLine("0:00:00.00,0:00:01.00", text)),
                                        [](const Found &f) { return f.kind == Kind::EditorOnly; }))
            << text;
    // A tag with no number, or one in a block that never closes, is left to
    // Unclosed.
    for (const char *text : {"{\\k100}a{\\k}b", "{\\k100}a{\\k50"})
        EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", text)), (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}})) << text;
    // Numbers both read alike: a sign, a fraction playback drops, text after.
    for (const char *text : {"{\\k50}a{\\k+50}b", "{\\k50}a{\\k50.9}b", "{\\k50}a{\\k50\\i1}b", "{\\k50}a{\\kf50}b"})
        EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", text)).problems.empty()) << text;
    // Tags both read, and ones neither reads, are not.
    for (const char *text : {"{\\k50}a{\\K50}b", "{\\kf50}a{\\ko50}b", "{\\kfoo}x", "{\\kfo}x", "{\\kfo50x"})
        EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", text)).problems.empty()) << text;
}

TEST(KaraokeChecks, LinesTheAudioEditorCannotReadAreFlagged)
{
    // The audio editor reads a Line as karaoke when one tag's number follows
    // it at once, in any case; it then reads every tag.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k+50}a{\\K50}b")).problems.empty());
    // Read, it takes "(50)" as 0 (atoi), so the 50 played is not the editor's.
    for (const char *text : {"{\\k(50)}a{\\k50}b", "{\\K(50)}a{\\kf50}b", "{\\i1\\ko50}a{\\k(50)}b"})
        EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", text)), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}})) << text;
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+1}a{\\k(99)}b")),
              (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    // A Line with no karaoke tag is not one the editor should read.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\i1}plain")).problems.empty());
}

TEST(KaraokeChecks, TranslatedLinesAreCheckedOnTheirTranslation)
{
    // Karaoke plays the translation, so its syllables are the ones checked.
    const std::string ass = "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                            "Comment: 0,0:00:00.00,0:00:01.00,O,,0,0,0,,{\\k0}orig\n"
                            "Dialogue: 0,0:00:00.00,0:00:01.00,T,,0,0,0,,{\\k50}tr{\\k40}ans\n";
    const Document document = loadAss(bytes(ass)).document;
    ASSERT_EQ(document.lines().size(), 1u);
    ASSERT_FALSE(document.lines()[0]->translation.empty());
    EXPECT_EQ(found(document), (std::vector<Found>{{0, Kind::TotalNotDuration, 900, 1000, {}}}));
}

// KA8 (#354): the Kanji timer's multi-highlight Lines. A # syllable is a
// syllable with its own time, so a kanji Line written with them has nothing
// to flag, the audio editor reads it, and a full-width ＃ is the same.
TEST(KaraokeChecks, MultiHighlightHashSyllablesAreSyllables)
{
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.15",
                                     "{\\k15}外{\\k16}道{\\k13}攻{\\k9}撃、{\\k7}#{\\k6}腐{\\k9}#{\\k9}#{\\k12}蝕{\\k12}攻{\\k7}撃"))
                    .problems.empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:00.30", "{\\i1\\kf20}撃{\\K10}＃")).problems.empty());
    // its time counts like any other's
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:00.30", "{\\k20}撃{\\k0}#")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 200, 300, {}}, {0, Kind::ZeroLength, 0, 0, {2}}}));
}
