#include "hikari/application/misspell_replacer.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;
using L = ReplacerScope::Lines;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return b;
}

constexpr std::string_view kHeader =
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

std::string dialogue(std::string_view text, std::string_view style = "Default", std::string_view actor = "")
{
    return "Dialogue: 0,0:00:01.00,0:00:02.00," + std::string(style) + "," + std::string(actor) + ",0,0,0,," +
           std::string(text) + "\n";
}

core::Document load(const std::vector<std::string> &lines)
{
    std::string text(kHeader);
    for (const auto &l : lines)
        text += l;
    return core::loadAss(bytes(text)).document;
}

std::u8string u8(std::string_view s)
{
    return std::u8string(s.begin(), s.end());
}

std::string str(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

std::string str(std::u16string_view s)
{
    return str(core::toUtf8(s));
}

std::u16string u16(std::string_view s)
{
    return core::toUtf16(u8(s));
}

ReplacerRule rule(std::string_view find, std::string_view replace, int options = 0, bool checked = true)
{
    return ReplacerRule{u8"", u8(find), u8(replace), options, checked};
}

std::vector<ReplacerRule> shipped(std::initializer_list<int> checked)
{
    auto rules = readReplacerRules({}).rules;
    for (const int i : checked)
        rules[static_cast<std::size_t>(i)].checked = true;
    return rules;
}

std::vector<std::string> texts(const EditSession &session)
{
    std::vector<std::string> out;
    for (const auto *l : session.document().lines())
        out.push_back(str(l->text));
    return out;
}

const ReplacerCase kPolish{
    [](char16_t c) { return (c >= u'A' && c <= u'Z') || std::u16string_view(u"ĄĆĘŁŃÓŚŹŻ").find(c) != std::u16string_view::npos; },
    [](char16_t c) -> char16_t {
        const std::u16string_view up = u"ĄĆĘŁŃÓŚŹŻ", low = u"ąćęłńóśźż";
        if (const auto i = up.find(c); i != up.npos)
            return low[i];
        return c >= u'A' && c <= u'Z' ? char16_t(c + 32) : c;
    },
    [](char16_t c) -> char16_t {
        const std::u16string_view up = u"ĄĆĘŁŃÓŚŹŻ", low = u"ąćęłńóśźż";
        if (const auto i = low.find(c); i != low.npos)
            return up[i];
        return c >= u'a' && c <= u'z' ? char16_t(c - 32) : c;
    }};

std::string fix(std::string_view text, const std::vector<ReplacerRule> &rules, const ReplacerCase &cases = kPolish)
{
    EditSession session{load({dialogue(text)})};
    const auto r = replaceErrors(session, rules, {}, cases);
    EXPECT_TRUE(r.has_value());
    return texts(session)[0];
}

}

TEST(MisspellRules, ShippedRulesInLegacyOrder)
{
    const auto read = readReplacerRules({});
    ASSERT_EQ(read.rules.size(), 13u);
    EXPECT_TRUE(read.invalid.empty());
    const std::vector<std::pair<std::string, std::string>> expected{
        {"Remove space before comma or dot", " ([,.!?%])"},
        {"Remove doubled spaces", "(  +)"},
        {"Replace more than three dots with a suspension point",
         "\\.{4,}"},
        {"Replace two dots to suspension point", "([^.])\\.\\.([^.])"},
        {"Replace missing spaces after dot or comma", "([^.])([,.!?%])([^ ,.!?%\\\"\\\\0-9-])"},
        {"Removing japanese suffixes", " ?- ?(san|chan|kun|sama|nee|dono|senpai|sensei)\\M"},
        {"Fixing Polish \"sie\"", "\\msie\\M"},
        {"Fixing Polish \"nie mozliwe\"", "\\mnie możliwe\\M"},
        {"Fixing Polish \"nie wazne\"", "\\mnie ważne\\M"},
        {"Fixing Polish \"w ogole\"", "\\mw ?og[uo]le\\M"},
        {"Fixing Polish \"w ogole\"", "\\mwogóle\\M"},
        {"Fixing Polish \"bede\"", "\\mbed[eę]\\M"},
        {"Fixing Polish \"bede\"", "\\mbęde\\M"},
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(str(read.rules[i].description), expected[i].first) << i;
        EXPECT_EQ(str(read.rules[i].find), expected[i].second) << i;
        EXPECT_EQ(read.rules[i].options, 0) << i;
        EXPECT_FALSE(read.rules[i].checked) << i;
    }
    EXPECT_EQ(str(read.rules[5].replace), "");
    EXPECT_EQ(str(read.rules[6].replace), "się");
    for (const auto &r : read.rules)
        EXPECT_TRUE(compileReplacerRule(r).isValid()) << str(r.find);
}

