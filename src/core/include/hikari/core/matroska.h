#pragma once

// Y9: subtitles read from a Matroska file (legacy Demux::GetSubtitles and its
// packet callback, Demux.cpp at 20d647c4). The media helper hands over the
// track's codec-private data and its packets as FFMS2's extension gives
// them; this turns them into the Document legacy built in the grid.

#include "hikari/core/document.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

// Demux's codecType (Demux.cpp:111-117): "ass" 0, "ssa" 1, anything else
// (subrip, srt, text) 2.
enum class MatroskaCodec { Ass = 0, Ssa = 1, Text = 2 };
MatroskaCodec matroskaCodec(std::u8string_view codecName);

namespace legacy {
// wxString(bytes, wxConvUTF8): the text, or empty when the bytes are not
// UTF-8 (legacy read every FFMS2 string this way).
std::u8string utf8OrEmpty(std::string_view bytes);
} // namespace legacy

// One packet as Demux::GetSubtitles' callback writes it (Demux.cpp:262-306):
// start and end in legacy's int milliseconds (start + 5 then truncated to
// centiseconds for ASS/SSA), the ReadOrder and Layer fields taken off an
// ASS/SSA block and "Dialogue: <layer>,<start>,<end>" put before the rest; a
// text packet becomes "<start> --> <end>\r\n<text>". `line` is the packet's
// bytes (not decoded).
std::u8string matroskaLine(std::int64_t start, std::int64_t duration, std::string_view line, MatroskaCodec codec);

// The Document Demux::GetSubtitles builds (Demux.cpp:128-164) from the
// codec-private data (bytes, not decoded) and the callback's lines in order:
// for ASS/SSA the private data's Script Info lines (until the first Style;
// ';', '[' and "Format:" lines skipped, a key seen again keeps its place
// and takes the new value), its Styles (SSA ones read with the SSA layout)
// and its Comment lines, then every packet's line as Dialogue(raw) reads
// it, and "YCbCr Matrix: TV.601" for ASS when the matrix is missing or None.
// Its format is SubsGrid::SetSubsFormat's: the first Line's that is not
// plain text, else ASS. The Document is what legacy held in memory; its
// source is the file legacy's SaveFile writes for it.
Document matroskaDocument(std::string_view codecPrivate, const std::vector<std::u8string> &lines, MatroskaCodec codec);

} // namespace hikari::core
