#pragma once

#include "hikari/application/karaoke_layout.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::application {

struct LinePlacement {
    int align = 2;
    std::optional<std::pair<double, double>> pos;
    std::string rawWithoutPosition;
};

// The first \an, and the first \pos or \move's start, of a Line's override blocks; and its text
// with every \pos and \move taken out.
LinePlacement linePlacement(std::string_view text, int styleAlign);

// A karaoke layout of an ordinary Line, as a typesetting graph reads it: its units are placed
// at its \pos (or \move's start) by its \an, its raw text has no \pos or \move, and a Line with no
// karaoke tags is one syllable spanning the Line, blank only when it holds only spaces.
void placeTypesettingLine(karaoke_layout::Line &line);

}
