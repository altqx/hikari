#include "hikari/core/grammar.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace hikari::core::grammar;

namespace {

using Range = std::pair<std::size_t, std::size_t>;

std::u16string rawUnder(std::u16string_view raw, const CheckedText &text, std::u16string_view word)
{
    const auto at = text.text.find(word);
    EXPECT_NE(at, std::u16string::npos);
    const auto range = rawRange(text, at, at + word.size());
    EXPECT_TRUE(range.has_value());
    return range ? std::u16string(raw.substr(range->first, range->second - range->first)) : std::u16string();
}

std::array<bool, 15> allGroups(bool on)
{
    std::array<bool, 15> out;
    out.fill(on);
    return out;
}

bool switchOf(const std::vector<std::pair<std::string_view, bool>> &switches, std::string_view rule)
{
    const auto it = std::ranges::find(switches, rule, &std::pair<std::string_view, bool>::first);
    EXPECT_NE(it, switches.end()) << rule;
    return it != switches.end() && it->second;
}

}

TEST(Grammar, OverridesAreLeftOutAndFindingsMapBackUnderTheRawText)
{
    const std::u16string raw = uR"({\i1}Their{\i0} going to the store, I has a apple.)";
    const auto text = checkedText(raw);
    EXPECT_EQ(text.text, u"Their going to the store, I has a apple.");
    ASSERT_EQ(text.raw.size(), text.text.size());
    EXPECT_EQ(rawRange(text, 0, 5), (Range{5, 10}));
    EXPECT_EQ(rawUnder(raw, text, u"has"), u"has");
    EXPECT_EQ(rawUnder(raw, text, u"Their going"), uR"(Their{\i0} going)");
}

TEST(Grammar, LineBreaksAndHardSpacesAreSpaces)
{
    const std::u16string raw = uR"(I has\Na apple\hnow\nthen)";
    const auto text = checkedText(raw);
    EXPECT_EQ(text.text, u"I has a apple now then");
    EXPECT_EQ(rawUnder(raw, text, u"has a"), uR"(has\Na)");
    EXPECT_EQ(rawUnder(raw, text, u"apple now"), uR"(apple\hnow)");
}

TEST(Grammar, KaraokeTagsAreSkippedLikeAnyOverride)
{
    const std::u16string raw = uR"({\k20}I {\k30}has {\k25}a {\k40}apple)";
    const auto text = checkedText(raw);
    EXPECT_EQ(text.text, u"I has a apple");
    EXPECT_EQ(rawRange(text, 2, 5), (Range{14, 17}));
    EXPECT_EQ(rawUnder(raw, text, u"a apple"), uR"(a {\k40}apple)");
}

TEST(Grammar, DrawingsAndMalformedSpansAreNeverChecked)
{
    const auto drawing = checkedText(uR"({\p1}m 0 0 l 100 0 100 100 0 100{\p0})");
    EXPECT_EQ(drawing.text, u" ");
    const std::u16string mixed = uR"(Hello {\p1}m 0 0 l 1 1{\p0}world)";
    const auto text = checkedText(mixed);
    EXPECT_EQ(text.text, u"Hello  world");
    EXPECT_EQ(rawUnder(mixed, text, u"world"), u"world");
    const auto malformed = checkedText(u"a {broken");
    EXPECT_EQ(malformed.text.find(u"broken"), std::u16string::npos);
}

TEST(Grammar, PositionsAreUtf16Units)
{
    // Harper's positions are UTF-16 (LSP's default), as QString's.
    const std::u16string raw = u"{\\b1}\U0001F600 dont";
    const auto text = checkedText(raw);
    EXPECT_EQ(text.text, u"\U0001F600 dont");
    EXPECT_EQ(rawUnder(raw, text, u"dont"), u"dont");
    EXPECT_EQ(rawRange(text, 3, 7), (Range{8, 12}));
}

