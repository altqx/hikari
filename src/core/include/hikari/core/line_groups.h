#pragma once

// A group: a [tree_description] Line followed by a contiguous run of Lines with one more
// [tree_opened] / [tree_closed] marker than the description; deeper Lines in the run belong
// to groups nested inside it.

#include "hikari/core/document.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace hikari::core {

// One [tree_opened] / [tree_closed] marker per group the Line is inside, outermost first.
std::vector<GroupMarker> groupMemberships(const LineRecord &line);
std::size_t groupDepth(const LineRecord &line);
// A Line inside a closed group is hidden; one that leaves every group is shown again.
void setGroupMarkers(LineRecord &line, std::vector<GroupMarker> memberships, bool description);

// The description of each Line's innermost group, or nullopt for an orphan (a Line with a
// marker that no description at that depth encloses). Lines outside every group are absent.
std::unordered_map<std::uint64_t, std::optional<LineId>> groupOwners(const Document &document);

// The Lines `after` breaks: a new orphan, or a Line whose markers are unchanged but that is now in
// another group than in `before`. Group commands regroup Lines by rewriting their markers.
std::vector<LineId> groupBreaks(const Document &before, const Document &after);

// A description's direct members and inner descriptions, in Document order.
std::vector<LineId> groupMembers(const Document &document, LineId description);

// Every Line inside a description's group at any depth, in Document order.
std::vector<LineId> groupContents(const Document &document, LineId description);

}
