#pragma once

// Line groups (G9; legacy trees: SubsGridFiltering::MakeTree,
// SubsFile::OpenCloseTree and SubsGrid::TreeAddLines/TreeCopy/
// TreeChangeName/TreeRemove/TreeSelect at 20d647c4). A group is a
// description Line followed by its members; every command here keeps groups
// valid (G56-contiguity) and is refused with GroupBreak otherwise.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <expected>
#include <string>

namespace hikari::application {

// GRID_TREE_MAKE ("Adding tree"): each run of shown selected Lines gets a
// description before it (a comment in the first member's Style, no text) and
// its members are closed. Hidden Lines do not end a run, so a hidden ordinary
// Line inside one would be left between members: that is refused.
std::expected<void, CommandRefusal> makeGroups(EditSession &session, const LineVisible &shown = {});
// A click on a description: closed members open, open ones close.
std::expected<void, CommandRefusal> toggleGroup(EditSession &session, core::LineId description);
// "Change description" ("Setting tree description").
std::expected<void, CommandRefusal> renameGroup(EditSession &session, core::LineId description, std::u8string text);
// "Delete" ("Removing tree"): the members become ordinary shown Lines and the
// description Line is deleted (G56: removal visibly deletes it).
std::expected<void, CommandRefusal> removeGroup(EditSession &session, core::LineId description);
// "Select tree lines": closed members open, then the members are selected.
std::expected<void, CommandRefusal> selectGroup(EditSession &session, core::LineId description);
// "Add lines" ("Adding line to tree"): selected Lines outside the group move
// into it, those before it to its start and those after to its end.
std::expected<void, CommandRefusal> addLinesToGroup(EditSession &session, core::LineId description);
// "Copy tree": the description and members in the legacy clipboard form.
std::u8string copyGroup(const EditSession &session, core::LineId description);

} // namespace hikari::application
