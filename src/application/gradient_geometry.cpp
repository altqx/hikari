#include "hikari/application/gradient_geometry.h"

#include <boost/dynamic_bitset.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <numbers>
#include <utility>

namespace hikari::application::gradient {
namespace {

Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point operator*(Point a, double k) { return {a.x * k, a.y * k}; }
double dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
double cross(Point a, Point b) { return a.x * b.y - a.y * b.x; }
double norm(Point a) { return std::hypot(a.x, a.y); }
Point unit(Point a)
{
    const double n = norm(a);
    return n > 0 ? a * (1 / n) : Point{};
}
Point leftNormal(Point d) { return {-d.y, d.x}; }

constexpr double kSnap = 1e-6;
constexpr double kInf = std::numeric_limits<double>::infinity();

std::pair<long long, long long> keyOf(Point p)
{
    return {std::llround(p.x / kSnap), std::llround(p.y / kSnap)};
}

Contour withoutRepeats(const Contour &c)
{
    Contour out;
    for (const Point &p : c)
        if (out.empty() || norm(p - out.back()) > kSnap)
            out.push_back(p);
    while (out.size() > 1 && norm(out.front() - out.back()) <= kSnap)
        out.pop_back();
    return out;
}

Contour reversed(Contour c)
{
    std::reverse(c.begin(), c.end());
    return c;
}

Shape reversed(const Shape &shape)
{
    Shape out;
    for (const auto &c : shape)
        out.push_back(reversed(c));
    return out;
}

Contour offsetContour(const Contour &c, double d)
{
    Contour out;
    const std::size_t n = c.size();
    if (n < 3)
        return out;
    for (std::size_t i = 0; i < n; ++i) {
        const Point prev = c[(i + n - 1) % n], v = c[i], next = c[(i + 1) % n];
        const Point e0 = unit(v - prev), e1 = unit(next - v);
        const Point n0 = leftNormal(e0), n1 = leftNormal(e1);
        const double turn = cross(e0, e1);
        if (std::abs(turn) <= 1e-12 && dot(e0, e1) > 0) {
            out.push_back(v + n0 * d);
            continue;
        }
        if (turn > 0) {
            out.push_back(v + n0 * d);
            out.push_back(v);
            out.push_back(v + n1 * d);
            continue;
        }
        const double a0 = std::atan2(n0.y, n0.x);
        double a1 = std::atan2(n1.y, n1.x);
        while (a1 > a0)
            a1 -= 2 * std::numbers::pi;
        const int steps = std::max(1, static_cast<int>(std::ceil((a0 - a1) / (std::numbers::pi / 16))));
        for (int s = 0; s <= steps; ++s) {
            const double a = a0 + (a1 - a0) * s / steps;
            out.push_back(v + Point{std::cos(a), std::sin(a)} * d);
        }
    }
    return out;
}

struct Segment {
    Point a, b;
};

std::vector<Segment> segmentsOf(const Shape &shape)
{
    std::vector<Segment> out;
    for (const auto &c : shape)
        for (std::size_t i = 0; i < c.size(); ++i) {
            const Point a = c[i], b = c[(i + 1) % c.size()];
            if (norm(b - a) > kSnap)
                out.push_back({a, b});
        }
    return out;
}

std::vector<Segment> splitAtCrossings(const std::vector<Segment> &segments)
{
    std::vector<std::vector<double>> cuts(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i)
        for (std::size_t j = i + 1; j < segments.size(); ++j) {
            const Point p = segments[i].a, r = segments[i].b - p;
            const Point q = segments[j].a, s = segments[j].b - q;
            const double denom = cross(r, s);
            const double rr = dot(r, r), ss = dot(s, s);
            if (std::abs(denom) <= 1e-12 * std::sqrt(rr * ss)) {
                if (std::abs(cross(q - p, r)) > 1e-9 * std::sqrt(rr))
                    continue;
                for (const Point e : {q, segments[j].b}) {
                    const double t = dot(e - p, r) / rr;
                    if (t > 0 && t < 1)
                        cuts[i].push_back(t);
                }
                for (const Point e : {p, segments[i].b}) {
                    const double u = dot(e - q, s) / ss;
                    if (u > 0 && u < 1)
                        cuts[j].push_back(u);
                }
                continue;
            }
            const double t = cross(q - p, s) / denom, u = cross(q - p, r) / denom;
            constexpr double e = 1e-12;
            if (t < -e || t > 1 + e || u < -e || u > 1 + e)
                continue;
            if (t > e && t < 1 - e)
                cuts[i].push_back(t);
            if (u > e && u < 1 - e)
                cuts[j].push_back(u);
        }
    std::vector<Segment> out;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        auto &c = cuts[i];
        std::sort(c.begin(), c.end());
        Point from = segments[i].a;
        const Point d = segments[i].b - segments[i].a;
        for (const double t : c) {
            const Point to = segments[i].a + d * t;
            if (norm(to - from) > kSnap)
                out.push_back({from, to});
            from = to;
        }
        if (norm(segments[i].b - from) > kSnap)
            out.push_back({from, segments[i].b});
    }
    return out;
}

double clipValue(Point a, double c, Point p) { return dot(a, p) + c; }

Contour clipContour(const Contour &in, Point a, double c)
{
    Contour out;
    const std::size_t n = in.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point p = in[i], q = in[(i + 1) % n];
        const double fp = clipValue(a, c, p), fq = clipValue(a, c, q);
        if (fp >= 0)
            out.push_back(p);
        if ((fp >= 0) != (fq >= 0))
            out.push_back(p + (q - p) * (fp / (fp - fq)));
    }
    return withoutRepeats(out);
}

