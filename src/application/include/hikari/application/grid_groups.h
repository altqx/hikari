#pragma once

// Every command keeps groups contiguous and is refused with GroupBreak otherwise.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <expected>
#include <string>

namespace hikari::application {

// Each run of shown selected Lines gets a description before it and its
// members close. A hidden ordinary Line inside a run is refused.
std::expected<void, CommandRefusal> makeGroups(EditSession &session, const LineVisible &shown = {});
// Closed members open, open ones close.
std::expected<void, CommandRefusal> toggleGroup(EditSession &session, core::LineId description);
std::expected<void, CommandRefusal> renameGroup(EditSession &session, core::LineId description, std::u8string text);
// The members become ordinary shown Lines and the description is deleted.
std::expected<void, CommandRefusal> removeGroup(EditSession &session, core::LineId description);
// Opens closed members, then selects the members.
std::expected<void, CommandRefusal> selectGroup(EditSession &session, core::LineId description);
// Selected Lines before the group move to its start, those after to its end.
std::expected<void, CommandRefusal> addLinesToGroup(EditSession &session, core::LineId description);
// The description and members as clipboard rows.
std::u8string copyGroup(const EditSession &session, core::LineId description);

}
