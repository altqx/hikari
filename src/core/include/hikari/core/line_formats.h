#pragma once

// The format comes from the first timed Line; ASS or SRT content reloads with those loaders.

#include "hikari/core/document.h"

#include <cstddef>
#include <span>
#include <vector>

namespace hikari::core {

LoadResult loadLineFormats(std::span<const std::byte> bytes);

// Unchanged records are copied byte for byte; edited Lines are regenerated.
std::vector<std::byte> encodeLineFormat(const Document &document);

namespace legacy {
// TMPlayer "%02i:%02i:%02i".
std::u8string tmpTimeText(std::int64_t milliseconds);
} // namespace legacy

} // namespace hikari::core