std::string number(double v, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    double r = std::floor(std::abs(v) * scale + 0.5) / scale;
    if (v < 0 && r != 0)
        r = -r;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, r);
    std::string out = buf;
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0')
            out.pop_back();
        if (out.back() == '.')
            out.pop_back();
    }
    return out == "-0" ? "0" : out;
}

std::string pointText(Point p, int decimals = 3)
{
    return number(p.x, decimals) + " " + number(p.y, decimals);
}

std::string polygonText(const Contour &c)
{
    std::string out;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (i == 0)
            out += "m ";
        else if (i == 1)
            out += " l ";
        else
            out += " ";
        out += pointText(c[i]);
    }
    return out;
}

constexpr double kKappa = 0.5522847498;

std::string ellipseText(Point c, double rx, double ry, bool reverse)
{
    const double k = reverse ? -1 : 1;
    const Point start{c.x + rx, c.y};
    const Point pts[12] = {
        {c.x + rx, c.y - k * kKappa * ry}, {c.x + kKappa * rx, c.y - k * ry}, {c.x, c.y - k * ry},
        {c.x - kKappa * rx, c.y - k * ry}, {c.x - rx, c.y - k * kKappa * ry}, {c.x - rx, c.y},
        {c.x - rx, c.y + k * kKappa * ry}, {c.x - kKappa * rx, c.y + k * ry}, {c.x, c.y + k * ry},
        {c.x + kKappa * rx, c.y + k * ry}, {c.x + rx, c.y + k * kKappa * ry}, start,
    };
    std::string out = "m " + pointText(start) + " b";
    for (const Point &p : pts)
        out += " " + pointText(p);
    return out;
}

struct Frame {
    Point origin, u, n;
    double length = 0, height = 0;
    Point at(double a, double c) const { return origin + u * a + n * c; }
};

std::optional<Frame> frameOf(const MaskFrame &f)
{
    const double length = norm(f.p2 - f.p1);
    if (length <= 0)
        return std::nullopt;
    const Point u = (f.p2 - f.p1) * (1 / length);
    Point n = leftNormal(u);
    if (dot(f.p3 - f.p1, n) < 0)
        n = n * -1;
    return Frame{f.p1, u, n, length, std::abs(dot(f.p3 - f.p1, n))};
}

std::vector<std::pair<double, double>> duty(double extent, double spacing)
{
    const int count = std::max(1, static_cast<int>(std::ceil(extent / spacing)));
    const double sigma = extent / (static_cast<double>(count) * (count + 1));
    std::vector<std::pair<double, double>> out;
    double at = 0;
    for (int k = 1; k <= count; ++k) {
        const double band = (count - k + 1) * sigma;
        out.emplace_back(at, at + band);
        at += band + k * sigma;
    }
    return out;
}

double round4(double v) { return std::round(v * 1e4) / 1e4; }

}

double signedArea(const Contour &c)
{
    double a = 0;
    for (std::size_t i = 0; i < c.size(); ++i)
        a += cross(c[i], c[(i + 1) % c.size()]);
    return a / 2;
}