TEST(Grammar, OffsetsAreTheHiddenTagProjections)
{
    const std::u16string raw = uR"({\i1}I has{\i0}\Na {\p1}m 0 0{\p0}apple)";
    const auto text = checkedText(raw);
    const auto projection = hikari::core::project(raw);
    ASSERT_EQ(text.text.size(), projection.text.size());
    const auto at = text.text.find(u"has");
    EXPECT_EQ(projection.text.substr(at, 3), u"has");
    const auto replaced = hikari::core::mappedReplace(raw, at, at + 3, u"have");
    ASSERT_TRUE(replaced);
    EXPECT_EQ(*replaced, uR"({\i1}I have{\i0}\Na {\p1}m 0 0{\p0}apple)");
}

TEST(Grammar, AssSuggestionsKeepTheTagsAndReturnTheRawCaret)
{
    const std::u16string raw = uR"(I {\i1}has{\i0} a apple)";
    const auto replaced = replace(raw, hikari::core::SubtitleFormat::Ass, 2, 5, u"have");
    ASSERT_TRUE(replaced);
    EXPECT_EQ(replaced->raw, uR"(I {\i1}have{\i0} a apple)");
    EXPECT_EQ(replaced->caret, 11u);
    EXPECT_FALSE(replace(raw, hikari::core::SubtitleFormat::Ass, 2, 2, u"x"));
    EXPECT_FALSE(replace(raw, hikari::core::SubtitleFormat::Ass, 2, 5, u"{x}"));
}

TEST(Grammar, SrtLeavesItsAngleTagsOutAndChecksBraces)
{
    using hikari::core::SubtitleFormat;
    const std::u16string raw = uR"(<font face="tomorow">I has a apple.</font>\N{see you tomorow})";
    const auto text = checkedText(raw, SubtitleFormat::Srt);
    EXPECT_EQ(text.text, u"I has a apple. {see you tomorow}");
    ASSERT_EQ(text.raw.size(), text.text.size());
    EXPECT_EQ(rawUnder(raw, text, u"apple. {"), uR"(apple.</font>\N{)");
    const auto at = text.text.find(u"a apple");
    const auto replaced = replace(raw, SubtitleFormat::Srt, at, at + 1, u"an");
    ASSERT_TRUE(replaced);
    EXPECT_EQ(replaced->raw, uR"(<font face="tomorow">I has an apple.</font>\N{see you tomorow})");
    EXPECT_EQ(replaced->caret, raw.find(u"a apple") + 2);
    const std::u16string italic = u"<i>Their</i> going";
    const auto both = checkedText(italic, SubtitleFormat::Srt);
    ASSERT_EQ(both.text, u"Their going");
    const auto joined = replace(italic, SubtitleFormat::Srt, 0, 11, u"They're going");
    ASSERT_TRUE(joined);
    EXPECT_EQ(joined->raw, u"<i>They're going</i>");
    EXPECT_FALSE(replace(raw, SubtitleFormat::Srt, at, at + 1, u"<b>an"));
    const auto unclosed = checkedText(u"a <b", SubtitleFormat::Srt);
    EXPECT_EQ(unclosed.text, u"a  ");
    EXPECT_FALSE(replace(u"a <b", SubtitleFormat::Srt, 1, 3, u"x"));
}

TEST(Grammar, LineFormatsLeaveBracesOutAndReadTheBarAsASpace)
{
    using hikari::core::SubtitleFormat;
    const std::u16string raw = u"{y:i}I has|a apple";
    for (const auto format : {SubtitleFormat::MicroDvd, SubtitleFormat::TMPlayer, SubtitleFormat::Mpl2}) {
        const auto text = checkedText(raw, format);
        EXPECT_EQ(text.text, u"I has a apple");
        EXPECT_EQ(rawUnder(raw, text, u"has a"), u"has|a");
        const auto replaced = replace(raw, format, 2, 5, u"have");
        ASSERT_TRUE(replaced);
        EXPECT_EQ(replaced->raw, u"{y:i}I have|a apple");
        EXPECT_FALSE(replace(raw, format, 2, 5, u"ha|ve"));
    }
}

