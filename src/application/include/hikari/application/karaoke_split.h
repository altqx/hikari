#pragma once

// Positions are UTF-16 units of the Line's text.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hikari::application {

struct KaraokeCharClass;

// Liang's hyphenation over a libhyphen .dic; breaks leave two letters on
// either side whatever its LEFTHYPHENMIN and RIGHTHYPHENMIN say.
class Hyphenation {
public:
    static Hyphenation fromPatterns(std::string_view utf8);
    bool empty() const { return m_patterns.empty(); }
    // Syllable starts in the lower-case `word`, 1 to its length - 1.
    std::vector<std::size_t> breaks(std::u16string_view word) const;

private:
    std::unordered_map<std::u16string, std::vector<std::uint8_t>> m_patterns;
    std::size_t m_longest = 0;
};

// everyN: kan|ji, not ka|n|ji; doubleConsonantApart: so|ma|t|ta. Without
// `hyphenation` an English word is one syllable.
struct KaraokeSplitRules {
    bool everyN = false;
    bool doubleConsonantApart = false;
    const Hyphenation *hyphenation = nullptr;
};

// Syllable starts of a Line without \k tags, 0 first, ascending; empty past
// kKaraokeAutoSplitMost visible characters. An unclosed "{" runs to the end.
inline constexpr std::size_t kKaraokeAutoSplitMost = 4096;
std::vector<std::size_t> karaokeAutoSplit(std::u16string_view text, const KaraokeSplitRules &rules,
                                          const KaraokeCharClass &classes);

// Graphemes outside override blocks that are neither space (\N \n \h too)
// nor punctuation.
int karaokeWeight(std::u16string_view text, const KaraokeCharClass &classes);

// By extended grapheme clusters (UAX #29).
bool karaokeContinuesGrapheme(std::u16string_view text, std::size_t at);

} // namespace hikari::application
