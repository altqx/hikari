#include "hikari/application/audio_karaoke.h"
#include "hikari/application/karaoke_split.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <fstream>
#include <random>
#include <sstream>
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

const Hyphenation &english()
{
    static const Hyphenation patterns = [] {
        std::ifstream in(HIKARI_HYPHENATION_PATTERNS, std::ios::binary);
        std::stringstream all;
        all << in.rdbuf();
        return Hyphenation::fromPatterns(all.str());
    }();
    return patterns;
}

struct Rules {
    bool everyN = false, doubleApart = false, hyphenate = true;
};

std::string syllables(std::string_view text, Rules rules = {})
{
    const std::u16string t = u(text);
    const KaraokeSplitRules split{rules.everyN, rules.doubleApart, rules.hyphenate ? &english() : nullptr};
    const auto starts = karaokeAutoSplit(t, split, KaraokeCharClass::ascii());
    std::string out;
    for (std::size_t i = 0; i < starts.size(); i++) {
        const std::size_t end = i + 1 < starts.size() ? starts[i + 1] : t.size();
        out += (i ? "|" : "") + s(std::u16string_view(t).substr(starts[i], end - starts[i]));
    }
    return out;
}

AudioKaraoke kara(std::string_view text, int start, int end)
{
    AudioKaraoke k;
    k.split({u(text), start, end}, false, {});
    return k;
}

std::vector<std::string> strings(const std::vector<std::u16string> &values)
{
    std::vector<std::string> out;
    for (const auto &v : values)
        out.push_back(s(v));
    return out;
}

}

TEST(KaraokeAutoSplit, RomajiSplitsByMora)
{
    EXPECT_EQ(syllables("karaoke"), "ka|ra|o|ke");
    EXPECT_EQ(syllables("kyou wa"), "kyo|u |wa");
    EXPECT_EQ(syllables("shinjitsu"), "shi|n|ji|tsu");
    EXPECT_EQ(syllables("sempai"), "se|m|pa|i");
    EXPECT_EQ(syllables("T\xC5\x8Dky\xC5\x8D"), "T\xC5\x8D|ky\xC5\x8D");
    EXPECT_EQ(syllables("Toukyou"), "To|u|kyo|u");
    EXPECT_EQ(syllables("hon"), "hon");
    EXPECT_EQ(syllables("zen'in"), "ze|n'|in");
}

TEST(KaraokeAutoSplit, TheNOption)
{
    EXPECT_EQ(syllables("konnichiwa"), "ko|n|ni|chi|wa");
    EXPECT_EQ(syllables("konnichiwa", {.everyN = true}), "kon|ni|chi|wa");
    EXPECT_EQ(syllables("shinjitsu", {.everyN = true}), "shin|ji|tsu");
    EXPECT_EQ(syllables("sempai", {.everyN = true}), "sem|pa|i");
    EXPECT_EQ(syllables("zen'in", {.everyN = true}), "zen'|in");
    EXPECT_EQ(syllables("\xE3\x81\xBB\xE3\x82\x93\xE3\x81\xA8\xE3\x81\x86"),
              "\xE3\x81\xBB|\xE3\x82\x93|\xE3\x81\xA8|\xE3\x81\x86");
    EXPECT_EQ(syllables("\xE3\x81\xBB\xE3\x82\x93\xE3\x81\xA8\xE3\x81\x86", {.everyN = true}),
              "\xE3\x81\xBB\xE3\x82\x93|\xE3\x81\xA8|\xE3\x81\x86");
}

