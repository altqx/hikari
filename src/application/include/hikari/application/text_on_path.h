#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::text_on_path {

struct Point {
    double x = 0, y = 0;
    bool operator==(const Point &) const = default;
};

// Lines and cubic Béziers end to end, in script pixels, measured by exact arc length.
class Path {
public:
    // A vector drawing ("m", "n", "l", "b"; "s" B-splines become their Béziers), every
    // segment kept and each number divided by `divisor`. Empty without a segment.
    static Path fromDrawing(std::string_view drawing, double divisor = 1);

    void moveTo(Point at);
    void lineTo(Point to);
    void cubicTo(Point c1, Point c2, Point to);

    bool empty() const { return m_segments.empty(); }
    double length() const { return m_length; }
    Path reversed() const;

    struct Sample {
        Point at;
        Point tangent; // the curve's derivative there, not normalised
    };
    // At arc distance `s`; before the start and past the end the path goes straight on along its end tangents.
    Sample at(double s) const;

private:
    struct Segment {
        Point p[4];
        bool cubic = false;
        double start = 0, length = 0;
        std::vector<double> table; // arc length at t = i / (table.size() - 1)
    };
    static Point pointOf(const Segment &segment, double t);
    static Point derivativeOf(const Segment &segment, double t);
    static double arcOf(const Segment &segment, double from, double to);
    void push(Segment segment);

    std::vector<Segment> m_segments;
    std::optional<Point> m_pen;
    double m_length = 0;
};

// One grapheme cluster with the override tags before it, or the space a " ", "\h", "\N" or "\n" leaves.
struct Unit {
    std::string tags;  // every earlier override tag, in order, as one block's contents
    std::string text;
    double width = 0;
    bool space = false;
};
// Measures raw text with the tags in front of it (as "{tags}text").
using Measure = std::function<double(std::string_view raw)>;
// The Line text's units; positioning tags (\pos \move \an \a \org \clip \iclip, and \frz \fr when `dropRotation`)
// are left out of the carried tags.
std::vector<Unit> units(std::string_view text, const Measure &measure, bool dropRotation);

enum class Align { Left, Centre, Right, Spread };

struct Options {
    Align align = Align::Left;
    double offset = 0; // pixels along the path
    bool reverse = false;
    bool rotate = true;
};

struct Placed {
    std::size_t unit = 0;
    Point at;
    double rotation = 0; // \frz, degrees
};
struct Layout {
    std::vector<Placed> glyphs;
    double width = 0;    // every unit's width added up
    double overflow = 0; // how far the text runs past either end of the path, in pixels
};
// Where each non-space unit's bottom centre sits. `slide` moves the text's start by that much of the room the
// path leaves (0 to 1) after Options' offset, as zf.textInClip's Animated modes do.
Layout place(const Path &path, const std::vector<Unit> &units, const Options &options,
             std::optional<double> slide = std::nullopt);

// "{carried\an2\pos(x,y)\frz…}cluster" for a placed unit.
std::string glyphText(const Unit &unit, const Placed &placed, bool rotate);

// The text-on-path steps of a Line that slides: step j of n shows frames [first + j·every, …), up to `frames`.
int slideSteps(int frames, int every);

}
