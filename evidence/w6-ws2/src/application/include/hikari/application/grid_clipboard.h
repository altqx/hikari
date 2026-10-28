#pragma once

// Grid clipboard commands (G2; legacy SubsGrid::CopyRows and OnPaste at
// 20d647c4). The text forms live in core/clipboard_rows.h; these choose the
// Lines, apply the result as one undo step and leave the selection where
// legacy does.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/clipboard_rows.h"

#include <expected>
#include <string>
#include <string_view>

namespace hikari::application {

// Script Info "TLMode: Yes" (legacy hasTLMode).
bool translationMode(const EditSession &session);

// GRID_COPY: every selected Line, hidden ones included, in Document order.
std::u8string copyRows(const EditSession &session);
// GRID_COPY_COLUMNS with the chosen core::column bits.
std::u8string copyColumns(const EditSession &session, int columns);

// GRID_PASTE ("Pasting lines"): the pasted Lines go before the first shown
// selected Line and become the selection, the first of them active. Inside a
// group they join it, and a hidden neighbour after them hides them (legacy
// treeState/isVisible rules). Nothing to paste changes nothing.
std::expected<void, CommandRefusal> pasteRows(EditSession &session, std::u8string_view text,
                                              const LineVisible &visible = {},
                                              const core::PasteConversion &conversion = {});
// GRID_PASTE_COLUMNS ("Pasting columns"): the n-th pasted line's chosen
// fields go into the n-th shown selected Line; extra pasted lines are
// ignored. The selection stays; its first shown Line becomes active.
std::expected<void, CommandRefusal> pasteColumns(EditSession &session, std::u8string_view text, int columns,
                                                 const LineVisible &visible = {},
                                                 const core::PasteConversion &conversion = {});

} // namespace hikari::application
