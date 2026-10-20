#pragma once

// KA7: karaoke checks over a Document's Lines. A karaoke Line is one with a
// \k, \K, \kf or \ko tag in an override block (or an unclosed block holding
// one); template, code and mixin Lines (their Effect's first word) are
// skipped. The tags are read as the renderers read them: each starts a
// syllable that runs until the next tag, its value in centiseconds, and an
// unclosed "{" makes the rest of the text literal. A value may be signed,
// have a fraction and sit in parentheses ("\k(50)", "\k+12.5"), as the tag
// parser reads it (lua_helper's karaoke tags); the fraction is dropped, as
// the karaoke the application plays drops it. A translated Line is
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
        ZeroLength,       // a syllable of no length
        OutsideLine,      // a syllable that starts before the Line, ends after it, or runs backward
    };
    LineId line;
    std::size_t row = 0;   // the Line's place among all Lines, from 0
    Kind kind = Kind::Unclosed;
    std::int64_t totalMs = 0;    // TotalNotDuration: the syllables' sum, in milliseconds
    std::int64_t durationMs = 0; // TotalNotDuration: the Line's end - start
    std::vector<std::size_t> syllables; // ZeroLength, OutsideLine: which ones, from 1
    bool operator==(const KaraokeProblem &) const = default;
};

// Every problem of every karaoke Line, in Line order, and for each Line in
// the order of Kind.
std::vector<KaraokeProblem> checkKaraoke(const Document &document);

} // namespace hikari::core
