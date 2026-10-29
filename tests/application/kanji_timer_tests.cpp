#include "hikari/application/kana.h"
#include "hikari/application/kanji_timer.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

std::u16string u(std::string_view utf8)
{
    return core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string s(std::u16string_view text)
{
    const auto utf8 = core::toUtf8(text);
    return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

KaraokeCharClass classes()
{
    auto c = KaraokeCharClass::ascii();
    const auto ascii = c.space;
    c.space = [ascii](char16_t ch) { return ch == u'　' || ascii(ch); };
    return c;
}

std::string groups(const std::vector<KanjiGroup> &gs)
{
    std::string out;
    for (const auto &g : gs)
        out += std::to_string(g.syllables) + ":" + std::to_string(g.characters) + " ";
    return out;
}

const std::string kKimi = "{\\k18}ki{\\k22}mi {\\k30}no {\\k20}ko{\\k25}e {\\k35}ga {\\k22}ki{\\k18}ko{\\k20}e{\\k60}ru";
const std::string kKimiKanji = "君の声が聞こえる";
const std::string kKimiOut = "{\\k40}君{\\k30}の{\\k45}声{\\k35}が{\\k22}聞{\\k18}こ{\\k20}え{\\k60}る";

int total(std::u16string_view text)
{
    int cs = 0;
    for (const auto &syl : kanjiSyllables(text, 0, 0))
        cs += syl.cs;
    return cs;
}

KanjiTimer::Line line(std::uint64_t id, const std::string &text, int start, int end)
{
    return {core::LineId{id}, u(text), start, end};
}

}

TEST(Kana, ReadingsLongestKanaFirstWithTheVariants)
{
    const auto readings = kanaReadings(u("しゃべる"));
    ASSERT_GE(readings.size(), 4u);
    EXPECT_EQ(readings[0].length, 2u);
    EXPECT_EQ(s(readings[0].romaji), "sha");
    EXPECT_EQ(s(readings[1].romaji), "sya");
    EXPECT_EQ(readings[2].length, 1u);
    EXPECT_EQ(s(readings[2].romaji), "shi");
    EXPECT_EQ(s(kanaReadings(u("キ"))[0].romaji), "ki");
    std::vector<std::string> ha;
    for (const auto &r : kanaReadings(u("は")))
        ha.push_back(s(r.romaji));
    EXPECT_EQ(ha, (std::vector<std::string>{"ha", "wa"}));
    EXPECT_TRUE(kanaReadings(u("漢")).empty());
}

TEST(Kana, SmallTsuReadsAsTheNextConsonantAndTheLongMarkAsAVowel)
{
    const auto tsu = kanaReadings(u("っか"));
    ASSERT_EQ(tsu.size(), 1u);
    EXPECT_EQ(s(tsu[0].romaji), "k");
    EXPECT_EQ(s(kanaReadings(u("っち"))[0].romaji), "t");
    EXPECT_GT(kanaReadings(u("っ")).size(), 5u);
    EXPECT_EQ(kanaReadings(u("ー")).size(), 5u);
    EXPECT_TRUE(isKana(U'ー'));
    EXPECT_FALSE(isKana(U'君'));
}

TEST(Kana, RomajiToHiragana)
{
    EXPECT_EQ(s(*romajiToHiragana(u("kikoeru"))), "きこえる");
    EXPECT_EQ(s(*romajiToHiragana(u("Issho ni"))), "いっしょに");
    EXPECT_EQ(s(*romajiToHiragana(u("kanji"))), "かんじ");
    EXPECT_EQ(s(*romajiToHiragana(u("matchi"))), "まっち");
    EXPECT_EQ(s(*romajiToHiragana(u("kon'ya"))), "こんや");
    EXPECT_EQ(s(*romajiToHiragana(u("wo"))), "を");
    EXPECT_EQ(s(*romajiToHiragana(u("tōi"))), "とい");
    EXPECT_FALSE(romajiToHiragana(u("love")));
    EXPECT_FALSE(romajiToHiragana(u("")));
}

TEST(KanjiTimer, KaraokeTags)
{
    EXPECT_TRUE(hasKaraokeTags(u("{\\k10}a")));
    EXPECT_TRUE(hasKaraokeTags(u("{\\i1\\kf5}a")));
    EXPECT_TRUE(hasKaraokeTags(u("{\\K12}a")));
    EXPECT_TRUE(hasKaraokeTags(u("{\\ko7}a")));
    EXPECT_FALSE(hasKaraokeTags(u("{\\kt10}a")));
    EXPECT_FALSE(hasKaraokeTags(u("\\k10 a")));
    EXPECT_FALSE(hasKaraokeTags(u("plain")));
}

TEST(KanjiTimer, SyllablesAreKa1sWithTheirTimeAndTag)
{
    const auto syls = kanjiSyllables(u("{\\k18}ki{\\kf22}mi {\\ko30}no"), 1000, 2000);
    ASSERT_EQ(syls.size(), 3u);
    EXPECT_EQ(s(syls[0].text), "ki");
    EXPECT_EQ(s(syls[1].text), "mi ");
    EXPECT_EQ(syls[1].cs, 22);
    EXPECT_EQ(s(syls[1].tag), "kf");
    EXPECT_EQ(s(syls[2].tag), "ko");
    EXPECT_EQ(total(u(kKimi)), 270);
}

TEST(KanjiTimer, KanjiTextDropsItsKaraokeTagsOnly)
{
    const auto k = kanjiText(u("{\\i1\\k10}星{\\k20}を\\N数"));
    EXPECT_EQ(s(k.text), "{\\i1}星を\\N数");
    ASSERT_EQ(k.units.size(), 4u);
    EXPECT_EQ(s(k.units[0].text), "星");
    EXPECT_EQ(k.units[0].begin, 5u);
    EXPECT_EQ(s(k.units[2].text), "\\N");
    EXPECT_EQ(s(kanjiText(u("{\\kt5}a")).text), "{\\kt5}a");
}

TEST(KanjiTimer, SuggestsTheWholeLineAsAegisubWould)
{
    const auto syls = kanjiSyllables(u(kKimi), 0, 0);
    const auto kanji = kanjiText(u(kKimiKanji));
    const auto sug = suggestGroups(syls, 0, kanji, 0, classes());
    EXPECT_EQ(groups(sug.groups), "2:1 1:1 2:1 1:1 1:1 1:1 1:1 1:1 ");
    EXPECT_TRUE(sug.clean);
    EXPECT_EQ(s(kanjiOutput(syls, kanji, sug.groups)), kKimiOut);
}

TEST(KanjiTimer, AKanjiReadingSeveralKanaAndADoubleConsonant)
{
    const auto syls = kanjiSyllables(u("{\\k20}i{\\k10}s{\\k30}sho {\\k40}ni"), 0, 0);
    const auto sug = suggestGroups(syls, 0, kanjiText(u("一緒に")), 0, classes());
    EXPECT_EQ(groups(sug.groups), "2:1 1:1 1:1 ");
    const auto kana = suggestGroups(syls, 0, kanjiText(u("いっしょに")), 0, classes());
    EXPECT_EQ(groups(kana.groups), "1:1 1:1 1:2 1:1 ");
    EXPECT_TRUE(kana.clean);
}

TEST(KanjiTimer, RunningOutOfKanjiIsACheck)
{
    const auto syls = kanjiSyllables(u("{\\k30}ho{\\k25}shi {\\k30}wo {\\k20}ka{\\k22}zo{\\k18}e{\\k40}te {\\k55}yo"), 0, 0);
    const auto sug = suggestGroups(syls, 0, kanjiText(u("星を数えて")), 0, classes());
    EXPECT_EQ(groups(sug.groups), "2:1 1:1 2:1 1:1 2:1 ");
    EXPECT_FALSE(sug.clean);
    const auto left = suggestGroups(kanjiSyllables(u("{\\k50}yu{\\k50}me"), 0, 0), 0, kanjiText(u("ゆめを")), 0, classes());
    EXPECT_EQ(groups(left.groups), "1:1 1:1 ");
    EXPECT_FALSE(left.clean);
    EXPECT_TRUE(suggestGroups(kanjiSyllables(u("{\\k50}yu{\\k50}me"), 0, 0), 0, kanjiText(u("夢")), 0, classes()).clean);
}

TEST(KanjiTimer, ALineBreakInASyllableIsASpace)
{
    for (const char *esc : {"\\N", "\\n", "\\h"}) {
        const auto syls = kanjiSyllables(u(std::string("{\\k10}a") + esc + "{\\k20}b"), 0, 0);
        const auto kanji = kanjiText(u(std::string("a") + esc + "b"));
        const auto sug = suggestGroups(syls, 0, kanji, 0, classes());
        EXPECT_EQ(groups(sug.groups), "1:2 1:1 ") << esc;
        EXPECT_TRUE(sug.clean) << esc;
        EXPECT_EQ(s(kanjiOutput(syls, kanji, sug.groups)), std::string("{\\k10}a") + esc + "{\\k20}b") << esc;
    }
}

TEST(KanjiTimer, AnApostrophizedNIsReadWhole)
{
    const auto syls = kanjiSyllables(u("{\\k10}n'{\\k20}a"), 0, 0);
    const auto kanji = kanjiText(u("んあ"));
    const auto sug = suggestGroups(syls, 0, kanji, 0, classes());
    EXPECT_EQ(groups(sug.groups), "1:1 1:1 ");
    EXPECT_TRUE(sug.clean);
    EXPECT_EQ(s(kanjiOutput(syls, kanji, sug.groups)), "{\\k10}ん{\\k20}あ");
}

TEST(KanjiTimer, ABlankSyllableWithinAReadingIsInItsGroup)
{
    const auto syls = kanjiSyllables(u("{\\k10}ki{\\k5}{\\k20}mi{\\k30}no"), 0, 0);
    const auto kanji = kanjiText(u("君の"));
    const auto sug = suggestGroups(syls, 0, kanji, 0, classes());
    EXPECT_EQ(groups(sug.groups), "3:1 1:1 ");
    EXPECT_EQ(s(kanjiOutput(syls, kanji, sug.groups)), "{\\k35}君{\\k30}の");
    const auto next = kanjiSyllables(u("{\\k10}ki{\\k5}{\\k30}no"), 0, 0);
    EXPECT_EQ(groups(suggestGroups(next, 0, kanji, 0, classes()).groups), "2:1 1:1 ");
}

TEST(KanjiTimer, OutputKeepsTheTagLettersAndTheLinesOwnTags)
{
    const auto syls = kanjiSyllables(u("{\\kf30}ho{\\kf25}shi {\\kf30}wo {\\kf55}yo"), 0, 0);
    const auto kanji = kanjiText(u("{\\i1\\k99}星を{\\b1}"));
    EXPECT_EQ(s(kanjiOutput(syls, kanji, {{2, 1}, {1, 1}, {1, 0}})), "{\\i1\\kf55}星{\\kf30}を{\\b1\\kf55}");
    EXPECT_EQ(s(kanjiOutput(syls, kanji, {{2, 1}, {1, 0}, {1, 1}})), "{\\i1\\kf55}星{\\kf30}{\\kf55}を{\\b1}");
}

TEST(KanjiTimer, LeftoverKanjiJoinTheLastGroup)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, "{\\k50}yu{\\k50}me", 0, 1000)}, {line(2, "ゆめを", 0, 1000)});
    ASSERT_EQ(timer.current(), 0);
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Check);
    EXPECT_TRUE(timer.acceptLine());
    const auto pending = timer.pending();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(s(pending[0].second), "{\\k50}ゆ{\\k50}めを");
}

