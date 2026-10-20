#pragma once

// Internal to the lyrics importers (lyrics_import.cpp, lyrics_ttml.cpp): the
// timed lyrics both formats read, and the ASS Document made from them.

#include "hikari/core/document.h"
#include "hikari/core/lyrics_import.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hikari::core::detail {

struct LyricWord {
    std::int64_t begin = 0; // milliseconds
    std::int64_t end = 0;
    std::u8string text;     // with the space that follows the word, if any
};

struct LyricLine {
    std::int64_t start = 0; // milliseconds
    std::int64_t end = 0;
    std::vector<LyricWord> words; // empty: a plain Line of `text`
    std::u8string text;
    std::u8string actor;
};

struct LyricSheet {
    std::optional<std::u8string> title;
    std::vector<LyricLine> lines;
};

// The latest time either format may give (100 hours), so no arithmetic on
// a time can overflow.
inline constexpr std::int64_t kLatestLyricsTime = 360'000'000;

// Lyrics text as ASS shows it literally: "{" and "}" would open and close
// override blocks, so they become "\uFF5B" and "\uFF5D", and a word joiner
// (U+2060) after a backslash keeps \N, \n and \h from being a line break or a
// hard space. A backslash at the end gets one too, since a syllable's
// override block or other text may follow it.
std::u8string literalText(std::u8string_view text);

// A Line's text with one \k syllable per word, in centiseconds. Each word
// lasts until it ends or the next word begins, whichever is first; an empty
// syllable fills each pause, including one before the first word and one
// after the last within the Line's times. `line.end` is moved out to the
// last word's end when a word ends after it.
std::u8string karaokeText(LyricLine &line);

// The ASS Document of a sheet: the new-Document Script Info and Default
// Style (as DocumentFiles::createNew, legacy LoadDefault), then one Dialogue
// per Line in order.
LoadResult lyricsDocument(LyricSheet sheet);

} // namespace hikari::core::detail
