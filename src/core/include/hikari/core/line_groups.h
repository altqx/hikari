#pragma once

// A group: a [tree_description] Line followed by a contiguous run of
// [tree_opened] / [tree_closed] members.

#include "hikari/core/document.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace hikari::core {

// Each member's description, or nullopt for an orphan (a member that does not
// follow its description or another member).
std::unordered_map<std::uint64_t, std::optional<LineId>> groupOwners(const Document &document);

// The members `after` breaks: a new orphan, or a member now in another group
// than in `before`.
std::vector<LineId> groupBreaks(const Document &before, const Document &after);

// A description's members in Document order (empty for a standalone description).
std::vector<LineId> groupMembers(const Document &document, LineId description);

}
