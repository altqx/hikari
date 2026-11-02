#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace hikari::core {

enum class ExportFormat { Srt, WebVtt, Sbv, PlainText, MicroDvd, Mpl2, TMPlayer };

struct ExportOptions {
    // Only these Lines; every Line when empty.
    std::optional<std::set<LineId>> lines;
    // Overlapping Lines become one cue per stretch of time (SRT, SBV, MicroDVD, MPL2; TMPlayer always).
    bool mergeOverlaps = false;
    bool srtFontColour = false;
    // {\anN} at the start of SRT cues that are not bottom centre.
    bool srtAlignmentTags = true;
    bool vttPositions = true;
    // A STYLE block from the Styles, with colour classes for colour tags.
    bool vttStyles = true;
    bool vttKaraoke = false;
    bool textTimes = false;
    bool textActors = false;
    double fps = 24000.0 / 1001.0;
};

// A bit per kind of ASS feature a Line lost or had approximated.
enum class ExportLoss : std::uint32_t {
    Drawing = 1u << 0,
    Clip = 1u << 1,
    Transform = 1u << 2,
    Movement = 1u << 3,   // \move: VTT keeps the start position
    Fade = 1u << 4,
    Rotation = 1u << 5,   // \fr \frx \fry \frz \fax \fay \org
    Font = 1u << 6,       // \fn \fs \fscx \fscy \fsp
    Outline = 1u << 7,
    Colour = 1u << 8,
    Formatting = 1u << 9, // \i \b \u \s
    Position = 1u << 10,  // \pos \an \a, or a Style that is not bottom centre
    Karaoke = 1u << 11,
    Effect = 1u << 12,    // Banner, Scroll up, Scroll down
    Merged = 1u << 13,    // shown in a cue with an overlapping Line
};

struct ExportLineNote {
    LineId line;
    std::uint32_t losses = 0;
    bool has(ExportLoss loss) const { return (losses & static_cast<std::uint32_t>(loss)) != 0; }
    bool operator==(const ExportLineNote &) const = default;
};

struct ExportResult {
    std::vector<std::byte> bytes;
    // In document order; Lines that lost nothing are not listed.
    std::vector<ExportLineNote> notes;
    int linesWritten = 0;
    int commentsSkipped = 0;
    int emptySkipped = 0; // nothing visible left, or no duration
};

// Never changes the Document. UTF-8 without a byte-order mark.
ExportResult exportDocument(const Document &document, ExportFormat format, const ExportOptions &options);

}