double signedArea(const Shape &shape)
{
    double a = 0;
    for (const auto &c : shape)
        a += signedArea(c);
    return a;
}

int winding(const Shape &shape, Point p)
{
    int w = 0;
    for (const auto &c : shape)
        for (std::size_t i = 0; i < c.size(); ++i) {
            const Point a = c[i], b = c[(i + 1) % c.size()];
            if (a.y <= p.y) {
                if (b.y > p.y && cross(b - a, p - a) > 0)
                    ++w;
            } else if (b.y <= p.y && cross(b - a, p - a) < 0) {
                --w;
            }
        }
    return w;
}

Shape clipHalfPlane(const Shape &shape, Point a, double c)
{
    Shape out;
    for (const auto &contour : shape) {
        auto clipped = clipContour(contour, a, c);
        if (clipped.size() >= 3 && std::abs(signedArea(clipped)) > kSnap)
            out.push_back(std::move(clipped));
    }
    return out;
}

Shape clipConvex(const Shape &subject, const Contour &convex)
{
    const Contour hull = withoutRepeats(convex);
    if (hull.size() < 3)
        return {};
    const double sign = signedArea(hull) >= 0 ? 1 : -1;
    Shape out = subject;
    for (std::size_t i = 0; i < hull.size() && !out.empty(); ++i) {
        const Point p = hull[i], q = hull[(i + 1) % hull.size()];
        const Point a = leftNormal(q - p) * sign;
        out = clipHalfPlane(out, a, -dot(a, p));
    }
    return out;
}

Shape positiveRegion(const Shape &raw)
{
    const auto pieces = splitAtCrossings(segmentsOf(raw));
    std::multimap<std::pair<long long, long long>, std::size_t> starts;
    std::vector<Segment> kept;
    for (const auto &s : pieces) {
        const Point mid = (s.a + s.b) * 0.5;
        const Point side = unit(leftNormal(s.b - s.a)) * 1e-4;
        const bool left = winding(raw, mid + side) > 0, right = winding(raw, mid - side) > 0;
        if (left == right)
            continue;
        kept.push_back(left ? s : Segment{s.b, s.a});
        starts.emplace(keyOf(kept.back().a), kept.size() - 1);
    }
    boost::dynamic_bitset<> used(kept.size());
    Shape out;
    for (std::size_t first = 0; first < kept.size(); ++first) {
        if (used[first])
            continue;
        Contour loop;
        std::size_t at = first;
        while (!used[at]) {
            used[at] = true;
            loop.push_back(kept[at].a);
            const Point dir = kept[at].b - kept[at].a;
            const auto [lo, hi] = starts.equal_range(keyOf(kept[at].b));
            std::size_t next = kept.size();
            double best = -kInf;
            for (auto it = lo; it != hi; ++it) {
                if (used[it->second] && it->second != first)
                    continue;
                const Point d = kept[it->second].b - kept[it->second].a;
                const double turn = std::atan2(cross(dir, d), dot(dir, d));
                if (turn > best) {
                    best = turn;
                    next = it->second;
                }
            }
            if (next == kept.size() || next == first)
                break;
            at = next;
        }
        loop = withoutRepeats(loop);
        if (loop.size() >= 3 && std::abs(signedArea(loop)) > 1e-6)
            out.push_back(std::move(loop));
    }
    return out;
}

Shape inset(const Shape &shape, double distance)
{
    Shape oriented;
    for (const auto &c : shape) {
        auto clean = withoutRepeats(c);
        if (clean.size() >= 3)
            oriented.push_back(std::move(clean));
    }
    if (oriented.empty())
        return {};
    if (signedArea(oriented) < 0)
        oriented = reversed(oriented);
    oriented = positiveRegion(oriented);
    if (distance <= 0)
        return oriented;
    Shape raw;
    for (const auto &c : oriented)
        raw.push_back(offsetContour(c, distance));
    return positiveRegion(raw);
}

