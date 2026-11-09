#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// One QC note: a moment of the video, a section (a topic or an author) and its text, whose
// lines are joined by "\N".
struct QcNote {
    std::int64_t ms = 0;
    std::u8string section;
    bool done = false;
    std::u8string text;
    bool operator==(const QcNote &) const = default;
};

// The section of notes written before any [Section] header.
inline constexpr std::u8string_view kQcGeneralSection = u8"General";

// arch1t3cht's Note Browser format: a note starts with h:mm:ss or m:ss (a fraction is kept),
// "[Section]" lines group the notes after them, and following lines continue the note up to a
// blank line after an unindented line. A file with an mpvQC "generator" header is read as
// mpvQC, whose category becomes the section. In time order.
std::vector<QcNote> parseQcNoteText(std::u8string_view text);

// Note Browser's format, which it reads back: [Section] headers, "h:mm:ss.cc - text", and the
// text's further lines indented.
std::u8string formatQcNoteText(std::span<const QcNote> notes);

// The note as the file keeps it: the time truncated to centiseconds, the section without commas
// (General when empty), and the text on one line, with "\N" for its line breaks and no blanks
// around it.
QcNote normalisedQcNote(QcNote note);

// The "Hikari QC <n>" Script Info keys, in time order. Keys whose value isn't a note are left
// alone and not listed.
std::vector<QcNote> documentQcNotes(const core::Document &document);

// Writes `notes` as "Hikari QC <n>: h:mm:ss.cc,section,open|done,text" keys in time order at the
// end of Script Info, times truncated to centiseconds. Other properties and unchanged notes keep
// their bytes. False for formats without Script Info.
bool setDocumentQcNotes(core::Document &document, std::vector<QcNote> notes);

struct QcImport {
    std::vector<QcNote> notes;
    std::size_t added = 0;
    std::size_t skipped = 0; // already there: same time, section and text
};
// `incoming` added to `current` (or replacing it), in time order.
QcImport importQcNotes(std::vector<QcNote> current, std::span<const QcNote> incoming, bool replace);

// The Line a note at `ms` marks: the one on screen then, the latest start among several; with
// none on screen, the nearest start less than 15 s away (Note Browser's rule). Comment and
// unparsed Lines never match. Ties go to the earlier Line in `lines`.
std::optional<core::LineId> qcNoteLine(std::span<const core::LineRecord *const> lines, std::int64_t ms);

// "0:03:24" for whole seconds, "0:03:24.40" otherwise.
std::u8string qcNoteTimeText(std::int64_t ms);

}
