#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace hikari::core {

// Every input byte stays in a record span.
LoadResult loadSrt(std::span<const std::byte> bytes);

// Unchanged records are copied byte for byte; an edited cue keeps its cue
// number and newline style.
std::vector<std::byte> encodeSrt(const Document &document);

namespace legacy {
// Positional fields around the first ':'.
std::int64_t srtTimeMilliseconds(std::u8string_view lexeme);
// "%02i:%02i:%02i,%03i".
std::u8string srtTimeText(std::int64_t milliseconds);
}

}
