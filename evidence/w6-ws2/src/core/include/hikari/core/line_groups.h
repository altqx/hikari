#pragma once

// Line groups (legacy trees, G9) as the Actor markers describe them: a
// description Line ([tree_description]) followed by a contiguous run of
// members ([tree_opened] / [tree_closed]). Accepted G56-contiguity: a command
// may keep groups valid, but must not orphan a member or move an existing
// member into another group; such a result is rejected before it is applied.

#include "hikari/core/document.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace hikari::core {

// Each member's description, or nullopt for an orphan (a member that does not
// follow its description or another member). Imported malformed runs stay
// as they are: they are reported, not repaired.
std::unordered_map<std::uint64_t, std::optional<LineId>> groupOwners(const Document &document);

// The members `after` breaks: an orphan that was not already one (new
// malformed groups are not created either), or a member now in another
// group than in `before`.
std::vector<LineId> groupBreaks(const Document &before, const Document &after);

// A description's members in Document order (empty for a standalone description).
std::vector<LineId> groupMembers(const Document &document, LineId description);

} // namespace hikari::core