TEST(KaraokeAutoSplit, TheDoubleConsonantOption)
{
    EXPECT_EQ(syllables("somatta"), "so|mat|ta");
    EXPECT_EQ(syllables("gakkou"), "gak|ko|u");
    EXPECT_EQ(syllables("matcha"), "mat|cha");
    EXPECT_EQ(syllables("\xE3\x81\x8C\xE3\x81\xA3\xE3\x81\x93\xE3\x81\x86"),
              "\xE3\x81\x8C\xE3\x81\xA3|\xE3\x81\x93|\xE3\x81\x86");
    EXPECT_EQ(syllables("somatta", {.doubleApart = true}), "so|ma|t|ta");
    EXPECT_EQ(syllables("gakkou", {.doubleApart = true}), "ga|k|ko|u");
    EXPECT_EQ(syllables("matcha", {.doubleApart = true}), "ma|t|cha");
    EXPECT_EQ(syllables("\xE3\x81\x8C\xE3\x81\xA3\xE3\x81\x93\xE3\x81\x86", {.doubleApart = true}),
              "\xE3\x81\x8C|\xE3\x81\xA3|\xE3\x81\x93|\xE3\x81\x86");
}

TEST(KaraokeAutoSplit, KanaSplitsByMora)
{
    EXPECT_EQ(syllables("\xE3\x81\x8B\xE3\x82\x89\xE3\x81\x8A\xE3\x81\x91"),
              "\xE3\x81\x8B|\xE3\x82\x89|\xE3\x81\x8A|\xE3\x81\x91");
    EXPECT_EQ(syllables("\xE3\x81\x8D\xE3\x82\x87\xE3\x81\x86"), "\xE3\x81\x8D\xE3\x82\x87|\xE3\x81\x86");
    EXPECT_EQ(syllables("\xE3\x81\x8B\xE3\x83\xB6\xE3\x81\x86"), "\xE3\x81\x8B\xE3\x83\xB6|\xE3\x81\x86");
    EXPECT_EQ(syllables("\xE4\xB8\x80\xE3\x83\xB6\xE6\x9C\x88"), "\xE4\xB8\x80|\xE3\x83\xB6|\xE6\x9C\x88");
    EXPECT_EQ(syllables("\xF0\x9B\x80\x82\xF0\x9B\x80\x83"), "\xF0\x9B\x80\x82|\xF0\x9B\x80\x83");
    EXPECT_EQ(syllables("\xE3\x81\x8B\xF0\x9B\x85\x90\xE3\x81\x86"), "\xE3\x81\x8B\xF0\x9B\x85\x90|\xE3\x81\x86");
    EXPECT_EQ(syllables("\xE3\x83\xA9\xE3\x83\xBC\xE3\x83\xA1\xE3\x83\xB3"),
              "\xE3\x83\xA9|\xE3\x83\xBC|\xE3\x83\xA1\xE3\x83\xB3");
    EXPECT_EQ(syllables("\xEF\xBD\xB6\xEF\xBE\x9E\xEF\xBE\x97"), "\xEF\xBD\xB6\xEF\xBE\x9E|\xEF\xBE\x97");
}

TEST(KaraokeAutoSplit, KanjiAndHangulOneCharacterEach)
{
    EXPECT_EQ(syllables("\xF0\xB1\x8D\x90\xF0\xB1\x8D\x91"), "\xF0\xB1\x8D\x90|\xF0\xB1\x8D\x91");
    EXPECT_EQ(syllables("\xE6\x9D\xB1\xE4\xBA\xAC"), "\xE6\x9D\xB1|\xE4\xBA\xAC");
    EXPECT_EQ(syllables("\xEC\x82\xAC\xEB\x9E\x91\xED\x95\xB4"), "\xEC\x82\xAC|\xEB\x9E\x91|\xED\x95\xB4");
    EXPECT_EQ(syllables("\xEF\xBE\xA1\xEF\xBF\x82"), "\xEF\xBE\xA1|\xEF\xBF\x82");
    EXPECT_EQ(syllables("\xE5\x90\x9B\xE3\x81\xAE\xE5\x90\x8D\xE3\x81\xAF"),
              "\xE5\x90\x9B|\xE3\x81\xAE|\xE5\x90\x8D|\xE3\x81\xAF");
    EXPECT_EQ(syllables("\xF0\xA0\xAE\xB7\xE9\x87\x8E"), "\xF0\xA0\xAE\xB7|\xE9\x87\x8E");
    EXPECT_EQ(syllables("\xE1\x84\x89\xE1\x85\xA1\xE1\x84\x85\xE1\x85\xA1\xE1\x86\xBC"),
              "\xE1\x84\x89\xE1\x85\xA1|\xE1\x84\x85\xE1\x85\xA1\xE1\x86\xBC");
}

