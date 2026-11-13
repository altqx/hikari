#include "hikari/application/shape_kernel.h"

#include <clipper2/clipper.h>

#include <charconv>
#include <cmath>

namespace hikari::application::shape {

namespace {

constexpr int kClipperDecimals = 4;
constexpr int kMaxCurveDepth = 16;

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

std::vector<std::string_view> words(std::string_view s)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i]))
            ++i;
        const std::size_t start = i;
        while (i < s.size() && !isSpace(s[i]))
            ++i;
        if (i > start)
            out.push_back(s.substr(start, i - start));
    }
    return out;
}

std::optional<double> number(std::string_view w)
{
    if (w.starts_with('+'))
        w.remove_prefix(1);
    double v = 0;
    const auto [end, ec] = std::from_chars(w.data(), w.data() + w.size(), v);
    if (ec != std::errc{} || end != w.data() + w.size() || !std::isfinite(v))
        return std::nullopt;
    return v;
}

double chordDistance(Point p, Point a, Point b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    if (len == 0)
        return std::hypot(p.x - a.x, p.y - a.y);
    return std::abs((p.x - a.x) * dy - (p.y - a.y) * dx) / len;
}

void flattenBezier(Point p0, Point p1, Point p2, Point p3, double tolerance, int depth, Contour &out)
{
    if (depth >= kMaxCurveDepth ||
        std::max(chordDistance(p1, p0, p3), chordDistance(p2, p0, p3)) <= tolerance) {
        out.push_back(p3);
        return;
    }
    const auto mid = [](Point a, Point b) { return Point{(a.x + b.x) / 2, (a.y + b.y) / 2}; };
    const Point p01 = mid(p0, p1), p12 = mid(p1, p2), p23 = mid(p2, p3);
    const Point p012 = mid(p01, p12), p123 = mid(p12, p23), m = mid(p012, p123);
    flattenBezier(p0, p01, p012, m, tolerance, depth + 1, out);
    flattenBezier(m, p123, p23, p3, tolerance, depth + 1, out);
}

// One uniform cubic B-spline span as libass draws it: the Bézier through its four control points.
void flattenSpline(Point a, Point b, Point c, Point d, double tolerance, Contour &out)
{
    const Point p0{(a.x + 4 * b.x + c.x) / 6, (a.y + 4 * b.y + c.y) / 6};
    const Point p1{(2 * b.x + c.x) / 3, (2 * b.y + c.y) / 3};
    const Point p2{(b.x + 2 * c.x) / 3, (b.y + 2 * c.y) / 3};
    const Point p3{(b.x + 4 * c.x + d.x) / 6, (b.y + 4 * c.y + d.y) / 6};
    if (out.empty() || !(out.back() == p0))
        out.push_back(p0);
    flattenBezier(p0, p1, p2, p3, tolerance, 0, out);
}

Clipper2Lib::PathsD toClipper(const Contours &contours)
{
    Clipper2Lib::PathsD paths;
    paths.reserve(contours.size());
    for (const Contour &c : contours) {
        if (c.size() < 3)
            continue;
        Clipper2Lib::PathD path;
        path.reserve(c.size());
        for (const Point p : c)
            path.emplace_back(p.x, p.y);
        paths.push_back(std::move(path));
    }
    return paths;
}

Contours fromClipper(const Clipper2Lib::PathsD &paths)
{
    Contours out;
    out.reserve(paths.size());
    for (const auto &path : paths) {
        Contour c;
        c.reserve(path.size());
        for (const auto &p : path)
            c.push_back({p.x, p.y});
        out.push_back(std::move(c));
    }
    return out;
}

}

void appendNumber(std::string &out, double v, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    v = std::round(v * scale) / scale;
    if (v == 0)
        v = 0;
    char buffer[64];
    const auto end = std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::fixed, decimals).ptr;
    std::string_view s(buffer, static_cast<std::size_t>(end - buffer));
    if (s.find('.') != std::string_view::npos) {
        while (s.ends_with('0'))
            s.remove_suffix(1);
        if (s.ends_with('.'))
            s.remove_suffix(1);
    }
    out += s;
}

