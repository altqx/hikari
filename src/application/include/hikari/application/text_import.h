#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/core/document.h"
#include "hikari/core/text_import.h"

#include <expected>
#include <span>
#include <string_view>

namespace hikari::application {

// A new file's Document, as File › New starts it, holding `lines` untimed in style Default.
core::LoadResult textImportFile(std::span<const core::TextImportLine> lines);

// Adds `lines` untimed after the session's last Line, each in `style`, as one history step named
// "Import text", and selects them. Only an ASS Document takes them.
std::expected<void, CommandRefusal> addTextLines(EditSession &session, std::span<const core::TextImportLine> lines,
                                                 std::u8string_view style);

}