TEST(KaraokeAutoSplit, EnglishByHyphenation)
{
    ASSERT_FALSE(english().empty());
    EXPECT_EQ(english().breaks(u"hyphenation"), (std::vector<std::size_t>{2, 6}));
    EXPECT_EQ(english().breaks(u"cat"), std::vector<std::size_t>{});
    EXPECT_EQ(english().breaks(u"dreams"), std::vector<std::size_t>{});
    EXPECT_EQ(syllables("beautiful"), "beau|ti|ful");
    EXPECT_EQ(syllables("I want to hold your hand"), "I |want |to |hold |your |hand");
    EXPECT_EQ(syllables("Take me home tonight"), "Take |me |home |tonight");
    EXPECT_EQ(syllables("happy story"), "hap|py |sto|ry");
    EXPECT_EQ(syllables("a\xEF\xAC\x83nity"), "a\xEF\xAC\x83n|i|ty");
    EXPECT_EQ(syllables("a\xEA\x9E\xB5" "a"), "a\xEA\x9E\xB5" "a");
    EXPECT_EQ(syllables("ka\xC9\x90ra"), "ka\xC9\x90ra");
    EXPECT_EQ(syllables("beautiful", {.hyphenate = false}), "beautiful");
}

TEST(KaraokeAutoSplit, MixedLines)
{
    EXPECT_EQ(syllables("kimi wo love shiteru"), "ki|mi |wo |love |shi|te|ru");
    EXPECT_EQ(syllables("dakishimete beautiful"), "da|ki|shi|me|te |beau|ti|ful");
    EXPECT_EQ(syllables("\xE5\x90\x9B\xE3\x81\xA8" "dance"), "\xE5\x90\x9B|\xE3\x81\xA8|dance");
    EXPECT_EQ(syllables("\xE6\x9D\xB1\xE4\xBA\xAC love story"), "\xE6\x9D\xB1|\xE4\xBA\xAC |love |sto|ry");
    EXPECT_EQ(syllables("KARAOKE nani"), "KA|RA|O|KE |na|ni");
    const auto decomposed = [](std::string text) {
        for (const auto &[composed, nfd] : {std::pair<std::string, std::string>{"\xC3\xA9", "e\xCC\x81"},
                                            {"\xC5\x8D", "o\xCC\x84"}, {"\xC3\xBB", "u\xCC\x82"}})
            for (std::size_t at; (at = text.find(composed)) != std::string::npos;)
                text.replace(at, composed.size(), nfd);
        return text;
    };
    for (const char *line : {"caf\xC3\xA9 caf\xC3\xA9 take", "t\xC5\x8Dky\xC5\x8D", "k\xC3\xBBki"})
        EXPECT_EQ(syllables(decomposed(line)), decomposed(syllables(line))) << line;
    EXPECT_EQ(syllables("caf\xC3\xA9 caf\xC3\xA9 take").find("ta|ke"), std::string::npos);
}

TEST(KaraokeAutoSplit, SpacesPunctuationAndBlocks)
{
    EXPECT_EQ(syllables("sakura, sakura"), "sa|ku|ra, |sa|ku|ra");
    EXPECT_EQ(syllables("kimi \"ai\""), "ki|mi |\"a|i\"");
    EXPECT_EQ(syllables("ka\\Nra"), "ka\\N|ra");
    EXPECT_EQ(syllables("{\\b1}ka{\\b0}ra"), "{\\b1}ka|{\\b0}ra");
    EXPECT_EQ(syllables("I don'{\\i1}t know"), "I |don'{\\i1}t |know");
    EXPECT_EQ(syllables("I don't know"), "I |don't |know");
    EXPECT_EQ(syllables("kara{ke"), "ka|ra{ke");
    EXPECT_EQ(syllables("2kara"), "2ka|ra");
    EXPECT_EQ(syllables("ka$ra"), "ka|$ra");
    auto classes = KaraokeCharClass::ascii();
    classes.punct = [](char32_t c) { return c == 0x10100 || KaraokeCharClass::ascii().punct(c); };
    const std::u16string aegean = u"ka\U00010100 ra";
    EXPECT_EQ(karaokeAutoSplit(aegean, {}, classes), (std::vector<std::size_t>{0, 5}));
    EXPECT_EQ(karaokeWeight(aegean, classes), 4);
}

