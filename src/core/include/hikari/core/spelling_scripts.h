#pragma once

#include <cstdint>
#include <string_view>

namespace hikari::core::spelling {

enum class Script : std::uint8_t {
    Latin,
    Cyrillic,
    Greek,
    Armenian,
    Georgian,
    Hebrew,
    Arabic,
    Devanagari,
    Bengali,
    Gurmukhi,
    Gujarati,
    Oriya,
    Tamil,
    Telugu,
    Kannada,
    Malayalam,
    Sinhala,
    Thai,
    Lao,
    Tibetan,
    Myanmar,
    Khmer,
    Ethiopic,
    Hangul,
    Kana,
    Han,
};

class Scripts {
public:
    constexpr Scripts() = default;
    constexpr Scripts(Script s) : m_bits(bit(s)) {}
    constexpr Scripts operator|(Scripts o) const { return Scripts(m_bits | o.m_bits); }
    constexpr Scripts &operator|=(Scripts o) { return m_bits |= o.m_bits, *this; }
    constexpr bool has(Script s) const { return (m_bits & bit(s)) != 0; }
    constexpr bool intersects(Scripts o) const { return (m_bits & o.m_bits) != 0; }
    constexpr bool empty() const { return m_bits == 0; }
    constexpr bool operator==(const Scripts &) const = default;

private:
    constexpr explicit Scripts(std::uint32_t bits) : m_bits(bits) {}
    static constexpr std::uint32_t bit(Script s) { return std::uint32_t{1} << static_cast<unsigned>(s); }
    std::uint32_t m_bits = 0;
};

// The script of a code point's letter; nothing for digits (any script's),
// punctuation, combining marks and scripts outside the table.
bool scriptOf(char32_t c, Script &out);
// Every script among a word's letters (UTF-16, surrogate pairs decoded).
Scripts wordScripts(std::u16string_view word);
// The script most of a text's letters are in (a dictionary's TRY letters);
// false when none is in the table.
bool dominantScript(std::u16string_view text, Script &out);
// The scripts a dictionary checks, from its symbol: a script subtag (Latn,
// Cyrl, ...) first, else the language subtag's table row. Empty when the
// language is not in the table.
Scripts dictionaryScripts(std::u16string_view symbol);

} // namespace hikari::core::spelling
