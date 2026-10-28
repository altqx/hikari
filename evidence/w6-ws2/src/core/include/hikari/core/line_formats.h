#pragma once

// Line-based subtitle formats (C4-microdvd, C4-mpl2, C4-tmplayer, C4-text).
// Loading follows the legacy text path: SubsLoader::LoadTXT splits lines,
// Dialogue::SetRaw classifies each one, and SubsGrid::SetSubsFormat picks the
// Document's format from the first timed Line. ASS or SRT content found this
// way is reloaded with those loaders, as the legacy loader does.

#include "hikari/core/document.h"

#include <cstddef>
#include <span>
#include <vector>

namespace hikari::core {

LoadResult loadLineFormats(std::span<const std::byte> bytes);

// Same-format save: unchanged records copied byte for byte; an edited Line is
// regenerated in the legacy Dialogue::GetRaw form of the Document's format.
std::vector<std::byte> encodeLineFormat(const Document &document);

namespace legacy {
// SubsTime::raw for TMPlayer: "%02i:%02i:%02i".
std::u8string tmpTimeText(std::int64_t milliseconds);
} // namespace legacy

} // namespace hikari::core