TEST(KaraokeAutoSplit, ALongerLineSplitsAtSpaces)
{
    std::string most = "{\\b1}";
    for (std::size_t i = 0; i < kKaraokeAutoSplitMost / 2; i++)
        most += "ka";
    const auto split = syllables(most);
    EXPECT_EQ(std::count(split.begin(), split.end(), '|'), long(kKaraokeAutoSplitMost / 2 - 1));
    EXPECT_EQ(syllables(most + "k"), "");
    std::string longer(4'000'000, 'a');
    longer += " kara";
    const auto began = std::chrono::steady_clock::now();
    EXPECT_EQ(syllables(longer), "");
    AudioKaraoke k;
    k.split({u(longer), 0, 1000}, true, {});
    EXPECT_EQ(k.count(), 2);
    EXPECT_EQ(s(k.syllables()[1]), "kara");
    EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(10));
}

TEST(KaraokeWeight, VisibleCharactersOnly)
{
    const auto ascii = KaraokeCharClass::ascii();
    EXPECT_EQ(karaokeWeight(u"ka, ra", ascii), 4);
    EXPECT_EQ(karaokeWeight(u"{\\b1}ka", ascii), 2);
    EXPECT_EQ(karaokeWeight(u"ka\\Nra\\h", ascii), 4);
    EXPECT_EQ(karaokeWeight(u"don't", ascii), 5);
    EXPECT_EQ(karaokeWeight(u"'ka'", ascii), 2);
    EXPECT_EQ(karaokeWeight(u"ka$", ascii), 3);
    EXPECT_EQ(karaokeWeight(u(" \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD" "a"), ascii), 2);
    EXPECT_EQ(karaokeWeight(u"é", ascii), 1);
}

TEST(KaraokeTimeByCharacter, NewSyllablesShareTheLineByCharacter)
{
    AudioKaraoke k;
    k.split({u"sakura saku", 0, 1000}, true, {});
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"sa", "ku", "ra ", "sa", "ku"}));
    EXPECT_EQ(k.times(), (std::vector<int>{200, 400, 600, 800, 1000}));
    k.split({u"ka ra\\Nkiss", 0, 800}, false, {});
    EXPECT_EQ(k.times(), (std::vector<int>{200, 400, 800}));
    k.split({u"... ka", 0, 1000}, false, {});
    EXPECT_EQ(k.times(), (std::vector<int>{330, 1000}));
}

