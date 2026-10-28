#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace hikari::core {

// SRT load (C4-srt). Cue grouping, cue-number removal and text joining follow
// the legacy SubsLoader::LoadSRT exactly; every byte stays in a record span.
LoadResult loadSrt(std::span<const std::byte> bytes);

// Same-format SRT save: unchanged records are copied byte for byte; an edited
// cue is regenerated in the legacy Dialogue::GetRaw SRT form, keeping its
// authored cue number and its newline style.
std::vector<std::byte> encodeSrt(const Document &document);

namespace legacy {
// SubsTime::ParseMS for SRT: positional fields around the first ':'.
std::int64_t srtTimeMilliseconds(std::u8string_view lexeme);
// SubsTime::raw for SRT: "%02i:%02i:%02i,%03i".
std::u8string srtTimeText(std::int64_t milliseconds);
} // namespace legacy

} // namespace hikari::core
