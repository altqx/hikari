#pragma once

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

// `length` in UTF-16 units of the text; `romaji` lower case.
struct KanaReading {
    std::size_t length = 0;
    std::u16string romaji;
};

// Longest kana first. A small tsu reads as the next kana's consonant (any at
// the end), the long vowel mark as any vowel. Empty when `text` does not start
// with kana.
std::vector<KanaReading> kanaReadings(std::u16string_view text);

// Ignores case, spaces, apostrophes and vowel accents (ō as o). Nullopt when
// part of it is no romaji.
std::optional<std::u16string> romajiToHiragana(std::u16string_view romaji);

}