TEST(KanjiTimer, PairsByTimeWithLinesLeftAlone)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, "{\\k50}yu{\\k50}me", 0, 1000), line(3, "{\\k50}la {\\k50}la", 1000, 2000),
                    line(5, "{\\k50}ho{\\k50}shi", 2000, 3000)},
                   {line(2, "夢", 0, 1000), line(6, "星", 2010, 3000), line(7, "（間奏）", 3000, 4000)});
    const auto &pairs = timer.pairs();
    ASSERT_EQ(pairs.size(), 4u);
    EXPECT_EQ(pairs[0].kanji->id.value, 2u);
    EXPECT_EQ(pairs[1].status, KanjiTimer::Status::NoKanji);
    EXPECT_EQ(pairs[2].romaji->id.value, 5u);
    EXPECT_EQ(pairs[2].kanji->id.value, 6u);
    EXPECT_EQ(pairs[3].status, KanjiTimer::Status::NoRomaji);
}

TEST(KanjiTimer, ALineWithinItsPartnerStaysPaired)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, "{\\k50}yu{\\k50}me", 0, 1000), line(3, "{\\k100}ho{\\k100}shi", 1000, 3000)},
                   {line(2, "夢", 0, 3000), line(4, "星", 1000, 3000)});
    const auto &pairs = timer.pairs();
    ASSERT_EQ(pairs.size(), 2u);
    EXPECT_EQ(pairs[0].romaji->id.value, 1u);
    EXPECT_EQ(pairs[0].kanji->id.value, 2u);
    EXPECT_EQ(pairs[1].romaji->id.value, 3u);
    EXPECT_EQ(pairs[1].kanji->id.value, 4u);
    timer.setLines({line(1, "{\\k50}la{\\k50}la", 0, 1000), line(3, "{\\k100}ho{\\k100}shi", 1000, 3000)},
                   {line(2, "星", 900, 3000)});
    ASSERT_EQ(timer.pairs().size(), 2u);
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::NoKanji);
    EXPECT_EQ(timer.pairs()[1].romaji->id.value, 3u);
}

TEST(KanjiTimer, AFirstGroupThatRanOutIsACheck)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, "{\\k10}a{\\k20}b", 0, 300)}, {line(2, "a", 0, 300)});
    ASSERT_EQ(timer.pairs().size(), 1u);
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Check);
    EXPECT_EQ(timer.acceptAllSuggestions(), 0);
    EXPECT_TRUE(timer.pending().empty());
}

