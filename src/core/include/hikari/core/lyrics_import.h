#pragma once

// Word-timed lines get one \k syllable per word plus an empty one per pause,
// adding up to the Line's duration. Lyrics text stays literal: braces become
// fullwidth and a backslash never starts an ASS escape.

#include "hikari/core/document.h"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace hikari::core {

struct LyricsProblem {
    enum class Kind {
        NotUtf8,
        UnreadableTime,
        TimeGoesBack,
        EndBeforeBegin,
        MissingTime,      // lyrics text with no time to place it
        RepeatedWordLine, // LRC: a word-timed line with several line times
        NotWellFormed,
        NotTtml,
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

inline constexpr std::size_t kMaxTtmlDepth = 64;
// Past these limits an import is TooLarge: TTML nodes, one per
// kTtmlBytesPerNode bytes plus 4096; the Lines, kMaxLyricsGrowth times the
// file's size plus 1 MiB.
inline constexpr std::size_t kMaxLyricsFileSize = std::size_t(1) << 20;
inline constexpr std::size_t kTtmlBytesPerNode = 8;
inline constexpr std::size_t kMaxLyricsGrowth = 16;

// "lrc" or "ttml", lower-case, without the dot.
bool isLyricsExtension(std::u8string_view extension);

std::expected<LoadResult, LyricsProblem> importLrc(std::span<const std::byte> bytes);
std::expected<LoadResult, LyricsProblem> importTtml(std::span<const std::byte> bytes);
// By extension; a non-lyrics extension gives NoLyrics.
std::expected<LoadResult, LyricsProblem> importLyrics(std::span<const std::byte> bytes, std::u8string_view extension);

}
