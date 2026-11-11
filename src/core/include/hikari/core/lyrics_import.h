#pragma once

// Word-timed lines get one \k syllable per word plus an empty one per pause,
// adding up to the Line's duration. Lyrics text stays literal: braces become
// fullwidth and a backslash never starts an ASS escape.

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
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
        Encrypted,        // a Kugou or QQ Music file as the vendor ships it; HikariSub never decrypts these
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

enum class LyricsFormat { Lrc, Ttml, Krc, Qrc };

// End times the file leaves open: a plain LRC line's, or a word-timed line's last word's.
struct LyricsOptions {
    // nullopt: the Line ends when the next one starts. Otherwise it ends this many ms
    // earlier, unless that leaves it no time, when it still ends at the next start.
    std::optional<std::int64_t> gap;
    std::int64_t lastLine = 5000; // ms, for the last Line, which has no next one
    bool operator==(const LyricsOptions &) const = default;
};

struct LyricsFacts {
    LyricsFormat format = LyricsFormat::Lrc;
    bool wordTimed = false;
    std::size_t lines = 0;
    std::u8string title;
    std::u8string artist;
    std::int64_t offset = 0; // ms, as [offset:] gives it
};

struct ImportedLyrics {
    LoadResult load;
    LyricsFacts facts;
};

enum class LyricsEncoding { Utf8, Utf16Le, Utf16Be, Gb18030, Big5, ShiftJis, EucKr };

// "lrc", "ttml", "krc" or "qrc", lower-case, without the dot.
bool isLyricsExtension(std::u8string_view extension);
std::optional<LyricsFormat> lyricsFormatOf(std::u8string_view extension);

// The file as Kugou or QQ Music ship it, not decoded to text: KRC's "krc1" header,
// QRC's binary header, QQ Music's hex download, or bytes no text has.
bool lyricsEncrypted(std::span<const std::byte> bytes, LyricsFormat format);

// The encoding a byte order mark names, or UTF-8 for bytes that are valid UTF-8.
std::optional<LyricsEncoding> evidentLyricsEncoding(std::span<const std::byte> bytes);

// `bytes` is UTF-8 text; a leading BOM is skipped.
std::expected<ImportedLyrics, LyricsProblem> readLyrics(std::span<const std::byte> bytes, LyricsFormat format,
                                                        const LyricsOptions &options = {});

std::expected<LoadResult, LyricsProblem> importLrc(std::span<const std::byte> bytes);
std::expected<LoadResult, LyricsProblem> importTtml(std::span<const std::byte> bytes);
// By extension; a non-lyrics extension gives NoLyrics.
std::expected<LoadResult, LyricsProblem> importLyrics(std::span<const std::byte> bytes, std::u8string_view extension);

}