TEST(KaraokeGraphemes, ALetterIsNeverHalfACharacter)
{
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\U00020BB7", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"é", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"❤️", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\U0001F468‍\U0001F469", 3));
    EXPECT_FALSE(karaokeContinuesGrapheme(u"ab", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\u1112\u1161\u11AB", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\u1112\u1161\u11AB", 2));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\uD558\u11AB", 1));
    EXPECT_FALSE(karaokeContinuesGrapheme(u"\uD55C\u1161", 1));
    EXPECT_FALSE(karaokeContinuesGrapheme(u"\u11AB\u1112", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\u0915\u093E", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\u0B95\u0BCA", 1));
    EXPECT_TRUE(karaokeContinuesGrapheme(u"\U0001F1EF\U0001F1F5", 2));
    EXPECT_FALSE(karaokeContinuesGrapheme(u"\U0001F1EF\U0001F1F5\U0001F1F0\U0001F1F7", 4));
    EXPECT_FALSE(karaokeContinuesGrapheme(u"\u0915\u0916", 1));
    const std::u16string marked = u"a" + std::u16string(200, u'\u0301') + u"b";
    EXPECT_EQ(karaokeGraphemePrefix(marked, 1), 201u);
    EXPECT_EQ(karaokeGraphemePrefix(marked, 2), 202u);
    EXPECT_EQ(karaokeGraphemePrefix(u"\U00020BB7a", 1), 2u);
    EXPECT_EQ(karaokeGraphemePrefix(u"éx", 1), 2u);

    auto k = kara("{\\k20}\xF0\xA0\xAE\xB7\xE9\x87\x8E", 0, 1000);
    EXPECT_EQ(k.letters(0, 1), std::pair(u("{}\xF0\xA0\xAE\xB7"), u("\xE9\x87\x8E")));
    ASSERT_TRUE(k.splitSyllable(0, 1, 0));
    EXPECT_EQ(s(k.text(0)), "{\\k10}\xF0\xA0\xAE\xB7{\\k10}\xE9\x87\x8E");
    auto jamo = kara("{\\k20}\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB\xE1\x84\x80\xE1\x85\xAE\xE1\x86\xA8", 0, 1000);
    EXPECT_EQ(jamo.letters(0, 1), std::pair(u("{}\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB"),
                                            u("\xE1\x84\x80\xE1\x85\xAE\xE1\x86\xA8")));
    auto hindi = kara("{\\k30}\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87", 0, 1000);
    EXPECT_EQ(hindi.letters(0, 1), std::pair(u("{}\xE0\xA4\xA8"), u("\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87")));
    EXPECT_EQ(hindi.letters(0, 2).first, u("{}\xE0\xA4\xA8\xE0\xA4\xAE"));
    auto blocked = kara("{\\k20}a{\\b1}\xCC\x81" "b", 0, 1000);
    EXPECT_EQ(blocked.letters(0, 1), std::pair(u("{}a{\\b1}\xCC\x81"), u("b")));
    EXPECT_EQ(karaokeWeight(u("a{\\b1}\xCC\x81" "b"), KaraokeCharClass::ascii()), 2);
    EXPECT_EQ(karaokeLetterEnd(u"a{\\b1}b", 0), 1u);
    EXPECT_EQ(karaokeLetterEnd(u"a{\\b1", 0), 1u);
    const std::u16string marks(60, u'\u0301');
    const std::u16string joined = u"\U0001F600" + marks + u"\u200D\U0001F600" + u"b";
    EXPECT_EQ(karaokeGraphemePrefix(joined, 1), joined.size() - 1);
    const std::u16string blocked2 = u"\U0001F600{\\b1}" + std::u16string(62, u'\u0301') + u"\u200D\U0001F600" + u"b";
    EXPECT_EQ(karaokeLetterEnd(blocked2, 0), blocked2.size() - 1);
    auto breaks = kara("{\\k20}a\\Nb", 0, 1000);
    EXPECT_EQ(breaks.letters(0, 2), std::pair(std::u16string(u"{}a\\N"), std::u16string(u"b")));
}

TEST(KaraokeRoundTrip, TaggedLinesComeBackUnchanged)
{
    const char *lines[] = {
        "{\\k20}ka{\\k30}ra{\\k50}o{\\k40}ke",
        "{\\kf20}ka{\\ko30}ra{\\K50}o{\\k40}ke",
        "{\\k20\\fs20}ka{\\fs30\\k30}ra{\\k10}{\\i1}o{\\k40}ke{\\i0}",
        "{\\an8\\k20}ka{\\k30}ra",
        "{\\an8}{\\k20}ka{\\k30}ra",
        "{\\k20}{\\b1}ka{\\k30}{\\b0}ra",
        "{\\k20}ka {\\k30}ra\\N{\\k25}o",
        "{\\k0}{\\k20}ka{\\k0}",
        "{\\k20\\-singer1}ka{\\k30\\1c&H0000FF&}ra",
        "{\\k20}ka{\\b1}{\\k30}ra",
        "ab{\\k20}cd{\\k30}ef",
        "{\\kf150}long{\\k5}",
        "{\\K15}ka{\\K25}ra",
        "{\\k020}ka{\\k30}ra",
        "{\\k10\\k20}x",
        "{\\k20}\xE3\x81\x8B{\\k30}\xE3\x82\x89",
        "{\\k20}ka{note}{\\k30}ra",
        "{\\k20}ka{\\k30}ra{\\i1}",
        "{\\k12.5}ka{\\k30}ra",
        "{\\k20}ka{}ra{\\k30}to",
        "{\\k20}ka{\\k30}ra{\\kt10}",
    };
    for (const char *line : lines) {
        SCOPED_TRACE(line);
        AudioKaraoke k;
        k.split({u(line), 1000, 9000}, true, {});
        EXPECT_EQ(s(k.text(1000)), line);
    }
}

