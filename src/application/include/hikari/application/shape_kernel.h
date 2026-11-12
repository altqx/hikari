#pragma once

// The shape kernel: ASS drawing maths shared by the Lua helper (aegisub.__shape_impl, Yutils' fast
// paths), node effects and the Drawing tool. No Qt, no fonts: text outlines come through GlyphOutlinePort.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::shape {

// Ports of Yutils.lua's shape functions (Yutils 2014-11-14, as shipped in Automation/automation/Include)
// that give the same bytes and numbers as the Lua. nullopt means "not reproduced here": an input the Lua
// would reject with an error, or one outside what the port covers (a hex, exponent, inf or nan token,
// a -0, a non-finite result); the caller then runs the Lua.
namespace yutils {

struct Bounds {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct Bounding {
    std::optional<Bounds> bounds; // nullopt: the shape has no point (Yutils returns nothing)
};
std::optional<Bounding> bounding(std::string_view shape);
std::optional<std::string> move(std::string_view shape, double x, double y);
// Column-major as Yutils' create_matrix().get_data().
using Matrix = std::array<double, 16>;
std::optional<std::string> transform(std::string_view shape, const Matrix &matrix);
std::optional<std::string> flatten(std::string_view shape, double curveTolerance = 1);
enum class OutlineMode { Round, Bevel, Miter };
std::optional<std::string> toOutline(std::string_view shape, double widthXY, std::optional<double> widthY = {},
                                     OutlineMode mode = OutlineMode::Round);
struct Pixel {
    double alpha = 0, x = 0, y = 0;
};
std::optional<std::vector<Pixel>> toPixels(std::string_view shape);

}

// Geometry on flattened drawings, filled nonzero as libass fills them.
struct Point {
    double x = 0, y = 0;
    bool operator==(const Point &) const = default;
};
using Contour = std::vector<Point>;
using Contours = std::vector<Contour>;

// Reads an ASS drawing as libass tokenizes it (m, n, l, b, s, p, c), with Béziers and B-splines
// flattened to within `tolerance` units.
Contours readDrawing(std::string_view drawing, double tolerance = 0.25);
// "m x y l x y …" per contour, numbers with at most `decimals` places, trailing zeros dropped.
std::string writeDrawing(const Contours &contours, int decimals = 2);
void appendNumber(std::string &out, double value, int decimals);

enum class Combine { Unite, Subtract, Intersect, Exclude };
// `a` combined with `b`; Subtract takes `b` away from `a`.
Contours combine(const Contours &a, const Contours &b, Combine op);
enum class Join { Miter, Round, Square };
// Grows the filled area by `delta` units (shrinks it when negative).
Contours offset(const Contours &contours, double delta, Join join, double miterLimit = 2);
Contours shifted(Contours contours, double dx, double dy);

struct GlyphStyle {
    std::string family;
    double size = 0;
    bool bold = false, italic = false, underline = false, strikeOut = false;
    double scaleX = 100, scaleY = 100, spacing = 0;
};
struct TextOutline {
    std::string drawing; // the origin at the top left of the line box, the baseline at `ascent`
    double width = 0, height = 0, ascent = 0, descent = 0;
};
// Outlines text in a style's font, sized as libass sizes it (ascent plus descent is the size).
class GlyphOutlinePort {
public:
    virtual ~GlyphOutlinePort() = default;
    // nullopt: the font could not be found or read.
    virtual std::optional<TextOutline> outline(const GlyphStyle &style, std::string_view utf8) = 0;
};

}
