#pragma once

// KA7: karaoke checks over a Document's Lines. A karaoke Line is one with a
// \k, \K, \kf or \ko tag in an override block (or an unclosed block holding
// one); template, code and mixin Lines (their Effect's first word) are
// skipped. The tags are read as the renderers read them: each starts a
// syllable that runs until the next tag, its value in centiseconds, and an
// unclosed "{" makes the rest of the text literal. A value may be signed,
// have a fraction and sit in parentheses ("\k(50)", "\k+12.5"), as the tag
// parser reads it (lua_helper's karaoke tags); the fraction is dropped, as
// the karaoke the application plays drops it. The audio editor reads only
// a number that follows the tag at once ("\k50"), so a Line with none of
// those is flagged for it. A translated Line is checked on its
// translation, the text karaoke plays.

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hikari::core {

// A problem lists at most this many syllables; a Line of millions of bad
// ones still makes one short row.
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

// A check lists at most this many problems; a Document of millions of bad
// Lines still makes a short list, and says how many more it has.
inline constexpr std::size_t kListedProblems = 1000;

struct KaraokeCheck {
    std::vector<KaraokeProblem> problems; // the first `limit`
    std::size_t unlisted = 0;             // how many more there are
};

// The problems of every karaoke Line, in Line order, and for each Line in
// the order of Kind; the first `limit` are listed, the rest counted.
KaraokeCheck checkKaraoke(const Document &document, std::size_t limit = kListedProblems);

} // namespace hikari::core
