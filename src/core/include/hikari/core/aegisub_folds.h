#pragma once

#include "hikari/core/document.h"

#include <string_view>
#include <vector>

namespace hikari::core {

// arch1t3cht's Aegisub keeps each fold delimiter as "side;collapsed;id" under this key.
inline constexpr std::u8string_view kFoldKey = u8"_aegi_folddata";

// Both delimiters belong to the fold; a collapsed fold shows only its opener.
struct AegisubFold {
    LineId opener;
    LineId ender;
    bool collapsed = false;
    bool operator==(const AegisubFold &) const = default;
};

struct AegisubFolds {
    // In opener order, so a fold comes before the folds inside it.
    std::vector<AegisubFold> folds;
    // Delimiters Aegisub's pairing drops: unpaired, duplicated or crossing another fold.
    std::vector<LineId> dropped;
};

// The folds Aegisub reads from the Lines' extradata, paired and nested as its FoldController does.
AegisubFolds aegisubFolds(const Document &document);

}
