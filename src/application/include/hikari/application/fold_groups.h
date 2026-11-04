#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <cstddef>
#include <expected>

namespace hikari::application {

struct FoldConversion {
    std::size_t converted = 0;
    // Folds with a Line already in a line group, plus delimiters Aegisub's pairing drops.
    std::size_t skipped = 0;
};

// What converting would do now, without changing anything.
FoldConversion foldConversion(const core::Document &document);

// Each convertible Aegisub fold becomes a line group, nested as the folds are, in one undo step: a
// Comment description with the opener's text without tags goes before the opener, both delimiters
// and the Lines between become members, a collapsed fold closes its group, and the converted
// delimiters lose their _aegi_folddata. Skipped folds keep their entries.
std::expected<FoldConversion, CommandRefusal> convertFoldsToGroups(EditSession &session);

}