TEST(KanjiTimer, ManyLinesPairQuickly)
{
    std::vector<KanjiTimer::Line> romaji, kanji;
    for (int i = 0; i < 20000; ++i) {
        romaji.push_back(line(2 * i + 1, "{\\k10}a", i * 100, i * 100 + 50));
        kanji.push_back(line(2 * i + 2, "あ", i * 100 + 50, i * 100 + 100));
    }
    KanjiTimer timer(classes());
    const auto start = std::chrono::steady_clock::now();
    timer.setLines(std::move(romaji), std::move(kanji));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
    ASSERT_EQ(timer.pairs().size(), 20000u);
    EXPECT_EQ(timer.pairs()[1].kanji->id.value, 4u);
}

TEST(KanjiTimer, PairsByOrderWithoutAnyOverlap)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, "{\\k50}yu{\\k50}me", 0, 1000), line(3, "{\\k50}ho{\\k50}shi", 2000, 3000)},
                   {line(2, "夢", 0, 0), line(4, "星", 0, 0)});
    ASSERT_EQ(timer.pairs().size(), 2u);
    EXPECT_EQ(timer.pairs()[1].kanji->id.value, 4u);
    EXPECT_TRUE(timer.skipRomaji());
    ASSERT_EQ(timer.pairs().size(), 3u);
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::NoKanji);
    EXPECT_EQ(timer.pairs()[1].romaji->id.value, 3u);
    EXPECT_EQ(timer.pairs()[1].kanji->id.value, 2u);
    EXPECT_EQ(timer.current(), 1);
    EXPECT_TRUE(timer.undo());
    EXPECT_EQ(timer.pairs().size(), 2u);
}

TEST(KanjiTimer, KeysLinkUnlinkAndAccept)
{
    KanjiTimer timer(classes());
    timer.setSuggest(false);
    timer.setLines({line(1, kKimi, 0, 2700)}, {line(2, kKimiKanji, 0, 2700)});
    auto v = timer.view();
    ASSERT_TRUE(v);
    EXPECT_EQ(v->selection, (KanjiGroup{1, 1}));
    EXPECT_FALSE(v->canAccept);
    EXPECT_TRUE(timer.growSyllables());
    EXPECT_TRUE(timer.enter());
    EXPECT_TRUE(timer.enter());
    EXPECT_TRUE(timer.growSyllables());
    EXPECT_TRUE(timer.enter());
    EXPECT_EQ(groups(timer.view()->linked), "2:1 1:1 2:1 ");
    EXPECT_TRUE(timer.unlink());
    EXPECT_EQ(timer.view()->selection, (KanjiGroup{2, 1}));
    EXPECT_TRUE(timer.shrinkCharacters());
    EXPECT_FALSE(timer.shrinkCharacters());
    EXPECT_TRUE(timer.growCharacters());
    for (int i = 0; i < 6; ++i)
        EXPECT_TRUE(timer.enter());
    EXPECT_TRUE(timer.enter());
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Done);
    ASSERT_EQ(timer.pending().size(), 1u);
    EXPECT_EQ(s(timer.pending()[0].second), kKimiOut);
}

TEST(KanjiTimer, AcceptLineTakesTheSuggestionAndUndoRedo)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, kKimi, 0, 2700), line(3, "{\\k50}yu{\\k50}me", 3000, 4000)},
                   {line(2, kKimiKanji, 0, 2700), line(4, "夢", 3000, 4000)});
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Suggested);
    EXPECT_TRUE(timer.view()->canAccept);
    EXPECT_EQ(s(*timer.view()->output), kKimiOut);
    EXPECT_TRUE(timer.acceptLine());
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Done);
    EXPECT_EQ(timer.current(), 1);
    EXPECT_TRUE(timer.undo());
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Suggested);
    EXPECT_EQ(timer.current(), 0);
    EXPECT_TRUE(timer.redo());
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Done);
    EXPECT_TRUE(timer.previous());
    EXPECT_EQ(timer.view()->linked.size(), 8u);
    EXPECT_TRUE(timer.unlink());
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Suggested);
}

TEST(KanjiTimer, UndoKeepsWhatEachStepChanged)
{
    std::vector<KanjiTimer::Line> romaji, kanji;
    for (int i = 0; i < 2000; ++i) {
        romaji.push_back(line(2 * i + 1, "{\\k10}ka{\\k10}na", i * 100, i * 100 + 50));
        kanji.push_back(line(2 * i + 2, "かな", i * 100 + 50, i * 100 + 100));
    }
    KanjiTimer timer(classes());
    timer.setLines(std::move(romaji), std::move(kanji));
    for (int i = 0; i < 300; ++i) {
        ASSERT_TRUE(timer.growSyllables());
        ASSERT_TRUE(timer.shrinkSyllables());
    }
    int undone = 0;
    while (timer.undo())
        ++undone;
    EXPECT_EQ(undone, 500);
    EXPECT_EQ(groups({timer.view()->selection}), "1:1 ");
    while (timer.redo()) {
    }
    EXPECT_EQ(groups({timer.view()->selection}), "1:1 ");
    ASSERT_TRUE(timer.choose(1));
    ASSERT_TRUE(timer.link());
    ASSERT_TRUE(timer.undo());
    EXPECT_EQ(timer.current(), 1);
    EXPECT_TRUE(timer.view()->linked.empty());
    ASSERT_TRUE(timer.undo());
    EXPECT_EQ(timer.current(), 0);
    EXPECT_TRUE(timer.redo());
    EXPECT_TRUE(timer.redo());
    EXPECT_EQ(timer.current(), 1);
    EXPECT_EQ(timer.view()->linked.size(), 1u);
}

TEST(KanjiTimer, ClicksEndTheGroupOrReopenALinkedOne)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, kKimi, 0, 2700)}, {line(2, kKimiKanji, 0, 2700)});
    EXPECT_TRUE(timer.link());
    EXPECT_TRUE(timer.link());
    EXPECT_TRUE(timer.clickCharacter(3));
    EXPECT_EQ(timer.view()->selection, (KanjiGroup{2, 2}));
    EXPECT_TRUE(timer.clickSyllable(5));
    EXPECT_EQ(timer.view()->selection, (KanjiGroup{3, 2}));
    EXPECT_TRUE(timer.clickCharacter(0));
    EXPECT_TRUE(timer.view()->linked.empty());
    EXPECT_EQ(timer.view()->selection, (KanjiGroup{2, 1}));
}