TEST(MisspellRules, FileRoundTrip)
{
    std::vector<ReplacerRule> rules{{u8"A", u8"a+", u8"b", 3, true}, {u8"B", u8"x", u8"", 0, false},
                                    {u8"C", u8"y", u8"z", 48, true}};
    const auto file = writeReplacerRules(rules);
    EXPECT_EQ(str(file), "#HikariSub rules file\r\n1|0|1\r\nA\fa+\fb\f3\r\nB\fx\f\f0\r\nC\fy\fz\f48\r\n");
    std::u8string folded;
    for (std::size_t i = 0; i < file.size(); ++i)
        if (!(file[i] == u8'\r' && i + 1 < file.size() && file[i + 1] == u8'\n'))
            folded += file[i];
    const auto read = readReplacerRules(folded, RulesReadBy::Windows);
    EXPECT_EQ(read.rules, rules);
    EXPECT_TRUE(read.invalid.empty());
    const auto kept = readReplacerRules(file, RulesReadBy::Linux);
    EXPECT_EQ(kept.rules, rules);
    EXPECT_TRUE(kept.invalid.empty());
    EXPECT_EQ(readReplacerRules(file, RulesReadBy::Windows).rules[2], (ReplacerRule{u8"C", u8"y", u8"z", 48, false}));
    const auto blank = readReplacerRules(u8"#HikariSub rules file\r\n1|1\r\n\r\nA\fa\fb\f0\r\n", RulesReadBy::Linux);
    ASSERT_EQ(blank.rules.size(), 2u);
    EXPECT_EQ(blank.invalid, (std::vector<std::u8string>{u8"\r"}));
    EXPECT_EQ(blank.rules[0], (ReplacerRule{u8"\r", u8"", u8"", 0, true}));
    EXPECT_EQ(blank.rules[1], (ReplacerRule{u8"A", u8"a", u8"b", 0, true}));
    const auto bare = readReplacerRules(u8"1\n\nD\fd\fe\f1\nE\fe\ff\f0\n");
    ASSERT_EQ(bare.rules.size(), 2u);
    EXPECT_TRUE(bare.rules[0].checked);
    EXPECT_FALSE(bare.rules[1].checked);
    const auto broken = readReplacerRules(u8"#HikariSub rules file\n1|1\nF\ff\fg\nG\n");
    ASSERT_EQ(broken.rules.size(), 2u);
    EXPECT_EQ(broken.invalid, (std::vector<std::u8string>{u8"F\ff\fg", u8"G"}));
    EXPECT_EQ(broken.rules[0], (ReplacerRule{u8"F", u8"f", u8"g", 0, true}));
    EXPECT_EQ(broken.rules[1], (ReplacerRule{u8"G", u8"", u8"", 0, true}));
    const auto odd = readReplacerRules(u8"#HikariSub rules file\n2|1\nH\fh\fi\f 17x\nI\fi\fj\f0\n");
    EXPECT_EQ(odd.rules[0].options, 17);
    EXPECT_FALSE(odd.rules[0].checked);
    EXPECT_TRUE(odd.rules[1].checked);
    EXPECT_TRUE(readReplacerRules(u8"#HikariSub rules file\r\n").rules.empty());
    EXPECT_EQ(readReplacerRules(u8"").rules.size(), 13u);
    EXPECT_EQ(str(writeReplacerRules({})), "#HikariSub rules file\r\n");
}

