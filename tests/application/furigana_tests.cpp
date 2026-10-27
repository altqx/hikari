// KA4 (#279): furigana reading suggestions over a stand-in analyser: the
// split of a word's reading over its kanji runs, the runs of a Line's text
// (override blocks and line breaks, offsets, words without a reading or a
// split), typed readings kept over suggestions, morae, and the stock
// furigana karaoke syntax written and read back.

#include "hikari/application/furigana.h"

#include <gtest/gtest.h>

#include <map>
#include <tuple>

using namespace hikari::application;

namespace {

// Words by longest match against a fixed dictionary; other characters one
// word each with no reading, as MeCab's unknown words.
ReadingAnalyzer dictionary(std::map<std::u16string, std::u16string> words)
{
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
            if (!reading) {
                out.push_back({std::u16string(text.substr(i, 1)), {}});
                ++i;
                continue;
            }
            out.push_back({std::u16string(text.substr(i, best)), *reading});
            i += best;
        }
        return out;
    };
}

FuriganaRun run(std::size_t begin, std::u16string base, std::u16string suggested,
                std::optional<std::u16string> typed = std::nullopt)
{
    const std::size_t end = begin + base.size();
    return {begin, end, std::move(base), std::move(suggested), std::move(typed)};
}

} // namespace

TEST(Furigana, KanjiAndKatakana)
{
    EXPECT_TRUE(isKanji(U'漢'));
    EXPECT_TRUE(isKanji(U'々'));
    EXPECT_TRUE(isKanji(U'ヶ'));
    EXPECT_TRUE(isKanji(U'𠮟'));
    EXPECT_FALSE(isKanji(U'か'));
    EXPECT_FALSE(isKanji(U'カ'));
    EXPECT_FALSE(isKanji(U'A'));
    EXPECT_EQ(hiragana(u"エイエン"), u"えいえん");
    EXPECT_EQ(hiragana(u"ヴァ・ーヽ"), u"ゔぁ・ーゝ");
}

TEST(Furigana, AWordsReadingSplitsOverItsKanjiRuns)
{
    using Parts = std::vector<std::u16string>;
    EXPECT_EQ(splitReading(u"抱きしめ", u"ダキシメ"), (Parts{u"だ"}));
    EXPECT_EQ(splitReading(u"生まれ変わっ", u"ウマレカワッ"), (Parts{u"う", u"か"}));
    EXPECT_EQ(splitReading(u"一人", u"ヒトリ"), (Parts{u"ひとり"}));
    // The earliest split that fits: 気持 takes きも for the ち after it.
    EXPECT_EQ(splitReading(u"気持ち", u"きもち"), (Parts{u"きも"}));
    EXPECT_EQ(splitReading(u"お母さん", u"オカアサン"), (Parts{u"かあ"}));
    // Katakana in the word is spelt by its hiragana.
    EXPECT_EQ(splitReading(u"日ノ出", u"ヒノデ"), (Parts{u"ひ", u"で"}));
    // A reading that does not spell the kana (a pronunciation, は as わ).
    EXPECT_EQ(splitReading(u"春は来", u"ハルワキ"), std::nullopt);
    EXPECT_EQ(splitReading(u"夢", u""), std::nullopt);
}

TEST(Furigana, ASplitOfManyKanjiAndKanaIsQuick)
{
    // 漢あ repeated, read in あ: a great many ways to try, none that fits
    std::u16string surface, reading;
    for (int i = 0; i < 16; ++i) {
        surface += u"漢あ";
        reading += u"あああ";
    }
    EXPECT_EQ(splitReading(surface, reading + u"い"), std::nullopt);
    // one that fits is still the earliest
    const auto parts = splitReading(surface, reading);
    ASSERT_TRUE(parts);
    ASSERT_EQ(parts->size(), 16u);
    EXPECT_EQ(parts->front(), u"あ");
    EXPECT_EQ(parts->back(), u"あああああああああああああああああ");
    // so long that the steps run out: none, soon
    for (int i = 0; i < 3000; ++i) {
        surface += u"漢あ";
        reading += u"あああ";
    }
    EXPECT_EQ(splitReading(surface, reading + u"い"), std::nullopt);
}

