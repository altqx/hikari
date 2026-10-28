#pragma once

// Choosing a reader the way SubsLoader does: by extension first, falling back
// to the other readers, and counting a load as successful only when it found
// at least one Line. Same-format save then follows the Document's format.

#include "hikari/core/ass_save.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hikari::core {

// extension: lower-case, without the dot ("ass", "srt", "txt", ...).
// nullopt when no reader finds a Line (legacy: "Invalid format").
std::optional<LoadResult> loadSubtitle(std::span<const std::byte> bytes, std::u8string_view extension);

// Same-format save for the Document's format.
std::vector<std::byte> encodeSubtitle(const Document &document);
// The same, with the ASS save options (E5: TL_MODE_HIDE_ORIGINAL_ON_VIDEO).
std::vector<std::byte> encodeSubtitle(const Document &document, const AssSaveOptions &options);

} // namespace hikari::core