TEST(KanjiTimer, AcceptAllTakesTheCleanSuggestionsAndApplyMarksThem)
{
    KanjiTimer timer(classes());
    timer.setLines({line(1, kKimi, 0, 2700), line(3, "{\\k50}yu{\\k50}me", 3000, 4000)},
                   {line(2, kKimiKanji, 0, 2700), line(4, "ゆめを", 3000, 4000)});
    EXPECT_EQ(timer.pairs()[1].status, KanjiTimer::Status::Check);
    EXPECT_EQ(timer.acceptAllSuggestions(), 1);
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Done);
    EXPECT_EQ(timer.pairs()[1].status, KanjiTimer::Status::Check);
    EXPECT_EQ(timer.current(), 1);
    const auto pending = timer.pending();
    ASSERT_EQ(pending.size(), 1u);
    timer.applied();
    timer.setLines({line(1, kKimi, 0, 2700), line(3, "{\\k50}yu{\\k50}me", 3000, 4000)},
                   {line(2, kKimiOut, 0, 2700), line(4, "ゆめを", 3000, 4000)});
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Applied);
    EXPECT_TRUE(timer.pending().empty());
    timer.setLines({line(1, kKimi, 0, 2700), line(3, "{\\k50}yu{\\k50}me", 3000, 4000)},
                   {line(2, "君の声", 0, 2700), line(4, "ゆめを", 3000, 4000)});
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Changed);
}

TEST(KanjiTimer, EveryOutputAddsUpToTheRomaji)
{
    const std::vector<std::pair<std::string, std::string>> song{
        {kKimi, kKimiKanji},
        {"{\\k20}to{\\k25}o{\\k15}i {\\k30}so{\\k20}ra {\\k12}no {\\k18}mu{\\k14}ko{\\k16}u {\\k40}e", "遠い空の向こうへ"},
        {"{\\k30}a{\\k20}shi{\\k25}ta {\\k15}e {\\k22}ha{\\k18}shi{\\k12}ri{\\k16}da{\\k60}su", "明日へ走り出す"},
        {"{\\k30}ho{\\k25}shi {\\k30}wo {\\k20}ka{\\k22}zo{\\k18}e{\\k40}te {\\k55}yo", "星を数えて"},
    };
    for (const auto &[romaji, kanji] : song) {
        const auto syls = kanjiSyllables(u(romaji), 0, 0);
        const auto text = kanjiText(u(kanji));
        const auto sug = suggestGroups(syls, 0, text, 0, classes()).groups;
        const auto out = kanjiOutput(syls, text, sug);
        EXPECT_EQ(total(out), total(u(romaji))) << s(out);
        const auto multi = kanjiOutput(syls, text, sug, true);
        EXPECT_EQ(total(multi), total(u(romaji))) << s(multi);
        EXPECT_EQ(kanjiSyllables(multi, 0, 0).size(), syls.size()) << s(multi);
        EXPECT_EQ(kanjiText(multi).text, text.text) << s(multi);
        if (s(multi).find('#') != std::string::npos)
            EXPECT_EQ(groups(kanjiText(multi).tagged), groups(sug)) << s(multi);
    }
}

const std::string kGedou = "{\\k15}ge{\\k16}dou {\\k13}kou{\\k9}ge{\\k7}ki, {\\k6}fu{\\k9}sho{\\k9}ku {\\k12}kou{\\k12}ge{\\k7}ki";
const std::string kGedouKanji = "外道攻撃、腐蝕攻撃";
const std::string kGedouMulti = "{\\k15}外{\\k16}道{\\k13}攻{\\k9}撃、{\\k7}#{\\k6}腐{\\k9}#{\\k9}#{\\k12}蝕{\\k12}攻{\\k7}撃";
const std::vector<KanjiGroup> kGedouGroups{{1, 1}, {1, 1}, {1, 1}, {2, 2}, {3, 1}, {1, 1}, {1, 1}, {1, 1}};

TEST(KanjiTimer, MultiHighlightWritesAHashForEachFurtherSyllable)
{
    const auto syls = kanjiSyllables(u(kGedou), 0, 0);
    const auto kanji = kanjiText(u(kGedouKanji));
    EXPECT_EQ(s(kanjiOutput(syls, kanji, kGedouGroups, true)), kGedouMulti);
    EXPECT_EQ(s(kanjiOutput(syls, kanji, kGedouGroups)),
              "{\\k15}外{\\k16}道{\\k13}攻{\\k16}撃、{\\k24}腐{\\k12}蝕{\\k12}攻{\\k7}撃");
    EXPECT_EQ(total(u(kGedouMulti)), total(u(kGedou)));
}

TEST(KanjiTimer, MultiHighlightKeepsEachSyllablesLettersAndTheLinesTags)
{
    const auto syls = kanjiSyllables(u("{\\kf30}ho{\\K25}shi {\\ko30}wo {\\kf55}yo"), 0, 0);
    const auto kanji = kanjiText(u("{\\i1\\k99}星を{\\b1}"));
    EXPECT_EQ(s(kanjiOutput(syls, kanji, {{2, 1}, {1, 1}, {1, 0}}, true)),
              "{\\i1\\kf30}星{\\K25}#{\\ko30}を{\\b1\\kf55}");
    const auto between = kanjiText(u("星{\\i1}を"));
    EXPECT_EQ(s(kanjiOutput(syls, between, {{2, 1}, {2, 1}}, true)), "{\\kf30}星{\\K25}#{\\i1\\ko30}を{\\kf55}#");
}

TEST(KanjiTimer, MultiHighlightLeavesSingleBlankAndLeftoverAsTheyWere)
{
    const auto syls = kanjiSyllables(u("{\\k10}a{\\k5} {\\k5} {\\k20}b"), 0, 0);
    const auto kanji = kanjiText(u("AB"));
    EXPECT_EQ(s(kanjiOutput(syls, kanji, {{1, 1}, {2, 0}, {1, 1}}, true)), "{\\k10}A{\\k10}{\\k20}B");
    const auto geki = kanjiSyllables(u("{\\k20}ge{\\k10}ki"), 0, 0);
    EXPECT_EQ(s(kanjiOutput(geki, kanjiText(u("撃！")), {{2, 1}}, true)), "{\\k20}撃！{\\k10}#");
    KanjiTimer timer(classes());
    timer.setMulti(true);
    timer.setLines({line(1, "{\\k20}ge{\\k10}ki", 0, 300)}, {line(2, "撃！", 0, 300)});
    EXPECT_TRUE(timer.clickSyllable(1));
    EXPECT_TRUE(timer.enter());
    EXPECT_TRUE(timer.enter());
    ASSERT_EQ(timer.pending().size(), 1u);
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k20}撃！{\\k10}#");
}

TEST(KanjiTimer, HashSyllablesReadBackAsTheirGroups)
{
    const auto read = kanjiText(u(kGedouMulti));
    EXPECT_EQ(s(read.text), kGedouKanji);
    EXPECT_EQ(read.units.size(), 9u);
    EXPECT_EQ(groups(read.tagged), groups(kGedouGroups));
    const auto wide = kanjiText(u("{\\i1\\k20}撃{\\k10\\b1}＃{\\k5}を"));
    EXPECT_EQ(s(wide.text), "{\\i1}撃{\\b1}を");
    EXPECT_EQ(groups(wide.tagged), "2:1 1:1 ");
    for (const char *text : {"{\\k10}#{\\k5}a", "{\\k10}a{\\k5}#b", "a#", "{\\k10}a#"}) {
        const auto kanji = kanjiText(u(text));
        EXPECT_NE(s(kanji.text).find('#'), std::string::npos) << text;
        EXPECT_TRUE(kanji.tagged.empty()) << text;
    }
    const auto before = kanjiText(u("前{\\k10}撃{\\k5}#"));
    EXPECT_EQ(s(before.text), "前撃");
    EXPECT_TRUE(before.tagged.empty());
    EXPECT_TRUE(kanjiText(u(kKimiOut)).tagged.empty());
}

