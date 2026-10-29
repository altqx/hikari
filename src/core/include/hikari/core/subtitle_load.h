#pragma once

#include "hikari/core/ass_save.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hikari::core {

// `extension` lower-case without the dot; its reader goes first, then the others. nullopt when no
// reader finds a Line.
std::optional<LoadResult> loadSubtitle(std::span<const std::byte> bytes, std::u8string_view extension);

std::vector<std::byte> encodeSubtitle(const Document &document);
std::vector<std::byte> encodeSubtitle(const Document &document, const AssSaveOptions &options);

}
