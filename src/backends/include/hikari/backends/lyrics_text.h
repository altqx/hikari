#pragma once

#include "hikari/core/lyrics_import.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace hikari::backends {

// A byte order mark, then valid UTF-8, then uchardet's guess among the lyrics encodings; GB18030 otherwise.
core::LyricsEncoding detectLyricsEncoding(std::span<const std::byte> bytes);

// UTF-8 without a BOM; nullopt when the bytes are not text in `encoding`.
std::optional<std::vector<std::byte>> lyricsUtf8(std::span<const std::byte> bytes, core::LyricsEncoding encoding);

}
