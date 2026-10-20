// KA7: the karaoke checks, on the sample file tests/fixtures/karaoke/checks.ass
// (one Line per check) and on single Lines.

#include "hikari/core/ass_load.h"
#include "hikari/core/karaoke_checks.h"

#include <gtest/gtest.h>

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
    for (const KaraokeProblem &p : checkKaraoke(document)) {
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
        EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k0}", effect)).empty()) << effect;
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k0}{\\k100}", "karaoke")),
              (std::vector<Found>{{0, Kind::ZeroLength, 0, 0, {1}}}));
}

TEST(KaraokeChecks, OnlyKaraokeLinesAreChecked)
{
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\i1 no karaoke here")).empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "plain \\k50 outside a block")).empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\kt50}absolute time only")).empty());
    // An unclosed "{" after the syllables: its \k is literal text.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}one {\\k20 two")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
}

TEST(KaraokeChecks, SyllablesAddUpInCentiseconds)
{
    // Several tags in one block are each a syllable.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.50", "{\\k50\\k50}a{\\kf50}b")).empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.50", "{\\k50}a{\\k50}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 1000, 1500, {}}}));
    // A syllable ending exactly at the Line's end is inside; a zero-length
    // one at the end is only zero-length.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\k0}")),
              (std::vector<Found>{{0, Kind::ZeroLength, 0, 0, {2}}}));
}

TEST(KaraokeChecks, SignedAndOversizedValuesRead)
{
    // "+" may lead the number, as "-" may.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k+50}a{\\kf+50}b")).empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    // A huge value counts as 10^9 centiseconds, so the sum cannot overflow.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k99999999999999999999999}a{\\k99999999999999999999999}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 20'000'000'000, 1000, {}}, {0, Kind::OutsideLine, 0, 0, {2}}}));
    // Parentheses and fractions, as the tag parser reads them.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k(50)}a{\\kf(+50)}b")).empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k50.5}a{\\K49.5}b")).empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k12.34}a")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 123, 1000, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k(}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    // Other tags that start with k are not karaoke; a bare "\\k" or "\\ko" is
    // one with no number.
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\keep}text {\\kfoo\\Kx}more")).empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k50\\keep}a{\\k50}b")).empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\keep unclosed")).empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\ko\\i1}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
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
