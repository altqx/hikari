#pragma once

// KA6: word-timed lyrics imported as karaoke Lines (KA6-lyrics-import,
// docs/compatibility-decisions.md). An enhanced LRC file ("<mm:ss.xx>"
// word stamps) or a TTML file with word-level <span begin end> becomes an
// ASS Document whose Lines carry one \k syllable per word, with an empty
// syllable for each pause, so the syllables always add up to the Line's
// duration. Lines without word times import as plain Lines. A malformed file
// imports nothing; the problem names the line at fault. Lyrics text stays
// literal: braces become their fullwidth forms, and a backslash is kept
// from starting an ASS escape.

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
        TooDeep,          // TTML: elements nested past kMaxTtmlDepth
        TooLarge,         // larger than a lyrics file: the file, its XML or its Lines
        NoLyrics,         // nothing timed in the whole file
        OutsideParent,    // TTML: a begin after the end an element inherits, or an end before its begin
    };
    Kind kind = Kind::NoLyrics;
    std::size_t line = 0;  // 1-based line of the file; 0 when no line is at fault
    std::u8string excerpt; // the text at fault, as written
    bool operator==(const LyricsProblem &) const = default;
};

// TTML lyrics nest a handful of elements; deeper files are refused, so no
// walk over the tree can run out of stack.
inline constexpr std::size_t kMaxTtmlDepth = 64;
// What an import may take, so a file cannot make a huge Document or exhaust
// memory while it is read (each refused as TooLarge):
// - the file: lyrics files are tens of kilobytes, so 1 MiB is plenty;
// - TTML's element tree: one node (element or text) per kTtmlBytesPerNode
//   bytes of the file, plus 4096 (real files have one per 25 bytes or so);
// - the Lines: text a file repeats (a lyric under many line times, an agent
//   name on every Line) is copied into each Line, so the Lines may take
//   kMaxLyricsGrowth times the file's size, plus 1 MiB.
inline constexpr std::size_t kMaxLyricsFileSize = std::size_t(1) << 20;
inline constexpr std::size_t kTtmlBytesPerNode = 8;
inline constexpr std::size_t kMaxLyricsGrowth = 16;

// "lrc" or "ttml" (lower-case, without the dot).
bool isLyricsExtension(std::u8string_view extension);

std::expected<LoadResult, LyricsProblem> importLrc(std::span<const std::byte> bytes);
std::expected<LoadResult, LyricsProblem> importTtml(std::span<const std::byte> bytes);
// By extension; a non-lyrics extension gives NoLyrics.
std::expected<LoadResult, LyricsProblem> importLyrics(std::span<const std::byte> bytes, std::u8string_view extension);

} // namespace hikari::core
