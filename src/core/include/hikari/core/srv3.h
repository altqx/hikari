#pragma once

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

}