TEST(KaraokeRoundTrip, OnlyTheChangedTimesAreWrittenAgain)
{
    auto k = kara("{\\k020}ka{\\k12.5}ra{\\k30}o", 1000, 9000);
    k.times()[1] += 100;
    EXPECT_EQ(s(k.text(1000)), "{\\k020}ka{\\k22}ra{\\k20}o");
}

TEST(KaraokeRoundTrip, KtIsNoSyllable)
{
    auto k = kara("{\\k20}ka{\\kt10}{\\k30}ra", 1000, 9000);
    EXPECT_EQ(strings(k.tags()), (std::vector<std::string>{"k", "k"}));
    EXPECT_EQ(k.times(), (std::vector<int>{1200, 1500}));
    EXPECT_EQ(s(k.text(1000)), "{\\k20}ka{\\kt10}{\\k30}ra");
}

TEST(KaraokeSplitAt, TheTimeChoosesTheLetter)
{
    auto k = kara("{\\k100}sakura", 0, 2000);
    ASSERT_TRUE(k.splitAt(0, 334, 0));
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"{}sa", "kura"}));
    EXPECT_EQ(k.times(), (std::vector<int>{330, 1000}));
    EXPECT_EQ(s(k.text(0)), "{\\k33}sa{\\k67}kura");
    auto spaced = kara("{\\k100}ka, ra", 0, 2000);
    ASSERT_TRUE(spaced.splitAt(0, 500, 0));
    EXPECT_EQ(strings(spaced.syllables()), (std::vector<std::string>{"{}ka, ", "ra"}));
    auto edge = kara("{\\k100}sakura", 0, 2000);
    ASSERT_TRUE(edge.splitAt(0, 40, 0));
    EXPECT_EQ(s(edge.text(0)), "{\\k4}{\\k96}sakura");
}

TEST(KaraokeSplitAt, OnlyInsideTheSyllable)
{
    auto k = kara("{\\k50}ka{\\k50}ra", 0, 2000);
    EXPECT_FALSE(k.splitAt(0, 0, 0));
    EXPECT_FALSE(k.splitAt(0, 500, 0));
    EXPECT_FALSE(k.splitAt(0, 5, 0));
    EXPECT_FALSE(k.splitAt(1, 300, 0));
    EXPECT_FALSE(k.splitAt(2, 800, 0));
    EXPECT_EQ(k.count(), 2);
}

TEST(KaraokeMoveStart, AnEarlierStartIsALeadSyllable)
{
    auto k = kara("{\\k50}ka{\\k50}ra", 1000, 2000);
    k.current = 1;
    k.moveStart(1000, 800);
    EXPECT_EQ(strings(k.syllables()), (std::vector<std::string>{"", "{}ka", "{}ra"}));
    EXPECT_EQ(k.times(), (std::vector<int>{1000, 1500, 2000}));
    EXPECT_EQ(k.current, 2);
    EXPECT_EQ(s(k.text(800)), "{\\k20}{\\k50}ka{\\k50}ra");
    k.moveStart(800, 600);
    EXPECT_EQ(k.count(), 3);
    EXPECT_EQ(s(k.text(600)), "{\\k40}{\\k50}ka{\\k50}ra");
    k.moveStart(600, 1000);
    EXPECT_EQ(s(k.text(1000)), "{\\k50}ka{\\k50}ra");
    EXPECT_EQ(k.current, 1);
}

