#pragma once

#include "hikari/core/style.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::application::lerp {

enum class Kind { Number, Colour, Alpha, Point, Rect };
enum class Blend { Rgb, Hsl };

// An override tag that can be interpolated. `key` merges spellings: "c" and "1c" are one tag, "fr" is "frz".
struct TagSpec {
    std::string_view key;
    Kind kind;
};
const TagSpec *tagSpec(std::string_view name);

// Colour: blue, green, red. Alpha: the value. Number: v[0]. Point: x, y. Rect: x1, y1, x2, y2.
struct Value {
    std::array<double, 4> v{};
    bool operator==(const Value &) const = default;
};

// `args` is what follows the name: "&HFF00FF&", "12.5", "(10,20)".
std::optional<Value> parse(const TagSpec &spec, std::string_view args);
// "\1c&HBBGGRR&", "\fscx102.5", "\pos(10,20)": colour and alpha channels round half up, numbers keep two
// decimals without trailing zeros, rects are whole pixels.
std::string format(const TagSpec &spec, const Value &value);
Value lerp(const TagSpec &spec, const Value &from, const Value &to, double t, Blend blend = Blend::Rgb);
// The Style's value; nullopt for \pos, \org and \clip, which have none.
std::optional<Value> styleDefault(const TagSpec &spec, const core::StyleValues &style);

// t^accel, with t clamped to [0, 1].
double accelerate(double t, double accel);
// The pair of neighbouring stops `t` falls between, of `stops` spread evenly over [0, 1], and the
// local t between them.
struct StopAt {
    std::size_t index = 0;
    double t = 0;
};
StopAt stopAt(double t, std::size_t stops);

}
