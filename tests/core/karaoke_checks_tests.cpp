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

}

TEST(KaraokeChecks, EachCheckFindsItsLineInTheSample)
{
    const std::vector<Found> expected{
        {2, Kind::Unclosed, 0, 0, {}},
        {3, Kind::Unclosed, 0, 0, {}},
        {4, Kind::TotalNotDuration, 2500, 3000, {}},
        {5, Kind::ZeroLength, 0, 0, {2}},
        {6, Kind::TotalNotDuration, 1300, 1000, {}},
        {6, Kind::OutsideLine, 0, 0, {2}},
        {7, Kind::OutsideLine, 0, 0, {1, 2}},
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
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}one {\\k20 two")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
}

TEST(KaraokeChecks, SyllablesAddUpInCentiseconds)
{
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.50", "{\\k50\\k50}a{\\kf50}b")).problems.empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.50", "{\\k50}a{\\k50}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 1000, 1500, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\k0}")),
              (std::vector<Found>{{0, Kind::ZeroLength, 0, 0, {2}}}));
    {
        Document document = oneLine("0:00:00.00,0:00:01.00", "{\\k100}a");
        ASSERT_TRUE(document.editLine(document.lines().front()->id, [](LineRecord &line) {
            line.start.value = DocumentTime(999);
            line.end.value = DocumentTime(1'001'000);
        }));
        EXPECT_EQ(found(document), (std::vector<Found>{{0, Kind::TotalNotDuration, 1000, 1001, {}}}));
    }
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k80}a{\\k30}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 1100, 1000, {}}, {0, Kind::OutsideLine, 0, 0, {2}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k50}a{\\k-20}b{\\k70}c")),
              (std::vector<Found>{{0, Kind::OutsideLine, 0, 0, {2}}}));
}

TEST(KaraokeChecks, ALineOfManySyllablesListsTheFirstOnes)
{
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
    std::string ass = "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (int i = 0; i < 3'000; ++i)
        ass += "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\k0}a\n";
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
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+50}a{\\kf+50}b")), (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k99999999999999999999999}a{\\k99999999999999999999999}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 20'000'000'000, 1000, {}}, {0, Kind::OutsideLine, 0, 0, {1, 2}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k(50)}a{\\kf(+50)}b")), (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k( 50)}a{\\kf(\t +50)}b")), (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k(  )}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k50.5}a{\\K49.5}b")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 990, 1000, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k12.34}a")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 120, 1000, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k(}a{\\k100}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\keep}text {\\kfoo\\Kx}more")).problems.empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k50\\keep}a{\\k50}b")),
              (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}}));
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\keep unclosed")).problems.empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\ko\\i1}b")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+100}a{\\kf")),
              (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}, {0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+100}a{\\kfo")),
              (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
}

TEST(KaraokeChecks, TagsOnlyTheAudioEditorReadsAreFlagged)
{
    for (const char *text : {"{\\kfo50}x", "{\\KF50}x", "{\\kO50}x", "{\\KFO50}x"})
        EXPECT_EQ(found(oneLine("0:00:00.00,0:00:00.50", text)), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}})) << text;
    // QChar::toLower maps the Kelvin sign U+212A to k.
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:00.50", "{\\\xE2\x84\xAA" "50}x")), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}}));
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:00.50", "{\\k+50}a{\\\xE2\x84\xAA" "50}b")).problems.empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\\xE2\x84\xAA}x \xE2\x84\xAA" "50")).problems.empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k100}a{\\kfo50}b")), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}}));
    for (const char *text : {"{\\k100}a{\\keep}b", "{\\k50}a{\\kt50}b", "{\\k50}a{\\k(50)}b", "{\\k50}a{\\k 50}b",
                             "{\\k50}a{\\KF50}b", "{\\k100}a{\\keep"})
        EXPECT_TRUE(std::ranges::any_of(found(oneLine("0:00:00.00,0:00:01.00", text)),
                                        [](const Found &f) { return f.kind == Kind::EditorOnly; }))
            << text;
    for (const char *text : {"{\\k100}a{\\k}b", "{\\k100}a{\\k50"})
        EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", text)), (std::vector<Found>{{0, Kind::Unclosed, 0, 0, {}}})) << text;
    for (const char *text : {"{\\k50}a{\\k+50}b", "{\\k50}a{\\k50.9}b", "{\\k50}a{\\k50\\i1}b", "{\\k50}a{\\kf50}b"})
        EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", text)).problems.empty()) << text;
    for (const char *text : {"{\\k50}a{\\K50}b", "{\\kf50}a{\\ko50}b", "{\\kfoo}x", "{\\kfo}x", "{\\kfo50x"})
        EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", text)).problems.empty()) << text;
}

TEST(KaraokeChecks, LinesTheAudioEditorCannotReadAreFlagged)
{
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\k+50}a{\\K50}b")).problems.empty());
    for (const char *text : {"{\\k(50)}a{\\k50}b", "{\\K(50)}a{\\kf50}b", "{\\i1\\ko50}a{\\k(50)}b"})
        EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", text)), (std::vector<Found>{{0, Kind::EditorOnly, 0, 0, {}}})) << text;
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:01.00", "{\\k+1}a{\\k(99)}b")),
              (std::vector<Found>{{0, Kind::NotForAudioEditor, 0, 0, {}}}));
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.00", "{\\i1}plain")).problems.empty());
}

TEST(KaraokeChecks, TranslatedLinesAreCheckedOnTheirTranslation)
{
    const std::string ass = "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                            "Comment: 0,0:00:00.00,0:00:01.00,O,,0,0,0,,{\\k0}orig\n"
                            "Dialogue: 0,0:00:00.00,0:00:01.00,T,,0,0,0,,{\\k50}tr{\\k40}ans\n";
    const Document document = loadAss(bytes(ass)).document;
    ASSERT_EQ(document.lines().size(), 1u);
    ASSERT_FALSE(document.lines()[0]->translation.empty());
    EXPECT_EQ(found(document), (std::vector<Found>{{0, Kind::TotalNotDuration, 900, 1000, {}}}));
}

TEST(KaraokeChecks, MultiHighlightHashSyllablesAreSyllables)
{
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:01.15",
                                     "{\\k15}外{\\k16}道{\\k13}攻{\\k9}撃、{\\k7}#{\\k6}腐{\\k9}#{\\k9}#{\\k12}蝕{\\k12}攻{\\k7}撃"))
                    .problems.empty());
    EXPECT_TRUE(checkKaraoke(oneLine("0:00:00.00,0:00:00.30", "{\\i1\\kf20}撃{\\K10}＃")).problems.empty());
    EXPECT_EQ(found(oneLine("0:00:00.00,0:00:00.30", "{\\k20}撃{\\k0}#")),
              (std::vector<Found>{{0, Kind::TotalNotDuration, 200, 300, {}}, {0, Kind::ZeroLength, 0, 0, {2}}}));
}
