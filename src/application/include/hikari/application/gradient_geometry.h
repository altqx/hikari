#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Plane geometry for gradients, in script pixels with y down. A Shape is filled with the nonzero rule, as
// libass fills drawings and vector clips.
namespace hikari::application::gradient {

struct Point {
    double x = 0, y = 0;
    bool operator==(const Point &) const = default;
};
using Contour = std::vector<Point>;
using Shape = std::vector<Contour>;

double signedArea(const Contour &contour);
double signedArea(const Shape &shape);
// The nonzero winding number of `shape` around `p`.
int winding(const Shape &shape, Point p);

// Keeps where a·p + c >= 0 (Sutherland–Hodgman against one edge).
Shape clipHalfPlane(const Shape &shape, Point a, double c);
// `convex` may wind either way.
Shape clipConvex(const Shape &subject, const Contour &convex);

// Rebuilds the region where the winding number is positive as contours that wind positively and do not
// cross; self-intersections, overlaps and inverted loops go.
Shape positiveRegion(const Shape &raw);
// The points of `shape` at least `distance` inside it (round joins at reflex corners); empty once nothing
// is left. `shape` may wind either way, have holes, be concave or touch itself.
Shape inset(const Shape &shape, double distance);

struct Band {
    Shape shape;
    double from = 0, to = 0; // the band's distance from the edge
};
// Rings `step` wide inward from the edge until nothing is left, at most `maxBands`; the last one is
// solid.
std::vector<Band> insetBands(const Shape &shape, double step, std::size_t maxBands = 512);

// Strips across a linear gradient: their sides follow `edge`; a point's place along the gradient is
// where its strip side meets start→end, 0 at start and `length` at end.
struct LinearFrame {
    Point start, dir, edge;
    double length = 0;
    double at(Point p) const;
};
// `skewDegrees` tilts the strip sides from square to start→end; nullopt for a zero-length gradient or a
// tilt of ±90° or more.
std::optional<LinearFrame> linearFrame(Point start, Point end, double skewDegrees = 0);
// The part of `region` from `from` to `to` along the gradient (either may be infinite).
Shape slab(const Shape &region, const LinearFrame &frame, double from, double to);

struct Rect {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
Contour rectContour(const Rect &rect);
// A single four-corner axis-aligned contour.
std::optional<Rect> asRect(const Shape &shape);

// ASS drawing commands m, n, l and b (curves flattened), divided by `divisor`; s, p and c end the spline.
Shape parseDrawing(std::string_view drawing, double divisor = 1);
// "m x y l x y …" per contour, `decimals` places without trailing zeros.
std::string formatShape(const Shape &shape, int decimals = 2);

// The phos masks: a drawing whose coverage, once blurred, ramps across the frame.
struct MaskFrame {
    Point p1, p2, p3;
};
std::string wedgeMask(const MaskFrame &frame, double thickness, double spacing);
std::string boxMask(const MaskFrame &frame, double spacing);
// p1 and p2 are the ends of a diameter.
std::string ringMask(Point p1, Point p2, double spacing);
std::string starMask(Point p1, Point p2, int spikes, double centre);
// p1 and p2 are opposite corners; `hole` is the share of the half size left clear in the middle.
std::string vignetteMask(Point p1, Point p2, double spacing, double hole);

}