TEST(MisspellRules, ShippedRuleFixtures)
{
    EXPECT_EQ(fix("Hello , world .", shipped({0})), "Hello, world.");
    EXPECT_EQ(fix("a   b  c", shipped({1})), "a b c");
    EXPECT_EQ(fix("Wait..... no....", shipped({2})), "Wait... no...");
    EXPECT_EQ(fix("So..what", shipped({3})), "So...what");
    EXPECT_EQ(fix("Hi.there,you", shipped({4})), "Hi. there, you");
    EXPECT_EQ(fix("Hello,World", shipped({4})), "HellO, World");
    EXPECT_EQ(fix("Naruto-kun and Kakashi - sensei, Sakura-chan", shipped({5})), "Naruto and Kakashi, Sakura");
    EXPECT_EQ(fix("Naruto-KUN!", shipped({5})), "Naruto!");
    EXPECT_EQ(fix("Nie wiem, sie zobaczy. Sie. SIE", shipped({6})), "Nie wiem, się zobaczy. Się. SIĘ");
    // PCRE2 without UCP: word boundaries see ASCII only.
    EXPECT_EQ(fix("sieć siew", shipped({6})), "sięć siew");
    EXPECT_EQ(fix("to nie możliwe", shipped({7})), "to niemożliwe");
    EXPECT_EQ(fix("nie ważne", shipped({8})), "nieważne");
    EXPECT_EQ(fix("wogole i w ogule", shipped({9})), "w ogóle i w ogóle");
    EXPECT_EQ(fix("wogóle", shipped({10})), "w ogóle");
    EXPECT_EQ(fix("bede tu", shipped({11})), "będę tu");
    EXPECT_EQ(fix("bedę tu", shipped({11})), "bedę tu");
    EXPECT_EQ(fix("Będe", shipped({12})), "Będę");
}

