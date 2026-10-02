#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace hikari::core {

// Structural ASS load (C2). Keeps every byte: sections and records in authored
// order, each linked to its source span. Field parsing reproduces the legacy
// positional loader; anything it cannot interpret stays opaque with a
// diagnostic. SSA, TLMode pairing and annotation markers are separate cards.
LoadResult loadAss(std::span<const std::byte> bytes);

namespace legacy {
// The legacy SubsTime ASS parser: positional fields around the first ':'.
std::int64_t assTimeMilliseconds(std::u8string_view lexeme);
// True when the lexeme is canonical "h:mm:ss.cc" (surrounding whitespace allowed).
bool isCanonicalAssTime(std::u8string_view lexeme);
// wxAtoi: C strtol on a 64-bit long, then truncated to int.
std::int64_t atoi(std::u8string_view text);
} // namespace legacy

} // namespace hikari::core
