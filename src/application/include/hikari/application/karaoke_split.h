#pragma once

// KA1 (#276): Karaoke's language-aware Auto split. Qt-free: the syllables
// of a Line without \k tags, by script and language (kana and romaji by
// mora, English by hyphenation, kanji and Hangul one character each), and
// the share of time each one takes (its visible characters). Positions are
// UTF-16 units of the Line's text, as audio_karaoke's.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hikari::application {

struct KaraokeCharClass; // audio_karaoke.h

// Liang's hyphenation over a libhyphen pattern file (hyph_en_US.dic: its
// charset line and the patterns; a break leaves two letters at least on
// either side, whatever the file's LEFTHYPHENMIN and RIGHTHYPHENMIN).
class Hyphenation {
public:
    static Hyphenation fromPatterns(std::string_view utf8);
    bool empty() const { return m_patterns.empty(); }
    // Where the lower-case `word` may break: the positions (1 to its length
    // - 1) of the characters a new syllable starts with.
    std::vector<std::size_t> breaks(std::u16string_view word) const;

private:
    std::unordered_map<std::u16string, std::vector<std::uint8_t>> m_patterns;
    std::size_t m_longest = 0;
};

// How Auto split treats Japanese: AUDIO_MERGE_EVERY_N_WITH_SYLLABLE's
// syllabic n (kan|ji, not ka|n|ji) and a double consonant on its own
// (so|ma|t|ta, not so|mat|ta); English goes by `hyphenation` (one syllable
// a word without it). A Line is English when its Latin words that are no
// romaji outweigh those that are (a word weighing its letters past the
// second); in a romaji Line a word that is no romaji is English.
struct KaraokeSplitRules {
    bool everyN = false;
    bool doubleConsonantApart = false;
    const Hyphenation *hyphenation = nullptr;
};

// Where the syllables of `text` (a Line without \k tags) start: 0 first,
// then each later start in increasing order. Override blocks and an
// unclosed "{" to the end are not text; \N \n \h are spaces. Spaces and
// punctuation end the syllable before them, but for punctuation after the
// last space, which opens the next word.
std::vector<std::size_t> karaokeAutoSplit(std::u16string_view text, const KaraokeSplitRules &rules,
                                          const KaraokeCharClass &classes);

// The characters a syllable's time is shared by: graphemes outside override
// blocks that are neither space (\N \n \h too) nor punctuation.
int karaokeWeight(std::u16string_view text, const KaraokeCharClass &classes);

// Whether the UTF-16 unit at `at` continues the grapheme before it: an
// extended grapheme cluster (UAX #29, core::graphemeEnd) such as a
// surrogate pair, a letter and its marks (spacing ones too, as Devanagari's
// vowel signs), a Hangul syllable in jamo, an emoji sequence or a flag.
bool karaokeContinuesGrapheme(std::u16string_view text, std::size_t at);

} // namespace hikari::application