TEST(MisspellRules, LaterRulesSearchTheChangedText)
{
    EXPECT_EQ(fix("a  b XY", {rule("(  +)", " "), rule("XY", "z")}), "a b Z");
    EXPECT_EQ(fix("a  b XY", {rule("XY", "z"), rule("(  +)", " ")}), "a b Z");
    EXPECT_EQ(fix("aaac", {rule("a", ""), rule("c", "x")}), "x");
    EXPECT_EQ(fix("ab cd", {rule("a", "xyz"), rule("cd", "Q")}), "xyzb Q");
    const auto all = shipped({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    EXPECT_EQ(fix("Hello , ty sie  bede..ok", all), "Hello, ty się będę...ok");
    EditSession session{load({dialogue("Hello , ty sie  bede..ok")})};
    const auto finds = findErrors(session, all, {});
    ASSERT_EQ(finds.size(), 5u);
    ASSERT_TRUE(replaceFinds(session, all, finds, kPolish)->changed);
    EXPECT_EQ(texts(session)[0], "Hello, ty się będę...ok");
}

TEST(MisspellRules, MoveCase)
{
    EXPECT_EQ(str(moveCase(u"Ab", u"xyz", 0)), "Xyz");
    EXPECT_EQ(str(moveCase(u"aB", u"xyz", 0)), "Xyz");
    EXPECT_EQ(str(moveCase(u"AB", u"xyz", 0)), "XYZ");
    EXPECT_EQ(str(moveCase(u"ab", u"Xyz", 0)), "Xyz");
    EXPECT_EQ(str(moveCase(u"AB", u"xYz", kReplacerLowerCase)), "xyz");
    EXPECT_EQ(str(moveCase(u"ab", u"xyz", kReplacerUpperCase)), "XYZ");
    EXPECT_EQ(str(moveCase(u"AB", u"xyz", kReplacerUnchangedCase | kReplacerUpperCase)), "xyz");
    EXPECT_EQ(str(moveCase(u"", u"xyz", kReplacerUpperCase)), "xyz");
    EXPECT_EQ(str(moveCase(u"ab", u"xYz", kReplacerLowerCase | kReplacerUpperCase)), "xyz");
    EXPECT_EQ(str(moveCase(u"ĘĆ", u"ęć", 0, kPolish)), "ĘĆ");
}

TEST(MisspellRules, OnlyTagsOrText)
{
    EXPECT_TRUE(keepFinding(u"{\\b1}x", 4, kReplacerOnlyText));
    EXPECT_TRUE(keepFinding(u"{\\b1}x", 2, kReplacerOnlyTags));
    EXPECT_FALSE(keepFinding(u"{\\b1}x", 2, kReplacerOnlyText));
    EXPECT_TRUE(keepFinding(u"{\\b1}x", 0, kReplacerOnlyTags));
    EXPECT_TRUE(keepFinding(u"{ab", 3, kReplacerOnlyTags));
    EXPECT_TRUE(keepFinding(u"x", 0, kReplacerOnlyTags | kReplacerOnlyText));
    EXPECT_FALSE(keepFinding(u"x", 0, 64));
    EXPECT_EQ(fix("{\\b1}b1", {rule("b1", "X", kReplacerOnlyText)}), "{\\b1}X");
    EXPECT_EQ(fix("{\\b1}b1", {rule("b1", "b0", kReplacerOnlyTags)}), "{\\b0}b1");
    EXPECT_EQ(fix("b1", {rule("b1", "X", 64)}), "b1");
    EXPECT_EQ(fix("{b x}", {rule("b| x", "", kReplacerOnlyText)}), "{b x}");
    EditSession seek{load({dialogue("{b x}")})};
    EXPECT_TRUE(findErrors(seek, {rule("b| x", "", kReplacerOnlyText)}, {}).empty());
    EXPECT_EQ(fix("{\\bord2\\bord3}Hello", {rule("\\\\bord", "X", kReplacerOnlyText)}), "{\\bord2\\bord3}Hello");
    EXPECT_EQ(fix("{\\bord2\\bord3}Hello", {rule("\\\\bord", "B", kReplacerOnlyTags)}), "{B2B3}Hello");
    EditSession bord{load({dialogue("{\\bord2\\bord3}Hello")})};
    EXPECT_EQ(findErrors(bord, {rule("\\\\bord", "X", kReplacerOnlyTags)}, {}).size(), 2u);
}

TEST(MisspellRules, RegexReplaceSyntax)
{
    const auto compile = [](std::string_view find, int options = kReplacerMatchCase) {
        return compileReplacerRule(rule(find, "", options));
    };
    const auto groups = compile("(a)(b)");
    ASSERT_TRUE(groups.isValid());
    EXPECT_EQ(str(replacerReplace(groups, u"ab", u"\\2\\1&").first), "baab");
    EXPECT_EQ(str(replacerReplace(groups, u"ab", u"\\&\\\\x\\").first), "&\\x\\");
    EXPECT_EQ(str(replacerReplace(groups, u"ab", u"[\\3\\10]").first), "[]");
    EXPECT_EQ(replacerReplace(groups, u"abab", u"x"), (std::pair<std::u16string, int>{u"xx", 2}));
    EXPECT_EQ(replacerReplace(compile("^a"), u"aaa", u"X"), (std::pair<std::u16string, int>{u"Xaa", 1}));
    EXPECT_EQ(str(replacerReplace(compile("\\mab\\M|\\ycd\\Y", 0), u"AB xab cde", u"_").first), "_ xab _e");
    EXPECT_EQ(str(replacerReplace(compile("ę", 0), u"Ę", u"e").first), "e");
    EXPECT_EQ(replacerReplace(compile("ę"), u"Ę", u"e").second, 0);
    EXPECT_EQ(replacerReplace(compile("a(?=b)"), u"a", u"x"), (std::pair<std::u16string, int>{u"a", 0}));
    EXPECT_EQ(str(replacerReplace(compile("(?<=a)b"), u"ab cb", u"X").first), "aX cb");
    EXPECT_EQ(str(replacerReplace(compile("\\Qa.b\\E"), u"a.b axb", u"X").first), "X axb");
    EXPECT_EQ(str(replacerReplace(compile("\\Aa"), u"aa", u"X").first), "XX");
    EXPECT_EQ(replacerSearch(compile("a.b"), u"a\nb"), (std::optional<std::pair<std::size_t, std::size_t>>{{0, 3}}));
    EXPECT_FALSE(compile("(").isValid());
}

TEST(MisspellRules, InvalidExpressionsAreReported)
{
    const std::vector<ReplacerRule> rules{rule("(", "x"), rule("ok", "x"), rule("[a", "y", 0, false),
                                          rule("\\mab(", "z")};
    const auto checked = replacerRegexErrors(rules, true);
    ASSERT_EQ(checked.size(), 2u);
    EXPECT_EQ(str(checked[0].expression), "(");
    EXPECT_FALSE(checked[0].message.empty());
    EXPECT_EQ(str(checked[1].expression), "[[:<:]]ab(");
    const auto all = replacerRegexErrors(rules, false);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(str(all[1].expression), "[a");
    auto none = rules;
    for (auto &r : none)
        r.checked = false;
    EXPECT_TRUE(replacerRegexErrors(none, true).empty());
    EXPECT_EQ(replacerRegexErrors(none, false).size(), 3u);
}

TEST(MisspellFind, FindsRowsNumbersAndPositions)
{
    EditSession session{load({dialogue("aXa"), dialogue("hidden a", "Default", "[hidden]"), dialogue("b a"),
                              dialogue("none")})};
    const std::vector<ReplacerRule> rules{rule("a", "x"), rule("b", "y"), rule("z", "w", 0, false)};
    const auto finds = findErrors(session, rules, {});
    const std::vector<ReplacerFind> expected{
        {0, 1, u"aXa", 0, 1, 0}, {0, 1, u"aXa", 2, 1, 0}, {2, 2, u"b a", 2, 1, 0}, {2, 2, u"b a", 0, 1, 1}};
    EXPECT_EQ(finds, expected);
    EXPECT_TRUE(findErrors(session, {rule("a", "x", 0, false)}, {}).empty());
}

TEST(MisspellFind, SearchesTheRestAlone)
{
    EditSession session{load({dialogue("aab")})};
    auto finds = findErrors(session, {rule("^a", "")}, {});
    ASSERT_EQ(finds.size(), 2u);
    EXPECT_EQ(finds[1].position, 1u);
    finds = findErrors(session, {rule("x*", "")}, {});
    ASSERT_EQ(finds.size(), 4u);
    EXPECT_EQ(finds[3].position, 3u);
    EXPECT_EQ(finds[3].length, 1u);
    EditSession tags{load({dialogue("{x}x{\\i1 x}")})};
    finds = findErrors(tags, {rule("x", "", kReplacerOnlyTags)}, {});
    ASSERT_EQ(finds.size(), 2u);
    EXPECT_EQ(finds[0].position, 1u);
    EXPECT_EQ(finds[1].position, 9u);
}

TEST(MisspellFind, InvalidRuleShiftsNumbers)
{
    EditSession session{load({dialogue("cat")})};
    std::vector<ReplacerRule> rules{rule("(", "bird"), rule("cat", "dog"), rule("dog", "cow", 0, false)};
    const auto finds = findErrors(session, rules, {});
    ASSERT_EQ(finds.size(), 1u);
    EXPECT_EQ(finds[0].rule, 0);
    const auto replaced = replaceFinds(session, rules, finds);
    ASSERT_TRUE(replaced);
    EXPECT_TRUE(replaced->changed);
    EXPECT_EQ(texts(session)[0], "bird");
    EditSession direct{load({dialogue("cat")})};
    EXPECT_TRUE(replaceErrors(direct, rules, {}).value());
    EXPECT_EQ(texts(direct)[0], "dog");
}

TEST(MisspellFind, WhichLines)
{
    EditSession session{load({dialogue("a", "Default"), dialogue("a", "Sign"), dialogue("a", "Sign Two")})};
    const auto lines = session.document().lines();
    session.setSelection(Selection{lines[1]->id, {lines[1]->id}, lines[1]->id, {}});
    const std::vector<ReplacerRule> rules{rule("a", "b")};
    auto rows = [&](ReplacerScope scope) {
        std::vector<std::size_t> out;
        for (const auto &f : findErrors(session, rules, scope))
            out.push_back(f.row);
        return out;
    };
    EXPECT_EQ(rows({L::All, {}}), (std::vector<std::size_t>{0, 1, 2}));
    EXPECT_EQ(rows({L::Selected, {}}), (std::vector<std::size_t>{1}));
    EXPECT_EQ(rows({L::FromSelection, {}}), (std::vector<std::size_t>{1, 2}));
    EXPECT_EQ(findErrors(session, rules, {L::FromSelection, {}}).front().lineNumber, 2);
    EXPECT_EQ(rows({L::Styles, u8"Default,Sign Two"}), (std::vector<std::size_t>{0, 2}));
    EXPECT_EQ(rows({L::Styles, u8"Default, Sign"}), (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(rows({L::Styles, u8" Sign Two ,, Default "}), (std::vector<std::size_t>{0, 2}));
    EXPECT_TRUE(rows({L::Styles, u8"Sign T, , Default2"}).empty());
    EXPECT_TRUE(replaceErrors(session, rules, {L::Selected, {}}).value());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"a", "b", "a"}));
    EXPECT_TRUE(replaceErrors(session, {rule("a|b", "c")}, {L::FromSelection, {}}).value());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"a", "c", "c"}));
    session.setSelection(Selection{});
    EXPECT_EQ(rows({L::FromSelection, {}}), (std::vector<std::size_t>{0}));
}