std::vector<Band> insetBands(const Shape &shape, double step, std::size_t maxBands)
{
    std::vector<Band> out;
    if (step <= 0)
        return out;
    Shape outer = inset(shape, 0);
    for (std::size_t k = 0; k < maxBands && !outer.empty(); ++k) {
        const double from = static_cast<double>(k) * step, to = from + step;
        Shape inner = inset(shape, to);
        Band band{outer, from, to};
        for (const auto &c : inner)
            band.shape.push_back(reversed(c));
        out.push_back(std::move(band));
        outer = std::move(inner);
    }
    return out;
}

double LinearFrame::at(Point p) const
{
    return cross(p - start, edge) / cross(dir, edge);
}

std::optional<LinearFrame> linearFrame(Point start, Point end, double skewDegrees)
{
    const double length = norm(end - start);
    if (length <= 0 || std::abs(skewDegrees) >= 90)
        return std::nullopt;
    const Point dir = (end - start) * (1 / length);
    const double a = skewDegrees * std::numbers::pi / 180;
    const Point square = leftNormal(dir);
    const Point edge = unit(square * std::cos(a) + dir * std::sin(a));
    return LinearFrame{start, dir, edge, length};
}

Shape slab(const Shape &region, const LinearFrame &frame, double from, double to)
{
    const double k = cross(frame.dir, frame.edge);
    const Point a{frame.edge.y / k, -frame.edge.x / k};
    const double c0 = -dot(a, frame.start);
    Shape out = region;
    if (std::isfinite(from))
        out = clipHalfPlane(out, a, c0 - from);
    if (std::isfinite(to) && !out.empty())
        out = clipHalfPlane(out, a * -1, to - c0);
    return out;
}

Contour rectContour(const Rect &r)
{
    return {{r.x1, r.y1}, {r.x2, r.y1}, {r.x2, r.y2}, {r.x1, r.y2}};
}

std::optional<Rect> asRect(const Shape &shape)
{
    if (shape.size() != 1)
        return std::nullopt;
    const Contour c = withoutRepeats(shape.front());
    if (c.size() != 4)
        return std::nullopt;
    Rect r{c[0].x, c[0].y, c[0].x, c[0].y};
    for (const Point &p : c) {
        r.x1 = std::min(r.x1, p.x);
        r.y1 = std::min(r.y1, p.y);
        r.x2 = std::max(r.x2, p.x);
        r.y2 = std::max(r.y2, p.y);
    }
    for (const Point &p : c)
        if ((std::abs(p.x - r.x1) > kSnap && std::abs(p.x - r.x2) > kSnap) ||
            (std::abs(p.y - r.y1) > kSnap && std::abs(p.y - r.y2) > kSnap))
            return std::nullopt;
    if (r.x2 - r.x1 <= kSnap || r.y2 - r.y1 <= kSnap)
        return std::nullopt;
    return r;
}

Shape parseDrawing(std::string_view drawing, double divisor)
{
    std::vector<double> nums;
    Shape out;
    char op = 0;
    Point pen;
    std::vector<double> pending;
    const auto flush = [&] {
        if (!out.empty() && out.back().size() < 3)
            out.pop_back();
    };
    const auto take = [&](double v) {
        pending.push_back(v / divisor);
        if (op == 'm' || op == 'n') {
            if (pending.size() < 2)
                return;
            pen = {pending[0], pending[1]};
            pending.clear();
            flush();
            out.push_back({pen});
            op = 'l';
        } else if (op == 'l') {
            if (pending.size() < 2)
                return;
            pen = {pending[0], pending[1]};
            pending.clear();
            if (out.empty())
                out.push_back({});
            out.back().push_back(pen);
        } else if (op == 'b') {
            if (pending.size() < 6)
                return;
            const Point p0 = pen, p1{pending[0], pending[1]}, p2{pending[2], pending[3]}, p3{pending[4], pending[5]};
            pending.clear();
            if (out.empty())
                out.push_back({p0});
            for (int s = 1; s <= 16; ++s) {
                const double t = s / 16.0, u = 1 - t;
                out.back().push_back(p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t));
            }
            pen = p3;
        } else {
            pending.clear();
        }
    };
    std::size_t i = 0;
    while (i < drawing.size()) {
        const char ch = drawing[i];
        if (ch == 'm' || ch == 'n' || ch == 'l' || ch == 'b' || ch == 's' || ch == 'p' || ch == 'c') {
            op = ch == 's' || ch == 'p' || ch == 'c' ? 0 : ch;
            pending.clear();
            ++i;
            continue;
        }
        if (ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9')) {
            std::size_t j = i + 1;
            while (j < drawing.size() && (drawing[j] == '.' || drawing[j] == 'e' || (drawing[j] >= '0' && drawing[j] <= '9')))
                ++j;
            double v = 0;
            const char *begin = drawing.data() + i + (ch == '+' ? 1 : 0);
            std::from_chars(begin, drawing.data() + j, v);
            take(v);
            i = j;
            continue;
        }
        ++i;
    }
    flush();
    for (auto &c : out)
        c = withoutRepeats(c);
    std::erase_if(out, [](const Contour &c) { return c.size() < 3; });
    return out;
}