TEST(Furigana, RunsOfALinesText)
{
    const auto analyze = dictionary({{u"永遠", u"エイエン"}, {u"に", u"ニ"}, {u"続く", u"ツヅク"}, {u"夢", u"ユメ"},
                                     {u"を", u"ヲ"}, {u"見", u"ミ"}});
    // Override blocks are left out; offsets are the text's.
    const std::u16string text = u"{\\k20}永遠{\\k10}に{\\k30}続く\\N夢を{\\k5}見";
    const auto runs = suggestReadings(text, analyze);
    ASSERT_EQ(runs.size(), 4u);
    EXPECT_EQ(runs[0], run(text.find(u"永遠"), u"永遠", u"えいえん"));
    EXPECT_EQ(runs[1], run(text.find(u"続"), u"続", u"つづ"));
    EXPECT_EQ(runs[2], run(text.find(u"夢"), u"夢", u"ゆめ"));
    EXPECT_EQ(runs[3], run(text.find(u"見"), u"見", u"み"));
    EXPECT_EQ(text.substr(runs[1].begin, runs[1].end - runs[1].begin), u"続");
}

TEST(Furigana, LineBreaksEndAPiece)
{
    // 二人 across \N is not one word: each piece is read on its own.
    std::vector<std::u16string> pieces;
    const ReadingAnalyzer analyze = [&](std::u16string_view piece) {
        pieces.emplace_back(piece);
        return std::vector<ReadingWord>{};
    };
    suggestReadings(u"二\\N人\\h三\\n四{\\b1}五", analyze);
    EXPECT_EQ(pieces, (std::vector<std::u16string>{u"二", u"人", u"三", u"四", u"五"}));
    // Pieces without kanji are not read, and an unclosed brace is text.
    pieces.clear();
    suggestReadings(u"かな\\N{漢", analyze);
    EXPECT_EQ(pieces, (std::vector<std::u16string>{u"{漢"}));
}

TEST(Furigana, AWordWithoutAReadingOrASplit)
{
    const auto analyze = dictionary({{u"春は来", u"ハルワキ"}, {u"お春は", u"オハルワ"}});
    // An unknown kanji: a run with no suggestion, for the user to type.
    auto runs = suggestReadings(u"魑魅", analyze);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0], run(0, u"魑", u""));
    EXPECT_EQ(runs[1], run(1, u"魅", u""));
    // No split: one run from the first kanji to the last.
    runs = suggestReadings(u"春は来", analyze);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0], run(0, u"春は来", u"はるわき"));
    // ... less the kana before it the reading spells.
    runs = suggestReadings(u"お春は", analyze);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0], run(1, u"春", u"はるわ"));
}

TEST(Furigana, AKanjiOutsideTheBasicPlane)
{
    const auto runs = suggestReadings(u"𠮟る", dictionary({{u"𠮟る", u"シカル"}}));
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0], run(0, u"𠮟", u"しか"));
    EXPECT_EQ(runs[0].end, 2u);
}

TEST(Furigana, NoAnalyserNoRuns)
{
    EXPECT_TRUE(suggestReadings(u"漢字", ReadingAnalyzer{}).empty());
}

TEST(Furigana, ATypedReadingIsNeverReplaced)
{
    const auto analyze = dictionary({{u"永遠", u"エイエン"}, {u"夢", u"ユメ"}, {u"運命", u"ウンメイ"}});
    auto runs = suggestReadings(u"永遠の夢", analyze);
    ASSERT_EQ(runs.size(), 2u);
    runs[0].typed = u"とわ";
    EXPECT_EQ(runs[0].reading(), u"とわ");
    EXPECT_EQ(runs[1].reading(), u"ゆめ");

    // Suggested again after an edit before it: the typed reading stays.
    auto again = keepTypedReadings(suggestReadings(u"運命と永遠の夢", analyze), runs);
    ASSERT_EQ(again.size(), 3u);
    EXPECT_EQ(again[0], run(0, u"運命", u"うんめい"));
    EXPECT_EQ(again[1], run(3, u"永遠", u"えいえん", u"とわ"));
    EXPECT_EQ(again[1].reading(), u"とわ");
    EXPECT_EQ(again[2], run(6, u"夢", u"ゆめ"));

    // A typed empty reading (no furigana) is kept too.
    again[2].typed = u"";
    again = keepTypedReadings(suggestReadings(u"運命と永遠の夢", analyze), again);
    EXPECT_EQ(again[2].reading(), u"");

    // A removed run's reading goes with it: a 永遠 written again is new.
    again = keepTypedReadings(suggestReadings(u"運命の夢", analyze), again);
    ASSERT_EQ(again.size(), 2u);
    EXPECT_EQ(again[1], run(3, u"夢", u"ゆめ", u""));
    again = keepTypedReadings(suggestReadings(u"運命の夢と永遠", analyze), again);
    ASSERT_EQ(again.size(), 3u);
    EXPECT_EQ(again[1], run(3, u"夢", u"ゆめ", u""));
    EXPECT_EQ(again[2], run(5, u"永遠", u"えいえん"));
}

