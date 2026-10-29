#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hikari::core {

inline constexpr std::size_t kListedSyllables = 20;

struct KaraokeProblem {
    enum class Kind {
        Unclosed,          // a "{" with a karaoke tag and no "}", or a tag with no number
        NotForAudioEditor, // no tag the audio editor reads: none whose number starts at once ("\k50")
        EditorOnly,        // the audio editor reads other syllables than play ("\kfo50", "\keep", "\k(50)")
        TotalNotDuration,  // the syllables add up to more or less than the Line lasts
        ZeroLength,        // a syllable of no length
        OutsideLine,       // a syllable that starts before the Line, ends after it, or runs backward
    };
    LineId line;
    std::size_t row = 0;   // the Line's place among all Lines, from 0
    Kind kind = Kind::Unclosed;
    std::int64_t totalMs = 0;    // TotalNotDuration: the syllables' sum, in milliseconds
    std::int64_t durationMs = 0; // TotalNotDuration: the Line's end - start
    std::vector<std::size_t> syllables; // ZeroLength, OutsideLine: which ones, from 1, the first kListedSyllables
    std::size_t syllableCount = 0;      // ZeroLength, OutsideLine: how many in all
    bool operator==(const KaraokeProblem &) const = default;
};

inline constexpr std::size_t kListedProblems = 1000;

struct KaraokeCheck {
    std::vector<KaraokeProblem> problems; // the first `limit`
    std::size_t unlisted = 0;             // how many more there are
};

// In Line order, then Kind order; the first `limit` are listed, the rest counted. A translated Line is
// checked on its translation; template, code, mixin and generated "fx" Lines are skipped.
KaraokeCheck checkKaraoke(const Document &document, std::size_t limit = kListedProblems);

} // namespace hikari::core