TEST(KaraokeMoveStart, ALaterStartCutsTheSyllablesItPasses)
{
    auto k = kara("{\\k50}ka{\\k50}ra", 1000, 2000);
    k.moveStart(1000, 1600);
    EXPECT_EQ(k.times(), (std::vector<int>{1600, 2000}));
    EXPECT_EQ(s(k.text(1600)), "{\\k0}ka{\\k40}ra");
    auto lead = kara("{\\k30}{\\k50}ka", 1000, 2000);
    lead.moveStart(1000, 1100);
    EXPECT_EQ(s(lead.text(1100)), "{\\k20}{\\k50}ka");
}

TEST(KaraokeMoveStart, ALineReadInPiecesIsTheWholeLine)
{
    const std::vector<std::u16string> parts{
        u"{\\k10}", u"{\\K25}", u"{\\kf7}", u"{\\ko0}", u"{\\k0}", u"{\\b1}", u"{\\b1\\k12}", u"{\\k3\\i1}",
        u"{\\kt50}", u"{\\k}", u"{\\kx}", u"{\\k5\\k6}", u"ka", u"ra", u" ", u"{", u"}", u"\\k5", u"\\N", u"o\u0301"};
    const std::vector<std::pair<int, int>> starts{{1000, 800}, {1000, 1010}, {1000, 1500}, {1000, 995}, {1000, 3000}};
    std::mt19937 random(276);
    for (int n = 0; n < 4000; n++) {
        std::u16string text = u"{\\k20}";
        const int count = static_cast<int>(random() % 24);
        for (int i = 0; i < count; i++)
            text += parts[random() % parts.size()];
        const auto [from, to] = starts[random() % starts.size()];
        EXPECT_EQ(karaokeKeepSyllables(text, from, to, 9000, 2), karaokeKeepSyllables(text, from, to, 9000, INT_MAX))
            << s(text) << " " << from << "->" << to;
    }
    std::u16string text = u"{\\b1}";
    for (int i = 0; i < 3000; i++)
        text += i % 3 ? u"{\\k0}" : u"{\\k2}ka";
    for (const auto &[from, to] : starts)
        EXPECT_EQ(karaokeKeepSyllables(text, from, to, 9000), karaokeKeepSyllables(text, from, to, 9000, INT_MAX));
}

TEST(KaraokeMoveStart, OneHugeBlockLeavesTheTextAsItIs)
{
    std::u16string fits = u"{";
    for (int i = 0; i < 16 * 1024; i++)
        fits += u"\\k0";
    fits += u"}x";
    EXPECT_NE(karaokeKeepSyllables(fits, 1000, 800, 9000), std::nullopt);
    std::u16string block = u"{";
    for (int i = 0; i < 1000000; i++)
        block += u"\\k0";
    block += u"}x";
    const auto began = std::chrono::steady_clock::now();
    EXPECT_EQ(karaokeKeepSyllables(block, 1000, 800, 9000), std::nullopt);
    EXPECT_EQ(karaokeKeepSyllables(block, 1000, 800, 9000, 2), std::nullopt);
    EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(1));
}