std::string formatShape(const Shape &shape, int decimals)
{
    std::string out;
    for (const auto &c : shape) {
        for (std::size_t i = 0; i < c.size(); ++i) {
            if (!out.empty())
                out += ' ';
            if (i == 0)
                out += "m ";
            else if (i == 1)
                out += "l ";
            out += number(c[i].x, decimals) + " " + number(c[i].y, decimals);
        }
    }
    return out;
}

std::string wedgeMask(const MaskFrame &f, double thickness, double spacing)
{
    const auto frame = frameOf(f);
    if (!frame || spacing <= 0)
        return {};
    const double t = std::clamp(thickness, 0.0, 1.0) * frame->height;
    Contour c{frame->at(0, 0), frame->at(0, t)};
    const int teeth = static_cast<int>(std::floor(frame->length / spacing));
    for (int k = 1; k <= teeth; ++k) {
        c.push_back(frame->at((k - 0.5) * spacing, frame->height));
        c.push_back(frame->at(k * spacing, t));
    }
    c.push_back(frame->at(frame->length, 0));
    return polygonText(c);
}

std::string boxMask(const MaskFrame &f, double spacing)
{
    const auto frame = frameOf(f);
    if (!frame || spacing <= 0)
        return {};
    std::string out;
    for (const auto &[from, to] : duty(frame->height, spacing)) {
        if (!out.empty())
            out += ' ';
        out += polygonText({frame->at(0, from), frame->at(frame->length, from), frame->at(frame->length, to),
                            frame->at(0, to)});
    }
    return out;
}

std::string ringMask(Point p1, Point p2, double spacing)
{
    const Point c = (p1 + p2) * 0.5;
    const double radius = norm(p2 - p1) / 2;
    if (radius <= 0 || spacing <= 0)
        return {};
    std::string out;
    for (const auto &[from, to] : duty(radius, spacing)) {
        if (!out.empty())
            out += ' ';
        out += ellipseText(c, round4(to), round4(to), false);
        if (round4(from) > 0)
            out += ' ' + ellipseText(c, round4(from), round4(from), true);
    }
    return out;
}

std::string starMask(Point p1, Point p2, int spikes, double centre)
{
    const Point c = (p1 + p2) * 0.5;
    const double outer = norm(p2 - p1) / 2;
    if (outer <= 0 || spikes < 2)
        return {};
    const double inner = outer * std::clamp(centre, 0.0, 1.0);
    const double step = 2 * std::numbers::pi / spikes;
    Contour star;
    for (int i = 0; i < spikes; ++i) {
        star.push_back(c + Point{std::cos(i * step), std::sin(i * step)} * inner);
        star.push_back(c + Point{std::cos((i + 0.5) * step), std::sin((i + 0.5) * step)} * outer);
    }
    return polygonText(star);
}

std::string vignetteMask(Point p1, Point p2, double spacing, double hole)
{
    const Point lo{std::min(p1.x, p2.x), std::min(p1.y, p2.y)}, hi{std::max(p1.x, p2.x), std::max(p1.y, p2.y)};
    const Point c = (lo + hi) * 0.5;
    const double a = (hi.x - lo.x) / 2, b = (hi.y - lo.y) / 2, most = std::max(a, b);
    if (most <= 0 || spacing <= 0)
        return {};
    const double width = most * (1 - std::clamp(hole, 0.0, 0.95));
    std::string out;
    for (const auto &[from, to] : duty(width, spacing)) {
        const double u = 1 - round4(from) / most, v = 1 - round4(to) / most;
        if (!out.empty())
            out += ' ';
        out += ellipseText(c, a * u, b * u, false) + ' ' + ellipseText(c, a * v, b * v, true);
    }
    return out;
}

}
