#pragma once

#include "hikari/core/document.h"
#include "hikari/core/style.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::core {

struct ConversionOptions {
    double fps = 23.976;           // MicroDVD frames
    double videoFps = 23.976;      // new MicroDVD end times use this rate
    std::u8string prefix;          // tags inserted at each Line's start
    bool newEndTimes = false;
    int timePerCharacter = 110;    // ms
    std::u8string resolutionWidth = u8"1280", resolutionHeight = u8"720";
    StyleValues style;             // its name goes on every Line
};

struct ConversionReport {
    int commentsRemoved = 0;
    int duplicatesRemoved = 0;    // equal start, end and text after sorting
    int emptyRemoved = 0;
    int drawingsCleared = 0;
    int textChanged = 0;
    int fieldsDropped = 0;        // layer, style, actor, margins or effect
    int markersDropped = 0;       // bookmarks, hidden Lines and groups
    int timesChanged = 0;         // rounding, frames or new end times
    bool reordered = false;
    bool headerDropped = false;   // Script Info and Styles
    bool operator==(const ConversionReport &) const = default;
};

struct ConversionResult {
    Document document;
    ConversionReport report;
};

// Locale collation for the sort's text tiebreak (<0, 0, >0); bytes when empty.
using ConversionCollate = std::function<int(std::u8string_view, std::u8string_view)>;

// Lines that survive keep their LineIds. Nullopt for a target that is the
// source's own format, plain text, or an fps below 1.
std::optional<ConversionResult> convertDocument(const Document &source, SubtitleFormat target,
                                                const ConversionOptions &options, const ConversionCollate &collate = {});

} // namespace hikari::core
