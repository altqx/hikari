#pragma once

#include "hikari/core/document.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

// "ass" 0, "ssa" 1, anything else (subrip, srt, text) 2.
enum class MatroskaCodec { Ass = 0, Ssa = 1, Text = 2 };
MatroskaCodec matroskaCodec(std::u8string_view codecName);

namespace legacy {
// Empty when the bytes are not UTF-8.
std::u8string utf8OrEmpty(std::string_view bytes);
} // namespace legacy

// ASS/SSA: Matroska's ReadOrder and Layer fields taken off the block, "Dialogue: <layer>,<start>,<end>"
// put before the rest, times + 5 ms truncated to centiseconds. Text: "<start> --> <end>\r\n<text>".
// `line` is the packet's bytes, not decoded.
std::u8string matroskaLine(std::int64_t start, std::int64_t duration, std::string_view line, MatroskaCodec codec);

// ASS/SSA: the codec-private data's Script Info (until the first Style; ';', '[' and "Format:"
// lines skipped, a repeated key keeps its place), Styles and Comments, then every packet's line,
// plus "YCbCr Matrix: TV.601" for ASS when it is missing or None. The format is the first
// non-plain-text Line's, else ASS. `codecPrivate` is bytes, not decoded.
Document matroskaDocument(std::string_view codecPrivate, const std::vector<std::u8string> &lines, MatroskaCodec codec);

} // namespace hikari::core