TEST(KanjiTimer, ALineOfManyHashSyllablesReadsQuickly)
{
    std::string text = "{\\k1}撃";
    for (int i = 0; i < 256000; ++i)
        text += "{\\k1}#";
    const auto line = u(text);
    const auto start = std::chrono::steady_clock::now();
    const auto read = kanjiText(line);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
    EXPECT_EQ(s(read.text), "撃");
    ASSERT_EQ(read.tagged.size(), 1u);
    EXPECT_EQ(read.tagged[0], (KanjiGroup{256001, 1}));
}

TEST(KanjiTimer, ALineWithHashSyllablesOpensWithItsGroupsLinked)
{
    KanjiTimer timer(classes());
    timer.setMulti(true);
    timer.setLines({line(1, kGedou, 0, 1150)}, {line(2, kGedouMulti, 0, 1150)});
    auto v = timer.view();
    ASSERT_TRUE(v);
    EXPECT_EQ(groups(v->linked), groups(kGedouGroups));
    EXPECT_TRUE(v->canAccept);
    ASSERT_TRUE(v->output);
    EXPECT_EQ(s(*v->output), kGedouMulti);
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Suggested);
    EXPECT_TRUE(timer.unlink());
    EXPECT_EQ(timer.view()->selection, (KanjiGroup{1, 1}));
    EXPECT_TRUE(timer.undo());
    timer.setMulti(false);
    EXPECT_TRUE(timer.enter());
    ASSERT_EQ(timer.pending().size(), 1u);
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k15}外{\\k16}道{\\k13}攻{\\k16}撃、{\\k24}腐{\\k12}蝕{\\k12}攻{\\k7}撃");
    KanjiTimer reopened(classes());
    reopened.setLines({line(1, kGedou, 0, 1150)}, {line(2, kGedouMulti, 0, 1150)});
    reopened.setLines({line(1, "{\\k16}ge" + kGedou.substr(8), 0, 1150)}, {line(2, kGedouMulti, 0, 1150)});
    EXPECT_EQ(reopened.pairs()[0].status, KanjiTimer::Status::Suggested);
    EXPECT_EQ(groups(reopened.view()->linked), groups(kGedouGroups));
    KanjiTimer other(classes());
    other.setLines({line(1, kKimi, 0, 2700)}, {line(2, "{\\k40}君{\\k30}#{\\k45}の", 0, 2700)});
    EXPECT_TRUE(other.view()->linked.empty());
}

TEST(KanjiTimer, MultiHighlightIsAppliedAndStaysApplied)
{
    KanjiTimer timer(classes());
    timer.setMulti(true);
    timer.setLines({line(1, kKimi, 0, 2700)}, {line(2, kKimiKanji, 0, 2700)});
    EXPECT_TRUE(timer.acceptLine());
    ASSERT_EQ(timer.pending().size(), 1u);
    const std::string out = s(timer.pending()[0].second);
    EXPECT_EQ(out, "{\\k18}君{\\k22}#{\\k30}の{\\k20}声{\\k25}#{\\k35}が{\\k22}聞{\\k18}こ{\\k20}え{\\k60}る");
    timer.applied();
    timer.setLines({line(1, kKimi, 0, 2700)}, {line(2, out, 0, 2700)});
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Applied);
    timer.setMulti(false);
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Applied);
    EXPECT_TRUE(timer.pending().empty());
}

namespace {

ReadingAnalyzer dictionary(std::map<std::string, std::string> utf8)
{
    std::map<std::u16string, std::u16string> words;
    for (const auto &[surface, kana] : utf8)
        words[u(surface)] = u(kana);
    return [words = std::move(words)](std::u16string_view text) {
        std::vector<ReadingWord> out;
        for (std::size_t i = 0; i < text.size();) {
            std::size_t best = 0;
            const std::u16string *reading = nullptr;
            for (const auto &[surface, kana] : words)
                if (surface.size() > best && text.substr(i).starts_with(surface)) {
                    best = surface.size();
                    reading = &kana;
                }
            out.push_back({std::u16string(text.substr(i, best ? best : 1)), reading ? *reading : std::u16string()});
            i += best ? best : 1;
        }
        return out;
    };
}

const ReadingAnalyzer kUnidic = dictionary({{"君", "キミ"}, {"声", "コエ"}, {"聞こえる", "キコエル"}, {"永遠", "エイエン"},
                                            {"愛", "アイ"}, {"二人", "フタリ"}});

std::string readings(const KanjiTimer &timer)
{
    std::string out;
    const auto v = timer.view();
    for (const auto &r : v->readings) {
        std::u16string base;
        for (std::size_t c = r.begin; c < r.end; ++c)
            base += v->kanji.units[c].text;
        out += s(base) + "|" + s(r.reading()) + (r.sung ? "*" : "") + " ";
    }
    return out;
}

KanjiTimer furiganaTimer(const std::string &romaji, const std::string &kanji, bool multi = false)
{
    KanjiTimer timer(classes());
    timer.setFurigana(true);
    timer.setMulti(multi);
    timer.setDictionary(kUnidic);
    timer.setLines({line(1, romaji, 0, 3000)}, {line(2, kanji, 0, 3000)});
    return timer;
}

}

TEST(KanjiTimer, FuriganaIsTheSungKanaOverEachKanjiRun)
{
    auto timer = furiganaTimer(kKimi, kKimiKanji);
    EXPECT_EQ(readings(timer), "君|きみ* 声|こえ* 聞|き* ");
    ASSERT_TRUE(timer.view()->output);
    EXPECT_EQ(s(*timer.view()->output), "{\\k40}君|きみ{\\k30}の{\\k45}声|こえ{\\k35}が{\\k22}聞|き{\\k18}こ{\\k20}え{\\k60}る");
    EXPECT_TRUE(timer.acceptLine());
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k40}君|きみ{\\k30}の{\\k45}声|こえ{\\k35}が{\\k22}聞|き{\\k18}こ{\\k20}え{\\k60}る");
    timer.setFurigana(false);
    EXPECT_EQ(s(timer.pending()[0].second), kKimiOut);
}

