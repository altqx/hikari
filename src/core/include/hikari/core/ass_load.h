#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace hikari::core {

// Keeps every byte: sections and records in authored order, each with its source span; what it cannot
// interpret stays opaque with a diagnostic.
LoadResult loadAss(std::span<const std::byte> bytes);

namespace legacy {
// Positional fields around the first ':', as HikariSub legacy parses them.
std::int64_t assTimeMilliseconds(std::u8string_view lexeme);
// True when the lexeme is canonical "h:mm:ss.cc" (surrounding whitespace allowed).
bool isCanonicalAssTime(std::u8string_view lexeme);
// C strtol on a 64-bit long, then truncated to int.
std::int64_t atoi(std::u8string_view text);
}

}
