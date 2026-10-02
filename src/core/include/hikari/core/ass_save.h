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

// The legacy TL_MODE_HIDE_ORIGINAL_ON_VIDEO option: a regenerated TLMode
// original is written as a Comment. Off by default, as in the legacy app.
struct AssSaveOptions {
    bool hideOriginalOnVideo = false;
};
// An edited TLMode pair is regenerated as SubsGrid::SaveFile writes it: with a
// translation or Unconfirmed, the original line (TLMode Style, "\fD" effect
// when Unconfirmed) then the translation line; otherwise one line, with the
// translation if there is one. "TLMode: Translated" writes only that one line.
std::vector<std::byte> encodeAss(const Document &document, const AssSaveOptions &options);

namespace legacy {
// SubsTime::raw for ASS: "%01i:%02i:%02i.%02i" from integer milliseconds,
// truncated to centiseconds.
std::u8string assTimeText(std::int64_t milliseconds);
// Dialogue::GetRaw for an ASS Line, without the line terminator.
std::u8string assLineText(const LineRecord &line);
} // namespace legacy

} // namespace hikari::core