TEST(KanjiTimer, APoeticReadingIsSungAndTheDictionarysIsKeptBeside)
{
    auto timer = furiganaTimer("{\\k30}to{\\k35}wa {\\k40}ni", "永遠に");
    EXPECT_EQ(readings(timer), "永|と* 遠|わ* ");
    for (const auto &r : timer.view()->readings) {
        EXPECT_EQ(s(r.dictionary), "えいえん");
        EXPECT_TRUE(r.differs);
    }
    EXPECT_TRUE(timer.growSyllables());
    EXPECT_TRUE(timer.growCharacters());
    const auto v = timer.view();
    ASSERT_EQ(v->readings.size(), 1u);
    EXPECT_EQ(s(v->readings[0].suggested), "とわ");
    EXPECT_TRUE(v->readings[0].sung);
    EXPECT_EQ(s(v->readings[0].dictionary), "えいえん");
    EXPECT_TRUE(v->readings[0].differs);
    auto futari = furiganaTimer("{\\k10}fu{\\k10}ta{\\k10}ri", "二人");
    ASSERT_EQ(futari.view()->readings.size(), 2u);
    for (const auto &r : futari.view()->readings) {
        EXPECT_EQ(s(r.dictionary), "ふたり");
        EXPECT_FALSE(r.differs);
    }
    EXPECT_EQ(s(*v->output), "{\\k65}永遠|とわ{\\k40}に");
}

TEST(KanjiTimer, RomajiThatIsNoKanaTakesTheDictionarysReading)
{
    auto timer = furiganaTimer("{\\k45}love {\\k30}wo", "愛を");
    EXPECT_EQ(readings(timer), "愛|あい ");
    EXPECT_EQ(s(*timer.view()->output), "{\\k45}愛|あい{\\k30}を");
    timer.setDictionary({});
    EXPECT_EQ(readings(timer), "愛| ");
    EXPECT_EQ(s(*timer.view()->output), "{\\k45}愛{\\k30}を");
}

TEST(KanjiTimer, ATypedReadingStaysUntilTheSuggestionIsTakenBack)
{
    auto timer = furiganaTimer("{\\k45}love {\\k30}wo", "愛を");
    EXPECT_TRUE(timer.setReading(u("ラヴ")));
    EXPECT_FALSE(timer.setReading(u("ラヴ")));
    EXPECT_EQ(readings(timer), "愛|ラヴ ");
    EXPECT_EQ(s(*timer.view()->output), "{\\k45}愛|ラヴ{\\k30}を");
    EXPECT_TRUE(timer.growCharacters());
    EXPECT_TRUE(timer.shrinkCharacters());
    EXPECT_EQ(readings(timer), "愛|ラヴ ");
    EXPECT_TRUE(timer.acceptLine());
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k45}愛|ラヴ{\\k30}を");
    EXPECT_TRUE(timer.undo());
    EXPECT_TRUE(timer.undo());
    EXPECT_TRUE(timer.undo());
    EXPECT_TRUE(timer.undo());
    EXPECT_EQ(readings(timer), "愛|あい ");
    EXPECT_TRUE(timer.redo());
    EXPECT_TRUE(timer.useSuggestion());
    EXPECT_FALSE(timer.useSuggestion());
    EXPECT_EQ(readings(timer), "愛|あい ");
    EXPECT_TRUE(timer.setReading(u""));
    EXPECT_EQ(s(*timer.view()->output), "{\\k45}愛{\\k30}を");
    EXPECT_TRUE(timer.link());
    EXPECT_FALSE(timer.setReading(u("お")));
}

TEST(KanjiTimer, AReadingPerKanjiRunSplitAtSpaces)
{
    const auto syls = kanjiSyllables(u("{\\k10}u{\\k10}ma{\\k10}re{\\k10}ka{\\k10}wa"), 0, 0);
    const auto kanji = kanjiText(u("生まれ変わ"));
    const std::vector<KanjiGroup> one{{5, 5}};
    auto rs = kanjiReadings(syls, kanji, one, {}, {});
    ASSERT_EQ(rs.size(), 2u);
    EXPECT_EQ(s(rs[0].suggested), "う");
    EXPECT_EQ(s(rs[1].suggested), "か");
    KanjiTimer timer(classes());
    timer.setFurigana(true);
    timer.setSuggest(false);
    timer.setLines({line(1, "{\\k10}u{\\k10}ma{\\k10}re{\\k10}ka{\\k10}wa", 0, 500)}, {line(2, "生まれ変わ", 0, 500)});
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(timer.growSyllables());
        EXPECT_TRUE(timer.growCharacters());
    }
    EXPECT_TRUE(timer.setReading(u("い　が")));
    EXPECT_EQ(readings(timer), "生|い* 変|が* ");
    EXPECT_TRUE(timer.setReading(u("うま れ か")));
    EXPECT_EQ(readings(timer), "生|うまれか* 変|* ");
}

TEST(KanjiTimer, AGroupOfKanjiAndKanaReadsOverItsKanjiOnly)
{
    const auto syls = kanjiSyllables(u("{\\k35}ga {\\k22}ki{\\k18}ko {\\k20}e"), 0, 0);
    const auto kanji = kanjiText(u("が聞こえ"));
    const std::vector<KanjiGroup> gs{{1, 1}, {2, 2}, {1, 1}};
    const auto rs = kanjiReadings(syls, kanji, gs, {}, {});
    EXPECT_EQ(s(kanjiOutput(syls, kanji, gs, false, &rs)), "{\\k35}が{\\k22}聞|き{\\k18}こ{\\k20}え");
    const auto one = kanjiSyllables(u("{\\k35}ga {\\k40}kiko {\\k20}e"), 0, 0);
    const std::vector<KanjiGroup> ones{{1, 1}, {1, 2}, {1, 1}};
    const auto rs1 = kanjiReadings(one, kanji, ones, {}, {});
    EXPECT_EQ(s(kanjiOutput(one, kanji, ones, false, &rs1)), "{\\k35}が{\\k40}聞こ|きこ{\\k20}え");
    const auto typed = kanjiReadings(syls, kanji, gs, {}, {{{1, 2}, u("きき")}});
    EXPECT_EQ(s(kanjiOutput(syls, kanji, gs, false, &typed)), "{\\k35}が{\\k22}聞|きき{\\k18}こ{\\k20}え");
}

TEST(KanjiTimer, MultiHighlightGivesEachSyllableItsKana)
{
    auto timer = furiganaTimer(kKimi, kKimiKanji, true);
    EXPECT_EQ(s(*timer.view()->output),
              "{\\k18}君|き{\\k22}#|み{\\k30}の{\\k20}声|こ{\\k25}#|え{\\k35}が{\\k22}聞|き{\\k18}こ{\\k20}え{\\k60}る");
    auto towa = furiganaTimer("{\\k30}to{\\k35}wa {\\k40}ni", "永遠に", true);
    EXPECT_TRUE(towa.growSyllables());
    EXPECT_TRUE(towa.growCharacters());
    EXPECT_EQ(s(*towa.view()->output), "{\\k30}永遠|と{\\k35}#|わ{\\k40}に");
    EXPECT_TRUE(towa.setReading(u("えいえん")));
    EXPECT_EQ(s(*towa.view()->output), "{\\k30}永遠|えいえん{\\k35}#{\\k40}に");
}

