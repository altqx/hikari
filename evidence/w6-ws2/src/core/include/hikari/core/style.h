#pragma once

// Style values as the legacy Styles::parseStyle reads them (C4-ssa). The
// Document keeps each Style line's exact bytes; this is the semantic view.
// SSA v4 lines are read with the SSA field layout and alignment numbering;
// converting a Document to ASS is a separate, explicit operation.

#include "hikari/core/document.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

struct Colour {
    std::int64_t r = 0, g = 0, b = 0, a = 0; // legacy AssColor longs
    bool operator==(const Colour &) const = default;
};

struct StyleValues {
    std::u8string name;
    std::u8string fontname, fontsize;
    Colour primary, secondary, outline, back;
    bool bold = false, italic = false, underline = false, strikeOut = false;
    std::u8string scaleX, scaleY, spacing, angle;
    bool borderStyle = false; // BorderStyle 3 (opaque box)
    std::u8string outlineWidth, shadow;
    std::u8string alignment;  // ASS numpad numbering; SSA values are renumbered
    std::u8string marginLeft, marginRight, marginVertical, encoding;
    // False when the line ran out of fields. Legacy keeps such a Style with
    // the remaining fields unset (its booleans are then indeterminate).
    bool complete = false;
    // SSA fields with no ASS counterpart, as authored. Converting to ASS
    // drops them, so a conversion must report them.
    std::optional<std::u8string> ssaTertiaryColour, ssaAlphaLevel;
};

// Every Style line in document order. As in the legacy loader, a "[V4"
// header that is not "[V4+" switches to the SSA layout for all later Styles.
std::vector<StyleValues> decodeStyles(const Document &document);

namespace legacy {
// Styles::parseStyle on one line from "Style:" on (left-trimmed).
StyleValues decodeStyle(std::u8string_view styleLine, bool ssa);
// AssColor::SetAss: decimal (SSA) or &HAABBGGRR& / #RRGGBB hex.
Colour colour(std::u8string_view text);
// TagFindReplace::TagValueFromStyle: the Style's value for an override tag
// name (fs, bord, shad, fsp, fscx, fscy, c/1c-4c as &HBBGGRR&, 1a-4a as hex,
// fn, b, i, u, s as 0/1, fr/frz), or nullopt for any other tag.
std::optional<std::u8string> styleTagValue(const StyleValues &style, std::u8string_view tag);
// Styles::GetRaw's fields, name first (colours as &HAABBGGRR, flags -1/0).
std::vector<std::u8string> styleRawFields(const StyleValues &style);
} // namespace legacy

} // namespace hikari::core
