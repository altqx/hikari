#pragma once

// KA4 (#279): furigana reading suggestions. Qt-free. An analyser (MeCab over
// UniDic, backends/mecab_readings.h) splits a Line's text into words with
// their readings; each run of kanji in a word gets the part of the word's
// reading it is read as, its okurigana left out (抱きしめ だきしめ: 抱 だ).
// Songs use poetic readings (永遠 とわ), so every suggestion stays editable,
// and a reading the user typed is never replaced by a suggestion. The output
// is the stock karaoke furigana syntax (Aegisub's furigana karaoke): 漢|かん,
// # for a further highlight of the same base and ! after the | to keep a
// reading from centring with the one before. Texts are UTF-16.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// One word of an analysed text: its characters as written and its reading
// in kana (katakana or hiragana), empty when the dictionary has none.
struct ReadingWord {
    std::u16string surface;
    std::u16string reading;
};
// The words of a text without override blocks or line breaks, in order.
using ReadingAnalyzer = std::function<std::vector<ReadingWord>(std::u16string_view)>;

// Kanji: the CJK ideographs (extensions and compatibility ones too) and the
// characters written among them, 々 〆 〇 ヵ ヶ (一ヶ月 いっかげつ).
bool isKanji(char32_t c);

// The hiragana of a katakana text (ヽ ヾ too); anything else as it is.
std::u16string hiragana(std::u16string_view text);

// A run of kanji in the text: where it is (UTF-16 units), its kanji, the
// suggested reading in hiragana (empty: the dictionary has none) and the
// reading the user typed (empty: no furigana for it).
struct FuriganaRun {
    std::size_t begin = 0, end = 0;
    std::u16string base;
    std::u16string suggested;
    std::optional<std::u16string> typed;
    // The typed reading when there is one, else the suggestion.
    const std::u16string &reading() const { return typed ? *typed : suggested; }
    bool operator==(const FuriganaRun &) const = default;
};

// A word's reading split over its kanji runs, in order: each run's kana
// between the word's other characters, which the reading must spell (a
// katakana one by its hiragana), the earliest split that fits. None when
// the reading does not spell them.
std::optional<std::vector<std::u16string>> splitReading(std::u16string_view surface, std::u16string_view reading);

// The kanji runs of `text` with their suggested readings. Override blocks
// are left out and \N \n \h end a piece; the analyser reads each piece. A
// word whose reading does not split gets one run from its first kanji to
// its last, read as the reading less the kana before and after it.
std::vector<FuriganaRun> suggestReadings(std::u16string_view text, const ReadingAnalyzer &analyze);

// `fresh` with the readings typed in `previous`: each fresh run takes the
// typed reading of the next previous run with the same kanji, in order, so
// text edited around a run keeps its reading and a removed run's goes.
std::vector<FuriganaRun> keepTypedReadings(std::vector<FuriganaRun> fresh, const std::vector<FuriganaRun> &previous);

// The morae of a kana text: a small kana (ゃ ゅ ょ, ぁ to ぉ, ゎ) sings with
// the one before; っ, ん and ー are morae of their own.
std::vector<std::u16string> morae(std::u16string_view kana);

// One karaoke syllable of a furigana Line: its base ("#": a further
// highlight of the base before), its reading and the sequence break after
// the | (! or <, 0 for none): the reading does not centre with the previous
// syllable's.
struct FuriganaSyllable {
    std::u16string base;
    std::u16string reading;
    char16_t sequenceBreak = 0;
    bool operator==(const FuriganaSyllable &) const = default;
};

// A run sung over one syllable per reading piece: the first syllable has
// the base and the first piece, each further one # and its piece. The
// break goes on the first: ! keeps a word's reading off the word before.
std::vector<FuriganaSyllable> runSyllables(std::u16string_view base, const std::vector<std::u16string> &pieces,
                                           char16_t sequenceBreak = 0);

// A syllable's text: base|reading (base|!reading with a break), the base
// alone when it has no reading.
std::u16string syllableText(const FuriganaSyllable &syllable);

// A syllable's text read back, the full-width forms (｜ ＃ ！ ＜) as the
// half-width ones. Text without a | has no reading.
FuriganaSyllable parseSyllable(std::u16string_view text);

} // namespace hikari::application
