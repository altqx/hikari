#pragma once

#include "hikari/core/document.h"
#include "hikari/core/lyrics_import.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    std::size_t budget = 0; // bytes the Lines may take
    std::size_t spent = 0;
};

inline std::size_t lyricsBudget(std::size_t fileSize)
{
    return kMaxLyricsGrowth * fileSize + (std::size_t(1) << 20);
}

// Each Line costs about 1.5 KB however short, so a file of tiny lines would outgrow the byte budget.
inline constexpr std::size_t kMaxLyricsLines = 10'000;

// Call before making the Line. False when the budget or the Line cap is exhausted.
inline bool spend(LyricSheet &sheet, std::size_t textBytes, std::size_t actorBytes, std::size_t words)
{
    // MUST KILL: 16 and 64 are guessed byte costs of a \k tag and a Dialogue's fields
    sheet.spent += textBytes + actorBytes + 16 * words + 64;
    return sheet.spent <= sheet.budget && sheet.lines.size() < kMaxLyricsLines;
}

// 100 hours in ms, so time arithmetic cannot overflow.
inline constexpr std::int64_t kLatestLyricsTime = 360'000'000;

// Saturates at 10^12, so clock parts times 60, 60 and 1000 still fit an int64.
inline std::int64_t clampedNumber(std::u8string_view digits)
{
    std::int64_t n = 0;
    for (char8_t c : digits)
        n = std::min<std::int64_t>(n * 10 + (c - u8'0'), 1'000'000'000'000);
    return n;
}

// The line's end and the next line's start; LF, CRLF or a lone CR (XML, old Mac LRC files) ends it.
inline std::pair<std::size_t, std::size_t> lineAt(std::u8string_view text, std::size_t from)
{
    const std::size_t end = std::min(text.find_first_of(u8"\r\n", from), text.size());
    const bool crlf = end + 1 < text.size() && text[end] == u8'\r' && text[end + 1] == u8'\n';
    return {end, end + (crlf ? 2 : 1)};
}

// For ASS: braces become U+FF5B / U+FF5D, and a word joiner (U+2060) follows every backslash,
// including a final one, so \N, \n and \h stay literal.
std::u8string literalText(std::u8string_view text);

// One \k syllable per word, in centiseconds; a word lasts until it ends or the next begins, and
// empty syllables fill pauses within the Line's times. Extends `line.end` to the last word's end.
std::u8string karaokeText(LyricLine &line);

// New-Document Script Info and Default Style, then one Dialogue per Line.
LoadResult lyricsDocument(LyricSheet sheet);

}