TEST(KanjiTimer, ALineOfManyFuriganaSyllablesReadsQuickly)
{
    std::string text;
    for (int i = 0; i < 128000; ++i)
        text += "{\\k1}君|き{\\k1}#|み";
    const auto line = u(text);
    const auto start = std::chrono::steady_clock::now();
    const auto read = kanjiText(line);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
    ASSERT_EQ(read.units.size(), 128000u);
    ASSERT_EQ(read.readings.size(), 128000u);
    EXPECT_EQ(read.readings.back(), (KanjiText::Reading{127999, 128000, u("きみ")}));
    EXPECT_EQ(read.tagged.size(), 128000u);
}

TEST(KanjiTimer, TheReadingsOfALongLineComeQuickly)
{
    std::string romaji, text;
    for (int i = 0; i < 64000; ++i) {
        romaji += "{\\k1}ki{\\k1}mi";
        text += "君";
    }
    const auto syls = kanjiSyllables(u(romaji), 0, 0);
    const auto kanji = kanjiText(u(text));
    std::vector<KanjiGroup> groups(64000, KanjiGroup{2, 1});
    std::vector<FuriganaRun> words;
    for (std::size_t i = 0; i < 64000; i += 2)
        words.push_back({i, i + 2, u("君君"), u("きみきみ"), std::nullopt});
    const auto start = std::chrono::steady_clock::now();
    const auto rs = kanjiReadings(syls, kanji, groups, words, {});
    const auto out = kanjiOutput(syls, kanji, groups, true, &rs);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
    ASSERT_EQ(rs.size(), 64000u);
    EXPECT_EQ(s(rs.back().dictionary), "きみきみ");
    EXPECT_FALSE(rs.back().differs);
    EXPECT_EQ(s(out.substr(0, 16)), "{\\k1}君|き{\\k1}#|み");
}

TEST(KanjiTimer, ALongMixedBaseReadingIsClassifiedOnce)
{
    constexpr std::size_t count = 64000;
    std::u16string text(count, u'あ');
    for (std::size_t i = 0; i < count; ++i)
        text += u"漢あ";
    const auto kanji = kanjiText(text);
    const std::vector<KanjiGroup> groups{{1, text.size()}};
    const KanjiTyped typed{{{0, text.size()}, u("x")}};
    const auto start = std::chrono::steady_clock::now();
    const auto rs = kanjiReadings({{u("x"), 1, {}}}, kanji, groups, {}, typed, false);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
    ASSERT_EQ(rs.size(), count);
    EXPECT_EQ(rs.front().typed, u("x"));
}

TEST(KanjiTimer, FuriganaReadsBackAsTypedReadings)
{
    const auto read = kanjiText(u("{\\k40}君|きみ{\\k30}の{\\k20}声｜！こ{\\k25}#|え{\\k10}二人|ふたり"));
    EXPECT_EQ(s(read.text), "君の声二人");
    ASSERT_EQ(read.readings.size(), 3u);
    EXPECT_EQ(read.readings[0], (KanjiText::Reading{0, 1, u("きみ")}));
    EXPECT_EQ(read.readings[1], (KanjiText::Reading{2, 3, u("こえ")}));
    EXPECT_EQ(read.readings[2], (KanjiText::Reading{3, 5, u("ふたり")}));
    EXPECT_EQ(groups(read.tagged), "1:1 1:1 2:1 1:2 ");
    const auto mixed = kanjiText(u("{\\k40}聞こ|きこ{\\k20}え"));
    ASSERT_EQ(mixed.readings.size(), 1u);
    EXPECT_EQ(mixed.readings[0], (KanjiText::Reading{0, 1, u("き")}));
    EXPECT_EQ(s(kanjiText(u("君|きみ")).text), "君|きみ");
    auto timer = furiganaTimer(kKimi, "{\\k40}君|キミ{\\k30}の{\\k45}声{\\k35}が{\\k22}聞{\\k18}こ{\\k20}え{\\k60}る");
    EXPECT_EQ(readings(timer), "君|キミ* 声|こえ* 聞|き* ");
    ASSERT_TRUE(timer.view()->readings[0].typed);
    timer.setFurigana(false);
    EXPECT_EQ(s(*timer.view()->output), "{\\k40}君|キミ{\\k30}の{\\k45}声{\\k35}が{\\k22}聞{\\k18}こ{\\k20}え{\\k60}る");
}

TEST(KanjiTimer, HashReadingsJoinTheirBaseThroughHighlightsWithNone)
{
    EXPECT_EQ(kanjiText(u("{\\k10}君{\\k10}#|き{\\k10}#|み")).readings,
              (std::vector<KanjiText::Reading>{{0, 1, u("きみ")}}));
    EXPECT_EQ(kanjiText(u("{\\k10}君|き{\\k10}#{\\k10}#|み")).readings,
              (std::vector<KanjiText::Reading>{{0, 1, u("きみ")}}));
    EXPECT_EQ(kanjiText(u("{\\k10}君|き{\\k10}#{\\i1}|み")).readings,
              (std::vector<KanjiText::Reading>{{0, 1, u("きみ")}}));
    EXPECT_EQ(kanjiText(u("{\\k10}君|き{\\k10\\i1}＃|み")).readings,
              (std::vector<KanjiText::Reading>{{0, 1, u("きみ")}}));
    EXPECT_EQ(kanjiText(u("{\\k10}君{\\k10}#{\\k10}声|こえ")).readings,
              (std::vector<KanjiText::Reading>{{1, 2, u("こえ")}}));
}

TEST(KanjiTimer, RegroupingKeepsTheReadingsOfTheLine)
{
    auto timer = furiganaTimer("{\\k30}to{\\k30}wa{\\k20}ni", "{\\k60}永遠|えいえん{\\k20}に");
    EXPECT_EQ(readings(timer), "永|えいえん* 遠|* ");
    timer.setFurigana(false);
    EXPECT_TRUE(timer.acceptLine());
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k30}永|えいえん{\\k30}遠{\\k20}に");
    auto joined = furiganaTimer("{\\k30}to{\\k35}wa {\\k40}ni", "永遠に");
    EXPECT_TRUE(joined.setReading(u("とこ")));
    EXPECT_TRUE(joined.link());
    EXPECT_TRUE(joined.setReading(u("しえ")));
    EXPECT_TRUE(joined.unlink());
    EXPECT_TRUE(joined.growSyllables());
    EXPECT_TRUE(joined.growCharacters());
    EXPECT_EQ(readings(joined), "永遠|とこしえ* ");
    EXPECT_TRUE(joined.setReading(u("とわ")));
    EXPECT_TRUE(joined.shrinkSyllables());
    EXPECT_TRUE(joined.shrinkCharacters());
    EXPECT_EQ(readings(joined), "永|とわ* 遠|* ");
    EXPECT_TRUE(joined.useSuggestion());
    EXPECT_EQ(readings(joined), "永|と* 遠|* ");
}

