#pragma once

// Every command keeps groups contiguous and is refused with GroupBreak otherwise.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <expected>
#include <string>

namespace hikari::application {

// Each run of shown selected Lines gets a description before it, at the run's shallowest depth, and
// everything in it, including the hidden Lines of whole groups it contains, goes one level deeper
// into the new closed group. A hidden ordinary Line inside a run, or a run that cuts a group, is refused.
std::expected<void, CommandRefusal> makeGroups(EditSession &session, const LineVisible &shown = {});
// Flips the group's own marker on every Line inside it; groups nested in it keep theirs.
std::expected<void, CommandRefusal> toggleGroup(EditSession &session, core::LineId description);
std::expected<void, CommandRefusal> renameGroup(EditSession &session, core::LineId description, std::u8string text);
// The description is deleted and everything inside the group moves up one level.
std::expected<void, CommandRefusal> removeGroup(EditSession &session, core::LineId description);
// Opens the group, then selects everything inside it.
std::expected<void, CommandRefusal> selectGroup(EditSession &session, core::LineId description);
// Selected Lines at the group's own level move into it: those before to its start, those after to its
// end. A selected description brings its whole group, which nests inside.
std::expected<void, CommandRefusal> addLinesToGroup(EditSession &session, core::LineId description);
// The description and everything inside the group as clipboard rows.
std::u8string copyGroup(const EditSession &session, core::LineId description);

}
