#pragma once

// Texts are UTF-16; syllables use Aegisub's furigana karaoke syntax (漢|かん).

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// `reading` is kana, empty when the dictionary has none.
struct ReadingWord {
    std::u16string surface;
    std::u16string reading;
};
// Takes a text without override blocks or line breaks.
using ReadingAnalyzer = std::function<std::vector<ReadingWord>(std::u16string_view)>;

// CJK ideographs plus 々 〆 〇 ヵ ヶ.
bool isKanji(char32_t c);

// Katakana (ヽ ヾ too) to hiragana; anything else unchanged.
std::u16string hiragana(std::u16string_view text);

// Empty `suggested`: the dictionary has none; empty `typed`: no furigana.
struct FuriganaRun {
    std::size_t begin = 0, end = 0;
    std::u16string base;
    std::u16string suggested;
    std::optional<std::u16string> typed;
    const std::u16string &reading() const { return typed ? *typed : suggested; }
    bool operator==(const FuriganaRun &) const = default;
};

// Each kanji run's share of `reading`, the earliest split that fits; nullopt
// when the reading does not spell the word's kana.
std::optional<std::vector<std::u16string>> splitReading(std::u16string_view surface, std::u16string_view reading);

// \N \n \h end a piece. A word whose reading does not split gets one run
// from its first kanji to its last.
std::vector<FuriganaRun> suggestReadings(std::u16string_view text, const ReadingAnalyzer &analyze);

// Each fresh run takes the typed reading of the next previous run with the
// same kanji.
std::vector<FuriganaRun> keepTypedReadings(std::vector<FuriganaRun> fresh, const std::vector<FuriganaRun> &previous);

// A small kana joins the one before; っ, ん and ー stand alone.
std::vector<std::u16string> morae(std::u16string_view kana);

// base "#": a further highlight of the base before; sequenceBreak: ! or <,
// 0 for none.
struct FuriganaSyllable {
    std::u16string base;
    std::u16string reading;
    char16_t sequenceBreak = 0;
    bool operator==(const FuriganaSyllable &) const = default;
};

// The first syllable has the base and the break, each further one #.
std::vector<FuriganaSyllable> runSyllables(std::u16string_view base, const std::vector<std::u16string> &pieces,
                                           char16_t sequenceBreak = 0);

// base|reading, base|!reading with a break, the base alone without a reading.
std::u16string syllableText(const FuriganaSyllable &syllable);

// Full-width ｜ ＃ ！ ＜ read as half-width; text without | has no reading.
FuriganaSyllable parseSyllable(std::u16string_view text);

} // namespace hikari::application