TEST(KanjiTimer, ALinesReadingThatSpellsNoneOfItsKanaStaysWhole)
{
    const auto read = kanjiText(u("{\\k40}愛を|あいお"));
    EXPECT_EQ(read.readings, (std::vector<KanjiText::Reading>{{0, 2, u("あいお")}}));
    auto timer = furiganaTimer("{\\k40}aio", "{\\k40}愛を|あいお");
    EXPECT_EQ(readings(timer), "愛|あいお* ");
    EXPECT_EQ(s(*timer.view()->output), "{\\k40}愛を|あいお");
    timer.setFurigana(false);
    EXPECT_TRUE(timer.acceptLine());
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k40}愛を|あいお");
    timer.setFurigana(true);
    EXPECT_TRUE(timer.undo());
    EXPECT_TRUE(timer.setReading(u("あい")));
    EXPECT_EQ(s(*timer.view()->output), "{\\k40}愛を|あいを");
    KanjiTimer apart(classes());
    apart.setSuggest(false);
    apart.setLines({line(1, "{\\k20}ai{\\k20}okimi", 0, 600)}, {line(2, "{\\k40}愛を|あいお{\\k20}君|きみ", 0, 600)});
    EXPECT_TRUE(apart.enter());
    EXPECT_TRUE(apart.growCharacters());
    EXPECT_TRUE(apart.enter());
    EXPECT_TRUE(apart.enter());
    EXPECT_EQ(s(apart.pending()[0].second), "{\\k20}愛|あいお{\\k20}を君|きみ");
}

TEST(KanjiTimer, AWholeBaseReadingJoinsTheRunItIsGroupedWith)
{
    auto timer = furiganaTimer("{\\k20}ai{\\k40}jouo", "{\\k20}愛|あい{\\k40}情を|じょうお");
    timer.setFurigana(false);
    EXPECT_TRUE(timer.growSyllables());
    EXPECT_TRUE(timer.growCharacters());
    EXPECT_TRUE(timer.growCharacters());
    EXPECT_TRUE(timer.acceptLine());
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k60}愛情を|あいじょうお");
}

TEST(KanjiTimer, AReadingOverNoKanjiIsKeptToo)
{
    const auto read = kanjiText(u("{\\k40}きみ|キミ{\\k30}の"));
    EXPECT_EQ(s(read.text), "きみの");
    EXPECT_EQ(read.readings, (std::vector<KanjiText::Reading>{{0, 2, u("キミ")}}));
    auto timer = furiganaTimer("{\\k40}kimi{\\k30}no", "{\\k40}きみ|キミ{\\k30}の");
    EXPECT_EQ(readings(timer), "きみ|キミ ");
    timer.setFurigana(false);
    EXPECT_TRUE(timer.acceptLine());
    EXPECT_EQ(s(timer.pending()[0].second), "{\\k40}きみ|キミ{\\k30}の");
    timer.setFurigana(true);
    EXPECT_TRUE(timer.undo());
    EXPECT_TRUE(timer.setReading(u("きみ")));
    EXPECT_EQ(s(*timer.view()->output), "{\\k40}きみ|きみ{\\k30}の");
    EXPECT_TRUE(timer.useSuggestion());
    EXPECT_EQ(s(*timer.view()->output), "{\\k40}きみ{\\k30}の");
}

TEST(KanjiTimer, AReadingOverNoKanjiAfterOneOverKanjiInTheSameGroup)
{
    auto timer = furiganaTimer("{\\k40}kimi{\\k40}wa", "{\\k40}君|きみ{\\k40}は|わ");
    timer.setFurigana(false);
    EXPECT_TRUE(timer.growSyllables());
    EXPECT_TRUE(timer.growCharacters());
    EXPECT_EQ(s(*timer.view()->output), "{\\k80}君は|きみわ");
    EXPECT_TRUE(timer.setReading(u("きみ は")));
    EXPECT_EQ(s(*timer.view()->output), "{\\k80}君は|きみは");
    EXPECT_TRUE(timer.setReading(u("")));
    EXPECT_EQ(s(*timer.view()->output), "{\\k80}君は");
    EXPECT_TRUE(timer.useSuggestion());
    EXPECT_FALSE(timer.useSuggestion());
    timer.setFurigana(true);
    EXPECT_EQ(s(*timer.view()->output), "{\\k80}君は|きみは");
}

TEST(KanjiTimer, TheReadingsAppliedAreTheLinesOwnAfter)
{
    auto timer = furiganaTimer("{\\k40}kimi", "君");
    EXPECT_TRUE(timer.acceptLine());
    const std::string out = s(timer.pending()[0].second);
    EXPECT_EQ(out, "{\\k40}君|きみ");
    timer.applied();
    timer.setLines({line(1, "{\\k40}kimi", 0, 3000)}, {line(2, out, 0, 3000)});
    EXPECT_EQ(timer.pairs()[0].status, KanjiTimer::Status::Applied);
    timer.setFurigana(false);
    EXPECT_EQ(s(*timer.view()->output), out);
    ASSERT_TRUE(timer.view());
}

TEST(KanjiTimer, TheSpacesAfterAReadingStayInTheText)
{
    const auto read = kanjiText(u("{\\k40}君|きみ {\\k30}の"));
    EXPECT_EQ(s(read.text), "君 の");
    EXPECT_EQ(read.readings, (std::vector<KanjiText::Reading>{{0, 1, u("きみ")}}));
    auto timer = furiganaTimer("{\\k40}kimi {\\k30}no", "{\\k40}君|きみ {\\k30}の");
    timer.setFurigana(false);
    EXPECT_TRUE(timer.growSyllables());
    EXPECT_TRUE(timer.growCharacters());
    EXPECT_EQ(s(*timer.view()->output), "{\\k70}君 の|きみの");
    auto apart = furiganaTimer("{\\k40}kimi {\\k30}no", "{\\k40}君|きみ {\\k30}の");
    apart.setFurigana(false);
    EXPECT_TRUE(apart.acceptLine());
    EXPECT_EQ(s(apart.pending()[0].second), "{\\k40}君|きみ {\\k30}の");
}

TEST(KanjiTimer, TheDictionaryReadsAKanjiRunThroughItsBlocks)
{
    auto timer = furiganaTimer("{\\k45}love", "愛{\\i1}情");
    timer.setDictionary(dictionary({{"愛", "アイ"}, {"情", "ジョウ"}}));
    EXPECT_TRUE(timer.growCharacters());
    EXPECT_EQ(readings(timer), "愛情|あいじょう ");
    EXPECT_EQ(s(*timer.view()->output), "{\\k45}愛{\\i1}情|あいじょう");
}

TEST(KanjiTimer, AReadingOfManyKanjiAndKanaReadsBackQuickly)
{
    std::string line = "{\\k10}", reading;
    for (int i = 0; i < 2000; ++i) {
        line += "漢あ";
        reading += "あああ";
    }
    const auto read = kanjiText(u(line + "|" + reading + "い"));
    ASSERT_EQ(read.readings.size(), 1u);
    EXPECT_EQ(read.readings[0].begin, 0u);
    EXPECT_EQ(read.readings[0].end, 4000u);
}