TEST(KaraokeMoveStart, TheLinesTextKeepsItsSyllables)
{
    EXPECT_EQ(karaokeKeepSyllables(u"{\\k50}ka{\\k50}ra", 1000, 800, 9000),
              std::optional<std::u16string>(u"{\\k20}{\\k50}ka{\\k50}ra"));
    EXPECT_EQ(karaokeKeepSyllables(u"{\\b1}ka{\\k50}ra", 1000, 800, 9000),
              std::optional<std::u16string>(u"{\\k20}{\\b1}ka{\\k50}ra"));
    EXPECT_EQ(karaokeKeepSyllables(u"karaoke", 1000, 800, 9000), std::nullopt);
    EXPECT_EQ(karaokeKeepSyllables(u"{\\k50}ka", 1000, 1000, 9000), std::nullopt);
    EXPECT_EQ(karaokeKeepSyllables(u"\\k50 ka{\\b1}", 1000, 800, 9000), std::nullopt);
    EXPECT_EQ(karaokeKeepSyllables(u"{\\b1}{\\k50 ka", 1000, 800, 9000), std::nullopt);
    EXPECT_EQ(karaokeKeepSyllables(u"{\\k20}ka{\\k30", 1000, 800, 2000), std::nullopt);
    EXPECT_EQ(karaokeKeepSyllables(u"{\\k20}ka{\\k30", 1000, 1300, 2000), std::nullopt);
    EXPECT_NE(karaokeKeepSyllables(u"{\\k20}ka{\\k30}ra{", 1000, 800, 2000), std::nullopt);
    std::u16string unclosed = u"{";
    for (int i = 0; i < 100000; i++)
        unclosed += u"\\k0";
    const auto began = std::chrono::steady_clock::now();
    EXPECT_EQ(karaokeKeepSyllables(unclosed, 1000, 800, 9000), std::nullopt);
    EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(1));
}

TEST(KaraokeRoundTrip, LongLinesAreReadAndWrittenInOnePass)
{
    std::u16string prefixed(2'000'000, u'a');
    prefixed += u"{";
    for (int i = 0; i < 16384; i++)
        prefixed += u"\\k0";
    prefixed += u"}x";
    const auto start = std::chrono::steady_clock::now();
    EXPECT_NE(karaokeKeepSyllables(prefixed, 1000, 800, 9000), std::nullopt);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));

    std::string block = "{";
    for (int i = 0; i < 20000; i++)
        block += "\\k0\\bord1";
    block += "}x";
    auto began = std::chrono::steady_clock::now();
    auto tags = kara(block, 0, 1000);
    EXPECT_EQ(s(tags.text(0)), block);
    EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(2));

    const std::string letters(200000, 'a');
    began = std::chrono::steady_clock::now();
    auto plain = kara("{\\k100}" + letters, 0, 1000);
    EXPECT_EQ(plain.letters(0, 100000).second.size(), 100000u);
    EXPECT_EQ(karaokeWeight(u(letters), KaraokeCharClass::ascii()), 200000);
    EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(2));

    std::u16string cut = u"a";
    for (int i = 0; i < 100000; i++)
        cut += u"{x}\u0301";
    began = std::chrono::steady_clock::now();
    EXPECT_GT(karaokeLetterEnd(cut, 0), 4u * 16);
    EXPECT_GT(karaokeWeight(cut, KaraokeCharClass::ascii()), 0);
    EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(2));
}

TEST(KaraokeKfSwitch, LongSyllablesSwitchInOneStep)
{
    auto k = kara("{\\k20}ka{\\k150}raa{\\k120}o{\\K150}ke", 0, 9000);
    ASSERT_TRUE(k.switchLongToKf(1000, 0));
    EXPECT_EQ(strings(k.tags()), (std::vector<std::string>{"k", "kf", "kf", "K"}));
    EXPECT_EQ(s(k.text(0)), "{\\k20}ka{\\kf150}raa{\\kf120}o{\\K150}ke");
    ASSERT_TRUE(k.switchLongToKf(1000, 0));
    EXPECT_EQ(s(k.text(0)), "{\\k20}ka{\\k150}raa{\\k120}o{\\K150}ke");
    auto mixed = kara("{\\kf150}a{\\k150}b", 0, 9000);
    ASSERT_TRUE(mixed.switchLongToKf(1000, 0));
    EXPECT_EQ(strings(mixed.tags()), (std::vector<std::string>{"kf", "kf"}));
    auto short_ = kara("{\\k20}ka{\\k30}ra", 0, 9000);
    EXPECT_FALSE(short_.switchLongToKf(1000, 0));
    EXPECT_EQ(strings(short_.tags()), (std::vector<std::string>{"k", "k"}));
}
