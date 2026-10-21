#pragma once

// VT2-arrow-nudge: a legacy probe's key press as the rewrite's. Legacy nudged
// with A/D/W/S (Move's end point with J/L/I/K); the rewrite nudges with the
// arrows (Move's end point with Alt+arrows), Shift a tenth. A press legacy
// nudged with is replayed as the rewrite's press for the same nudge: legacy
// read no Ctrl there, nor Alt unless alone, so they are dropped. Any other
// press is replayed as written; the rewrite ignores the letters, as legacy
// ignored them with Alt alone.

#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"

#include <string>
#include <string_view>

namespace hikari::application::visual::legacy_keys {

inline Key legacyKey(const std::string &k, bool shift, bool control, bool alt, Family family)
{
    Key key;
    key.key = k == "Delete" ? keys::Delete : k.empty() ? 0 : k[0];
    key.shift = shift;
    key.control = control;
    key.alt = alt;
    const bool vector = family == Family::VectorClip || family == Family::Drawing;
    const std::string_view start = "ADWS", end = "JLIK";
    const std::size_t letter = k.size() == 1 ? start.find(k[0]) : std::string_view::npos;
    const std::size_t endLetter =
            k.size() == 1 && family == Family::Move ? end.find(k[0]) : std::string_view::npos;
    const bool altAlone = alt && !control && !shift;
    const bool nudged = vector ? letter != std::string_view::npos && !(control && k == "A")
                               : (letter != std::string_view::npos || endLetter != std::string_view::npos) && !altAlone;
    if (!nudged)
        return key;
    constexpr int arrows[] = {keys::Left, keys::Right, keys::Up, keys::Down};
    key.key = arrows[letter != std::string_view::npos ? letter : endLetter];
    key.control = false;
    key.alt = endLetter != std::string_view::npos;
    return key;
}

} // namespace hikari::application::visual::legacy_keys
