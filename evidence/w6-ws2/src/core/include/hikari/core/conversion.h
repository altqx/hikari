#pragma once

// Y5: converting a Document to another subtitle format (legacy
// GLOBAL_CONVERT_TO_* and SubsGrid::Convert with Dialogue::Convert at
// 20d647c4). The result is a new Document in the target format, as legacy
// would save it; the source is untouched (C02-loss-preview: what the
// conversion removes or synthesizes is reported for review before it is
// accepted).

#include "hikari/core/document.h"
#include "hikari/core/style.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::core {

// The legacy CONVERT_* options.
struct ConversionOptions {
    double fps = 23.976;           // CONVERT_FPS (MicroDVD frames)
    double videoFps = 23.976;      // SubsTime::FPS: new MicroDVD end times use the video's rate
    std::u8string prefix;          // CONVERT_ASS_TAGS_TO_INSERT_IN_LINE
    bool newEndTimes = false;      // CONVERT_NEW_END_TIMES
    int timePerCharacter = 110;    // CONVERT_TIME_PER_CHARACTER (ms)
    std::u8string resolutionWidth = u8"1280", resolutionHeight = u8"720";
    StyleValues style;             // the conversion Style; its name goes on every Line
};

// What the conversion removes, rewrites or synthesizes.
struct ConversionReport {
    int commentsRemoved = 0;      // comment Lines (from ASS)
    int duplicatesRemoved = 0;    // equal start, end and text after sorting (from ASS)
    int emptyRemoved = 0;         // Lines left without text (from ASS)
    int drawingsCleared = 0;      // drawings have no counterpart (from ASS)
    int textChanged = 0;          // Lines whose markup was converted or removed
    int fieldsDropped = 0;        // layer, style, actor, margins or effect lost or replaced
    int markersDropped = 0;       // bookmarks, hidden Lines and groups (from ASS)
    int timesChanged = 0;         // start or end moved: rounding, frames or new end times
    bool reordered = false;       // sorted by time (from ASS)
    bool headerDropped = false;   // Script Info and Styles (from ASS)
    bool operator==(const ConversionReport &) const = default;
};

struct ConversionResult {
    Document document;
    ConversionReport report;
};

// Locale collation for the sort's text tiebreak (<0, 0, >0); bytes when empty.
using ConversionCollate = std::function<int(std::u8string_view, std::u8string_view)>;

// Lines that survive keep their LineIds. Nullopt for a target that is the
// source's own format, plain text, or an fps below 1 (legacy "Invalid FPS").
std::optional<ConversionResult> convertDocument(const Document &source, SubtitleFormat target,
                                                const ConversionOptions &options, const ConversionCollate &collate = {});

} // namespace hikari::core
