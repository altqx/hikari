#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

enum Srv3Shadow : std::uint8_t { Srv3Glow = 1, Srv3HardShadow = 2, Srv3SoftShadow = 4, Srv3Bevel = 8 };

// YTSubConverter's per-style options. Colours are 0xRRGGBB and only count when `karaoke` is set.
struct Srv3StyleOptions {
    std::u16string style;
    std::uint8_t shadows = 0;
    bool karaoke = false;
    std::optional<std::uint32_t> currentWordText, currentWordOutline, currentWordShadow;
    bool operator==(const Srv3StyleOptions &) const = default;
};

// YouTube timed text (srv3, .ytt) converted from an ASS script exactly as YTSubConverter 1.6.5 converts it.
// `options` override YTSubConverter's built-in options for its YT* style names. The error is YTSubConverter's
// reason for refusing the script.
std::expected<std::vector<std::byte>, std::string> srv3FromAss(std::u16string_view script, const std::vector<Srv3StyleOptions> &options = {});

// What YTSubConverter uses for a style without options: its built-in options for a YT* name, else a soft shadow.
Srv3StyleOptions srv3DefaultOptions(std::u16string_view style);
// One of the 15 style names YTSubConverter has built-in options for.
bool isSrv3BuiltInStyle(std::u16string_view style);

// The "Hikari srv3 N: style,shadows,karaoke,#text,#outline,#shadow" [Script Info] keys, in order. Keys whose value
// doesn't parse are left out.
std::vector<Srv3StyleOptions> srv3StyleOptions(const Document &document);
// Rewrites the keys as one block numbered from 1, where the first key was, or after the last property. False
// when the Document can't hold Script Info.
bool setSrv3StyleOptions(Document &document, const std::vector<Srv3StyleOptions> &options);

}
