#pragma once

// KA7: karaoke checks over a Document's Lines. A karaoke Line is one with a
// \k, \K, \kf or \ko tag in an override block (or an unclosed block holding
// one); template, code and mixin Lines (their Effect's first word) are
// skipped. The tags are read as the renderers read them: each starts a
// syllable that runs until the next tag, its value in centiseconds, and an
// unclosed "{" makes the rest of the text literal. A translated Line is
// checked on its translation, the text karaoke plays.

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hikari::core {

struct KaraokeProblem {
    enum class Kind {
        Unclosed,         // a "{" with a karaoke tag and no "}", or a tag with no number
        TotalNotDuration, // the syllables add up to more or less than the Line lasts
        ZeroLength,       // a syllable of 0 cs
        OutsideLine,      // a syllable that starts before the Line or after it ends
    };
    LineId line;
    std::size_t row = 0;   // the Line's place among all Lines, from 0
    Kind kind = Kind::Unclosed;
    std::int64_t totalCs = 0;    // TotalNotDuration: the syllables' sum
    std::int64_t durationCs = 0; // TotalNotDuration: the Line's end - start
    std::vector<std::size_t> syllables; // ZeroLength, OutsideLine: which ones, from 1
    bool operator==(const KaraokeProblem &) const = default;
};

// Every problem of every karaoke Line, in Line order, and for each Line in
// the order of Kind.
std::vector<KaraokeProblem> checkKaraoke(const Document &document);

} // namespace hikari::core
