#pragma once

// KA6: word-timed lyrics imported as karaoke Lines (KA6-lyrics-import,
// docs/qt/compatibility-decisions.md). An enhanced LRC file ("<mm:ss.xx>"
// word stamps) or a TTML file with word-level <span begin end> becomes an
// ASS Document whose Lines carry one \k syllable per word, with an empty
// syllable for each pause, so the syllables always add up to the Line's
// duration. Lines without word times import as plain Lines. A malformed file
// imports nothing; the problem names the line at fault.

#include "hikari/core/document.h"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace hikari::core {

struct LyricsProblem {
    enum class Kind {
        NotUtf8,          // the bytes are not UTF-8 text
        UnreadableTime,   // a time stamp or begin/end value that is not a time
        TimeGoesBack,     // a word starts before the word before it
        EndBeforeBegin,   // TTML: an end earlier than its begin
        MissingTime,      // lyrics text with no time to place it
        RepeatedWordLine, // LRC: a word-timed line with several line times
        NotWellFormed,    // TTML: the XML is broken here
        NotTtml,          // TTML: the root element is not <tt>
        NoLyrics,         // nothing timed in the whole file
    };
    Kind kind = Kind::NoLyrics;
    std::size_t line = 0;  // 1-based line of the file; 0 when no line is at fault
    std::u8string excerpt; // the text at fault, as written
    bool operator==(const LyricsProblem &) const = default;
};

// "lrc" or "ttml" (lower-case, without the dot).
bool isLyricsExtension(std::u8string_view extension);

std::expected<LoadResult, LyricsProblem> importLrc(std::span<const std::byte> bytes);
std::expected<LoadResult, LyricsProblem> importTtml(std::span<const std::byte> bytes);
// By extension; a non-lyrics extension gives NoLyrics.
std::expected<LoadResult, LyricsProblem> importLyrics(std::span<const std::byte> bytes, std::u8string_view extension);

} // namespace hikari::core
