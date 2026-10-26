#pragma once

// KE3 (#285): the built-in karaoke effect presets (docs/qt/karaoke-presets.md).
// Each preset is a small ASS file in src/application/karaoke-presets: Script
// Info carries its Title and Description, and its Lines are template and code
// Comments in the stock templater's syntax, so they stay portable to Aegisub.
// Qt-free.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hikari::application {

struct KaraokePresetLine {
    std::int64_t layer = 0;
    std::u8string effect; // "template ..." or "code ..."
    std::u8string text;
};

struct KaraokePreset {
    std::u8string title;
    std::u8string description;
    std::vector<KaraokePresetLine> lines; // in file order
};

// The preset of a preset file: nullopt unless it has a Title and a
// Description and its Lines are template or code Comments, one at least.
std::optional<KaraokePreset> readKaraokePreset(std::span<const std::byte> bytes);

} // namespace hikari::application