TEST(MisspellFind, FromSelectionNumbersShownLines)
{
    EditSession session{load({dialogue("a"), dialogue("a", "Default", "[hidden]"), dialogue("a"),
                              dialogue("a", "Default", "[hidden]"), dialogue("a")})};
    const auto lines = session.document().lines();
    session.setSelection(Selection{lines[2]->id, {lines[2]->id}, lines[2]->id, {}});
    const auto finds = findErrors(session, {rule("a", "b")}, {L::FromSelection, {}});
    ASSERT_EQ(finds.size(), 2u);
    EXPECT_EQ(finds[0].row, 2u);
    EXPECT_EQ(finds[0].lineNumber, 2);
    EXPECT_EQ(finds[1].row, 4u);
    EXPECT_EQ(finds[1].lineNumber, 3);
}

TEST(MisspellFind, TranslationWhenPresent)
{
    auto document = load({dialogue("orig a"), dialogue("plain a")});
    const auto first = document.lines()[0]->id;
    document.editLine(first, [](core::LineRecord &l) { l.translation = u8"tl a"; });
    EditSession session{std::move(document)};
    const auto finds = findErrors(session, {rule("a", "x")}, {});
    ASSERT_EQ(finds.size(), 3u);
    EXPECT_EQ(str(finds[0].text), "tl a");
    EXPECT_EQ(finds[0].position, 3u);
    EXPECT_TRUE(replaceErrors(session, {rule("a", "x")}, {}).value());
    const auto lines = session.document().lines();
    EXPECT_EQ(str(lines[0]->text), "orig a");
    EXPECT_EQ(str(lines[0]->translation), "tl x");
    EXPECT_EQ(str(lines[1]->text), "plxin x");
}

