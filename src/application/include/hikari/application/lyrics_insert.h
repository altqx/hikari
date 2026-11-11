#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/core/document.h"

#include <expected>
#include <string_view>

namespace hikari::application {

// Adds `lyrics`' Lines after the session's last Line, each in `style`, as one history step
// named "Import lyrics", and selects them. Only an ASS Document takes them.
std::expected<void, CommandRefusal> addLyricsLines(EditSession &session, const core::Document &lyrics,
                                                   std::u8string_view style);

}
