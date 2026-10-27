#pragma once

// KA3 (#278): Japanese kana and the romaji they read as, for the Kanji
// timer's suggestions and the reading it shows beside a group; KA4 (#279,
// furigana reading suggestions) reads them too. Qt-free. Hepburn with the
// Kunrei spellings (si, ti, tu, hu, zi), the particles' (ha as wa, he as e,
// wo as o) and the loanword syllables lyrics write (fa, ti, di, she, je...).
// Katakana read as their hiragana. Texts are UTF-16.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Hiragana and katakana (the small ones, the iteration marks and the long
// vowel mark too), not the half-width forms.
bool isKana(char32_t c);

// A katakana's hiragana (ヴ ゔ, ヵ ゕ); any other character as it is.
char16_t toHiragana(char16_t c);

// One reading of the kana a text starts with: how many UTF-16 units of the
// text it takes and the lower-case romaji.
struct KanaReading {
    std::size_t length = 0;
    std::u16string romaji;
};

// Every reading of the kana `text` starts with, the longest kana first
// (きゃ kya, sya before き ki, si). A small tsu reads as the consonant the next
// kana's reading starts with (っか: k, as in kka; t before ch), or any
// consonant at the end; the long vowel mark as any vowel. None for a text
// that does not start with kana.
std::vector<KanaReading> kanaReadings(std::u16string_view text);

// The hiragana a romaji text reads as: spaces and apostrophes left out,
// lower case, a macron or circumflex vowel read as itself (ō as o), a
// doubled consonant as っ, n before a consonant or at the end as ん. None
// when part of it is no romaji.
std::optional<std::u16string> romajiToHiragana(std::u16string_view romaji);

} // namespace hikari::application