TEST(MisspellReplace, OneStepAndUndo)
{
    EditSession session{load({dialogue("a  b"), dialogue("c  d", "Default", "[hidden]"), dialogue("e f")})};
    const auto steps = session.historySize();
    EXPECT_FALSE(replaceErrors(session, shipped({}), {}).value());
    EXPECT_FALSE(replaceErrors(session, shipped({0}), {}).value());
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_TRUE(replaceErrors(session, shipped({1}), {}).value());
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Fixing minor errors");
    EXPECT_EQ(texts(session), (std::vector<std::string>{"a b", "c  d", "e f"}));
    EXPECT_TRUE(session.undo());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"a  b", "c  d", "e f"}));
    EXPECT_TRUE(replaceErrors(session, {rule("e", "e")}, {}).value());
    EXPECT_EQ(session.history().back().name, "Fixing minor errors");
}

TEST(MisspellReplace, ReviewAndApplyChosenFinds)
{
    EditSession session{load({dialogue("Hello , world , again"), dialogue("x  y"), dialogue("keep , this")})};
    const auto rules = shipped({0, 1});
    auto finds = findErrors(session, rules, {});
    ASSERT_EQ(finds.size(), 4u);
    EXPECT_EQ(finds[0].row, 0u);
    EXPECT_EQ(finds[0].position, 5u);
    EXPECT_EQ(finds[1].position, 13u);
    EXPECT_EQ(finds[2].rule, 1);
    EXPECT_EQ(finds[3].row, 2u);
    finds.pop_back();
    const auto steps = session.historySize();
    const auto replaced = replaceFinds(session, rules, finds);
    ASSERT_TRUE(replaced);
    EXPECT_TRUE(replaced->changed);
    EXPECT_TRUE(replaced->problems.empty());
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Fixing minor errors");
    EXPECT_EQ(texts(session), (std::vector<std::string>{"Hello, world, again", "x y", "keep , this"}));
    EXPECT_TRUE(session.undo());
    EXPECT_EQ(texts(session)[0], "Hello , world , again");
}