TEST(Grammar, RangesOutsideTheTextMapToNothing)
{
    const auto text = checkedText(u"abc");
    EXPECT_FALSE(rawRange(text, 2, 2));
    EXPECT_FALSE(rawRange(text, 1, 4));
    EXPECT_FALSE(rawRange(checkedText(u""), 0, 1));
}

TEST(Grammar, EveryRuleIsInOneOfHarpersFifteenGroups)
{
    ASSERT_EQ(groupIds().size(), 15u);
    EXPECT_EQ(groupIds().front(), "properNouns");
    EXPECT_EQ(groupIds().back(), "miscellaneous");
    EXPECT_EQ(rules().size(), 936u); // harper-cli config of v3.0.0
    for (const auto &rule : rules())
        EXPECT_LT(rule.group, groupIds().size()) << rule.name;
    EXPECT_EQ(groupIds()[*groupOfRule("AnA")], "miscellaneous");
    EXPECT_EQ(groupIds()[*groupOfRule("SpellCheck")], "spellingAndOrthography");
    EXPECT_EQ(groupIds()[*groupOfRule("PronounVerbAgreement")], "pronounsAndPossession");
    EXPECT_EQ(groupIds()[*groupOfRule("SentenceCapitalization")], "capitalizationAndCase");
    EXPECT_FALSE(groupOfRule("NoSuchRule"));
}

TEST(Grammar, SwitchesFollowTheGroupsAndHarpersDefaults)
{
    auto on = allGroups(true);
    auto switches = linterSwitches(on, false);
    EXPECT_EQ(switches.size(), rules().size());
    EXPECT_TRUE(switchOf(switches, "AnA"));
    EXPECT_TRUE(switchOf(switches, "SpellCheck"));
    EXPECT_FALSE(switchOf(switches, "NoOxfordComma"));
    EXPECT_TRUE(switchOf(switches, "OxfordComma"));
    EXPECT_FALSE(switchOf(switches, "SentenceCapitalization"));
    EXPECT_TRUE(switchOf(switches, "Spaces"));

    on[*groupOfRule("AnA")] = false;
    switches = linterSwitches(on, false);
    EXPECT_FALSE(switchOf(switches, "AnA"));
    EXPECT_TRUE(switchOf(switches, "PronounVerbAgreement"));

    switches = linterSwitches(allGroups(true), true);
    EXPECT_FALSE(switchOf(switches, "SpellCheck"));
    EXPECT_TRUE(switchOf(switches, "RepeatedWords"));

    switches = linterSwitches(allGroups(false), false);
    EXPECT_TRUE(std::ranges::none_of(switches, [](const auto &s) { return s.second; }));
}

TEST(Grammar, MessagesAreShownWithoutMarkdown)
{
    EXPECT_EQ(plainMessage(u"Use `an` instead of `a`."), u"Use \u201Can\u201D instead of \u201Ca\u201D.");
    EXPECT_EQ(plainMessage(u"Did you mean `tomorrow`?"), u"Did you mean \u201Ctomorrow\u201D?");
    EXPECT_EQ(plainMessage(u"No code here."), u"No code here.");
    EXPECT_EQ(plainMessage(u"A lone ` mark"), u"A lone  mark");
}

TEST(Grammar, ContextIsCutAtAWord)
{
    EXPECT_EQ(contextBefore(u"I have ", 40), u"I have ");
    EXPECT_EQ(contextAfter(u" apple.", 40), u" apple.");
    EXPECT_EQ(contextBefore(u"We went to the store and then I have ", 20), u"\u2026and then I have ");
    EXPECT_EQ(contextAfter(u" apple and a pear for the trip home.", 20), u" apple and a pear\u2026");
    EXPECT_EQ(contextBefore(u"one two three ", 10), u"\u2026two three ");
    EXPECT_EQ(contextAfter(u" one two three", 8), u" one two\u2026");
    EXPECT_EQ(contextBefore(u"Supercalifragilistic ", 8), u"\u2026ilistic ");
    EXPECT_EQ(contextAfter(u" Supercalifragilistic", 8), u" Superca\u2026");
}