TEST(Furigana, Morae)
{
    using Morae = std::vector<std::u16string>;
    EXPECT_EQ(morae(u"きょう"), (Morae{u"きょ", u"う"}));
    EXPECT_EQ(morae(u"がっこう"), (Morae{u"が", u"っ", u"こ", u"う"}));
    EXPECT_EQ(morae(u"らーめん"), (Morae{u"ら", u"ー", u"め", u"ん"}));
    EXPECT_EQ(morae(u"ふぁいと"), (Morae{u"ふぁ", u"い", u"と"}));
    EXPECT_EQ(morae(u"ゃあ"), (Morae{u"ゃ", u"あ"}));
    EXPECT_TRUE(morae(u"").empty());
}

TEST(Furigana, TheStockFuriganaSyntax)
{
    // Aegisub's furigana karaoke example: 中国魂, a mora per highlight.
    std::vector<std::u16string> line;
    for (const auto &[base, reading, br] : std::vector<std::tuple<std::u16string, std::u16string, char16_t>>{
             {u"中", u"ちゅう", 0}, {u"国", u"ごく", 0}, {u"魂", u"たましい", u'!'}}) {
        for (const auto &syllable : runSyllables(base, morae(reading), br))
            line.push_back(u"{\\k10}" + syllableText(syllable));
    }
    std::u16string text;
    for (const auto &s : line)
        text += s;
    EXPECT_EQ(text, u"{\\k10}中|ちゅ{\\k10}#|う{\\k10}国|ご{\\k10}#|く{\\k10}魂|!た{\\k10}#|ま{\\k10}#|し{\\k10}#|い");

    // One syllable for the whole run, and a run with no reading.
    EXPECT_EQ(syllableText(runSyllables(u"永遠", {u"とわ"}).front()), u"永遠|とわ");
    const auto bare = runSyllables(u"魑", {});
    ASSERT_EQ(bare.size(), 1u);
    EXPECT_EQ(syllableText(bare.front()), u"魑");
    EXPECT_EQ(syllableText({u"夢", u"", u'!'}), u"夢");
}

TEST(Furigana, SyllablesReadBack)
{
    EXPECT_EQ(parseSyllable(u"魂|!た"), (FuriganaSyllable{u"魂", u"た", u'!'}));
    EXPECT_EQ(parseSyllable(u"中|<ちゅ"), (FuriganaSyllable{u"中", u"ちゅ", u'<'}));
    EXPECT_EQ(parseSyllable(u"#|う"), (FuriganaSyllable{u"#", u"う", 0}));
    // The full-width forms read as the half-width ones.
    EXPECT_EQ(parseSyllable(u"魂｜！た"), (FuriganaSyllable{u"魂", u"た", u'!'}));
    EXPECT_EQ(parseSyllable(u"＃｜う"), (FuriganaSyllable{u"#", u"う", 0}));
    EXPECT_EQ(parseSyllable(u"国｜＜ご"), (FuriganaSyllable{u"国", u"ご", u'<'}));
    // No |: no reading, a ！ in the base is the text's.
    EXPECT_EQ(parseSyllable(u"好き！"), (FuriganaSyllable{u"好き！", u"", 0}));
    EXPECT_EQ(parseSyllable(u"#"), (FuriganaSyllable{u"#", u"", 0}));
    for (const auto &text : {u"中|ちゅ", u"#|う", u"魂|!た", u"夢"})
        EXPECT_EQ(syllableText(parseSyllable(text)), text);
}