TEST(MisspellReplace, EditedLinesAndFindsThatDoNotMatchAlone)
{
    EditSession session{load({dialogue("ab ab"), dialogue("z  z")})};
    const std::vector<ReplacerRule> rules{rule("a(?=b)", "x"), rule("(  +)", " ")};
    const auto finds = findErrors(session, rules, {});
    ASSERT_EQ(finds.size(), 3u);
    const auto second = session.document().lines()[1]->id;
    ASSERT_TRUE(session.run(Command{"Line editing", session.revision(), {second}, [&](core::Document &d) {
                                        return d.editLine(second, [](core::LineRecord &l) { l.text = u8"z   z"; });
                                    }}));
    const auto steps = session.historySize();
    const auto replaced = replaceFinds(session, rules, finds);
    ASSERT_TRUE(replaced);
    EXPECT_FALSE(replaced->changed);
    EXPECT_EQ(session.historySize(), steps);
    const std::vector<ReplacerProblem> problems{
        {ReplacerProblem::Kind::NotReplaced, 1, u"a", u8"x", u8"a(?=b)"},
        {ReplacerProblem::Kind::NotReplaced, 1, u"a", u8"x", u8"a(?=b)"},
        {ReplacerProblem::Kind::Edited, 2, {}, {}, {}}};
    EXPECT_EQ(replaced->problems, problems);
    EXPECT_FALSE(replaceFinds(session, rules, {{9, 9, u"x", 0, 1, 0}})->changed);
}

