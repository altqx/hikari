#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/clipboard_rows.h"

#include <expected>
#include <string>
#include <string_view>

namespace hikari::application {

// Script Info "TLMode: Yes".
bool translationMode(const EditSession &session);

// Every selected Line, hidden ones included, in Document order.
std::u8string copyRows(const EditSession &session);
// `columns`: core::column bits.
std::u8string copyColumns(const EditSession &session, int columns);

// Pasted Lines go before the first shown selected Line and become the
// selection; inside a group they join it, and a hidden next Line hides them.
std::expected<void, CommandRefusal> pasteRows(EditSession &session, std::u8string_view text,
                                              const LineVisible &visible = {},
                                              const core::PasteConversion &conversion = {});
// The n-th pasted line's fields go into the n-th shown selected Line; extra
// pasted lines are ignored.
std::expected<void, CommandRefusal> pasteColumns(EditSession &session, std::u8string_view text, int columns,
                                                 const LineVisible &visible = {},
                                                 const core::PasteConversion &conversion = {});

} // namespace hikari::application