Contours readDrawing(std::string_view drawing, double tolerance)
{
    Contours contours;
    Contour current;
    Point cursor;
    char command = 0;
    std::vector<Point> pending; // a command's points waiting for their count
    std::vector<Point> spline;  // the open B-spline's control points, the start point first
    const auto finish = [&] {
        if (current.size() > 1)
            contours.push_back(std::move(current));
        current.clear();
    };
    const auto endSpline = [&](bool close) {
        if (close && spline.size() >= 4) {
            for (int i = 0; i < 3; ++i)
                spline.push_back(spline[static_cast<std::size_t>(i)]);
        }
        for (std::size_t i = 0; i + 3 < spline.size(); ++i)
            flattenSpline(spline[i], spline[i + 1], spline[i + 2], spline[i + 3], tolerance, current);
        if (spline.size() >= 4)
            cursor = current.back();
        spline.clear();
    };
    bool hasMove = false;
    const auto ws = words(drawing);
    for (std::size_t i = 0; i < ws.size(); ++i) {
        const std::string_view w = ws[i];
        if (w.size() == 1 && std::string_view("mnlbspc").find(w[0]) != std::string_view::npos) {
            if (command == 's' || command == 'p') {
                if (w[0] != 'p')
                    endSpline(w[0] == 'c');
            }
            command = w[0];
            pending.clear();
            if (command == 'c')
                command = 0;
            if (command == 's')
                spline = {cursor};
            continue;
        }
        const auto x = number(w);
        if (!x || i + 1 >= ws.size())
            continue;
        const auto y = number(ws[i + 1]);
        if (!y)
            continue;
        ++i;
        const Point p{*x, *y};
        switch (command) {
        case 'm':
        case 'n':
            finish();
            current.push_back(p);
            cursor = p;
            hasMove = true;
            break;
        case 'l':
            if (!hasMove)
                break;
            current.push_back(p);
            cursor = p;
            break;
        case 'b':
            if (!hasMove)
                break;
            pending.push_back(p);
            if (pending.size() == 3) {
                flattenBezier(cursor, pending[0], pending[1], pending[2], tolerance, 0, current);
                cursor = pending[2];
                pending.clear();
            }
            break;
        case 's':
        case 'p':
            if (hasMove && !spline.empty())
                spline.push_back(p);
            break;
        default:
            break;
        }
    }
    if (command == 's' || command == 'p')
        endSpline(false);
    finish();
    return contours;
}

std::string writeDrawing(const Contours &contours, int decimals)
{
    std::string out;
    for (const Contour &c : contours) {
        if (c.size() < 2)
            continue;
        if (!out.empty())
            out += ' ';
        out += "m ";
        appendNumber(out, c[0].x, decimals);
        out += ' ';
        appendNumber(out, c[0].y, decimals);
        out += " l";
        for (std::size_t i = 1; i < c.size(); ++i) {
            out += ' ';
            appendNumber(out, c[i].x, decimals);
            out += ' ';
            appendNumber(out, c[i].y, decimals);
        }
    }
    return out;
}

Contours combine(const Contours &a, const Contours &b, Combine op)
{
    using namespace Clipper2Lib;
    const PathsD subject = toClipper(a), clip = toClipper(b);
    constexpr FillRule rule = FillRule::NonZero;
    switch (op) {
    case Combine::Unite: return fromClipper(Union(subject, clip, rule, kClipperDecimals));
    case Combine::Subtract: return fromClipper(Difference(subject, clip, rule, kClipperDecimals));
    case Combine::Intersect: return fromClipper(Intersect(subject, clip, rule, kClipperDecimals));
    case Combine::Exclude: return fromClipper(Xor(subject, clip, rule, kClipperDecimals));
    }
    return {};
}

Contours offset(const Contours &contours, double delta, Join join, double miterLimit)
{
    using namespace Clipper2Lib;
    const PathsD filled = Union(toClipper(contours), FillRule::NonZero, kClipperDecimals);
    if (delta == 0)
        return fromClipper(filled);
    const JoinType type = join == Join::Miter ? JoinType::Miter : join == Join::Round ? JoinType::Round
                                                                                     : JoinType::Square;
    return fromClipper(InflatePaths(filled, delta, type, EndType::Polygon, miterLimit, kClipperDecimals));
}

Contours shifted(Contours contours, double dx, double dy)
{
    for (Contour &c : contours)
        for (Point &p : c) {
            p.x += dx;
            p.y += dy;
        }
    return contours;
}

}