TEST(MisspellReplace, EmptyMatchesEnd)
{
    EXPECT_EQ(fix("ab", {rule("x*", "-")}), "-a--b--");
    EXPECT_EQ(fix("", {rule("^$", "empty")}), "empty");
    EditSession session{load({dialogue("ab")})};
    const auto finds = findErrors(session, {rule("x*", "-")}, {});
    ASSERT_EQ(finds.size(), 3u);
    ASSERT_TRUE(replaceFinds(session, {rule("x*", "-")}, finds)->changed);
    EXPECT_EQ(texts(session)[0], "-a--b--");
}

TEST(MisspellReplace, RuleNumberWithoutExpression)
{
    EditSession session{load({dialogue("cat")})};
    const std::vector<ReplacerRule> rules{rule("(", "x"), rule("cat", "dog")};
    const auto replaced = replaceFinds(session, rules, {{0, 1, u"cat", 0, 3, 1}, {0, 1, u"cat", 0, 3, 7}});
    ASSERT_TRUE(replaced);
    EXPECT_FALSE(replaced->changed);
    ASSERT_EQ(replaced->problems.size(), 2u);
    EXPECT_EQ(replaced->problems[0].kind, ReplacerProblem::Kind::NotReplaced);
    EXPECT_EQ(replaced->problems[1], (ReplacerProblem{ReplacerProblem::Kind::NotReplaced, 1, u"cat", {}, {}}));
    EXPECT_EQ(texts(session)[0], "cat");
}

TEST(MisspellReplace, BlockFromTheLastPositionBack)
{
    EditSession session{load({dialogue("Aa bb")})};
    const std::vector<ReplacerRule> rules{rule("a", "xyz"), rule("b+", "C")};
    const auto finds = findErrors(session, rules, {});
    ASSERT_EQ(finds.size(), 3u);
    ASSERT_TRUE(replaceFinds(session, rules, finds)->changed);
    EXPECT_EQ(texts(session)[0], "Xyzxyz C");
}

// (a+)+$ on a long text trips PCRE2's match limit (catastrophic backtracking).
TEST(MisspellReplace, MatchErrorsAreReportedAsNoMatch)
{
    const std::string longText = std::string(40, 'a') + "b";
    EditSession session{load({dialogue(longText), dialogue("x  y")})};
    const std::vector<ReplacerRule> rules{rule("(a+)+$", "Z"), rule("(  +)", " ")};
    ReplacerMatchErrors errors;
    const auto finds = findErrors(session, rules, {}, &errors);
    ASSERT_EQ(finds.size(), 1u);
    EXPECT_EQ(finds[0].row, 1u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_FALSE(errors[0].empty());

    errors.clear();
    EXPECT_TRUE(replaceErrors(session, rules, {}, {}, &errors).value());
    EXPECT_EQ(texts(session), (std::vector<std::string>{longText, "x y"}));
    EXPECT_EQ(errors.size(), 1u);
    errors.clear();
    EXPECT_FALSE(replaceErrors(session, {rules[0]}, {}, {}, &errors).value());
    EXPECT_EQ(errors.size(), 1u);

    const auto replaced = replaceFinds(session, rules, {{0, 1, u16(longText), 0, longText.size(), 0}});
    ASSERT_TRUE(replaced);
    EXPECT_FALSE(replaced->changed);
    ASSERT_EQ(replaced->problems.size(), 2u);
    EXPECT_EQ(replaced->problems[0].kind, ReplacerProblem::Kind::MatchError);
    EXPECT_EQ(replaced->problems[0].lineNumber, 1);
    EXPECT_FALSE(replaced->problems[0].found.empty());
    EXPECT_EQ(replaced->problems[1], (ReplacerProblem{ReplacerProblem::Kind::NotReplaced, 1, u16(longText), u8"Z", u8"(a+)+$"}));
    EXPECT_EQ(texts(session)[0], longText);
}
