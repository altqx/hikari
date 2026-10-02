#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hikari::core {

// Same-format ASS save (C3). Every record whose source span is still valid is
// copied byte for byte, terminator included; an edited Line is regenerated in
// the legacy serialization and keeps its original terminator. An unchanged
// Document therefore encodes to exactly its input.
std::vector<std::byte> encodeAss(const Document &document);

namespace legacy {
// SubsTime::raw for ASS: "%01i:%02i:%02i.%02i" from integer milliseconds,
// truncated to centiseconds.
std::u8string assTimeText(std::int64_t milliseconds);
// Dialogue::GetRaw for an ASS Line, without the line terminator.
std::u8string assLineText(const LineRecord &line);
} // namespace legacy

} // namespace hikari::core
