#include "hikari/application/text_on_path.h"

#include "motion_tags.h"

#include "hikari/core/graphemes.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>

namespace hikari::application::text_on_path {

namespace {

constexpr int kTableSteps = 64;
constexpr std::array<double, 5> kGaussX{-0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831,
                                        0.9061798459386640};
constexpr std::array<double, 5> kGaussW{0.2369268850561891, 0.4786286704993665, 0.5688888888888889,
                                        0.4786286704993665, 0.2369268850561891};

Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point operator*(Point a, double k) { return {a.x * k, a.y * k}; }
double norm(Point a) { return std::hypot(a.x, a.y); }

Point unit(Point a)
{
    const double n = norm(a);
    return n > 0 ? a * (1 / n) : Point{1, 0};
}

std::vector<double> numbersFrom(std::string_view drawing, std::size_t &i)
{
    std::vector<double> out;
    while (i < drawing.size()) {
        while (i < drawing.size() && drawing[i] == ' ')
            ++i;
        if (i >= drawing.size())
            break;
        const char c = drawing[i];
        if (!(c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')))
            break;
        const std::string token(drawing.substr(i, drawing.find(' ', i) - i));
        out.push_back(std::strtod(token.c_str(), nullptr));
        i += token.size();
    }
    return out;
}

std::string tagName(std::string_view tag)
{
    std::size_t i = 1;
    if (i < tag.size() && tag[i] >= '1' && tag[i] <= '4')
        ++i;
    std::size_t end = i;
    while (end < tag.size() && tag[end] >= 'a' && tag[end] <= 'z')
        ++end;
    return std::string(tag.substr(i, end - i));
}

bool positional(const std::string &name, bool dropRotation)
{
    static constexpr std::string_view kDropped[] = {"pos", "move", "an", "a", "org", "clip", "iclip"};
    if (std::ranges::contains(kDropped, name))
        return true;
    return dropRotation && (name == "frz" || name == "fr");
}

std::string number(double value)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", value);
    std::string s = buf;
    while (s.back() == '0')
        s.pop_back();
    if (s.back() == '.')
        s.pop_back();
    return s == "-0" ? "0" : s;
}

}

Path Path::fromDrawing(std::string_view drawing, double divisor)
{
    Path path;
    const double k = divisor > 0 ? 1 / divisor : 1;
    std::size_t i = 0;
    char command = 0;
    std::vector<Point> spline;
    const auto flushSpline = [&] {
        if (spline.size() >= 4)
            for (std::size_t s = 0; s + 3 < spline.size(); ++s) {
                const Point p0 = spline[s], p1 = spline[s + 1], p2 = spline[s + 2], p3 = spline[s + 3];
                if (s == 0)
                    path.moveTo((p0 + p1 * 4 + p2) * (1.0 / 6));
                path.cubicTo((p1 * 2 + p2) * (1.0 / 3), (p1 + p2 * 2) * (1.0 / 3), (p1 + p2 * 4 + p3) * (1.0 / 6));
            }
        spline.clear();
    };
    while (i < drawing.size()) {
        const char c = drawing[i];
        if (c == ' ') {
            ++i;
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            if (command == 's' && c != 'p')
                flushSpline();
            command = c;
            ++i;
            continue;
        }
        const std::vector<double> v = numbersFrom(drawing, i);
        if (v.empty()) {
            ++i;
            continue;
        }
        const auto pt = [&](std::size_t at) { return Point{v[at] * k, v[at + 1] * k}; };
        if (command == 'm' || command == 'n') {
            for (std::size_t at = 0; at + 1 < v.size(); at += 2)
                path.moveTo(pt(at));
        } else if (command == 'l') {
            for (std::size_t at = 0; at + 1 < v.size(); at += 2)
                path.lineTo(pt(at));
        } else if (command == 'b') {
            for (std::size_t at = 0; at + 5 < v.size(); at += 6)
                path.cubicTo(pt(at), pt(at + 2), pt(at + 4));
        } else if (command == 's' || command == 'p') {
            if (spline.empty() && path.m_pen)
                spline.push_back(*path.m_pen);
            for (std::size_t at = 0; at + 1 < v.size(); at += 2)
                spline.push_back(pt(at));
        }
    }
    flushSpline();
    return path;
}

void Path::moveTo(Point at)
{
    m_pen = at;
}

void Path::lineTo(Point to)
{
    if (!m_pen) {
        m_pen = to;
        return;
    }
    Segment s;
    s.p[0] = *m_pen;
    s.p[1] = *m_pen;
    s.p[2] = to;
    s.p[3] = to;
    push(std::move(s));
}

void Path::cubicTo(Point c1, Point c2, Point to)
{
    if (!m_pen) {
        m_pen = to;
        return;
    }
    Segment s;
    s.p[0] = *m_pen;
    s.p[1] = c1;
    s.p[2] = c2;
    s.p[3] = to;
    s.cubic = true;
    push(std::move(s));
}

void Path::push(Segment segment)
{
    m_pen = segment.p[3];
    segment.start = m_length;
    if (segment.cubic) {
        segment.table.assign(kTableSteps + 1, 0);
        for (int i = 1; i <= kTableSteps; ++i)
            segment.table[i] = segment.table[i - 1] + arcOf(segment, double(i - 1) / kTableSteps, double(i) / kTableSteps);
        segment.length = segment.table.back();
    } else {
        segment.length = norm(segment.p[3] - segment.p[0]);
    }
    if (segment.length <= 0)
        return;
    m_length += segment.length;
    m_segments.push_back(std::move(segment));
}

Point Path::pointOf(const Segment &s, double t)
{
    if (!s.cubic)
        return s.p[0] + (s.p[3] - s.p[0]) * t;
    const double u = 1 - t;
    return s.p[0] * (u * u * u) + s.p[1] * (3 * t * u * u) + s.p[2] * (3 * t * t * u) + s.p[3] * (t * t * t);
}

Point Path::derivativeOf(const Segment &s, double t)
{
    if (!s.cubic)
        return s.p[3] - s.p[0];
    const double u = 1 - t;
    Point d = (s.p[1] - s.p[0]) * (3 * u * u) + (s.p[2] - s.p[1]) * (6 * u * t) + (s.p[3] - s.p[2]) * (3 * t * t);
    if (norm(d) > 1e-9)
        return d;
    return t < 0.5 ? (s.p[2] == s.p[0] ? s.p[3] - s.p[0] : s.p[2] - s.p[0])
                   : (s.p[1] == s.p[3] ? s.p[3] - s.p[0] : s.p[3] - s.p[1]);
}

double Path::arcOf(const Segment &s, double from, double to)
{
    const double half = (to - from) / 2, mid = (to + from) / 2;
    double sum = 0;
    for (std::size_t i = 0; i < kGaussX.size(); ++i)
        sum += kGaussW[i] * norm(derivativeOf(s, mid + half * kGaussX[i]));
    return sum * half;
}

Path Path::reversed() const
{
    Path out;
    for (auto it = m_segments.rbegin(); it != m_segments.rend(); ++it) {
        out.moveTo(out.m_pen.value_or(it->p[3]));
        if (it->cubic)
            out.cubicTo(it->p[2], it->p[1], it->p[0]);
        else
            out.lineTo(it->p[0]);
    }
    return out;
}

Path::Sample Path::at(double s) const
{
    if (m_segments.empty())
        return {};
    if (s <= 0) {
        const Segment &first = m_segments.front();
        const Point d = derivativeOf(first, 0);
        return {first.p[0] + unit(d) * s, d};
    }
    if (s >= m_length) {
        const Segment &last = m_segments.back();
        const Point d = derivativeOf(last, 1);
        return {last.p[3] + unit(d) * (s - m_length), d};
    }
    const auto it = std::ranges::upper_bound(m_segments, s, {}, &Segment::start);
    const Segment &seg = *std::prev(it);
    const double local = s - seg.start;
    if (!seg.cubic) {
        const double t = local / seg.length;
        return {pointOf(seg, t), derivativeOf(seg, t)};
    }
    const auto cell = std::ranges::upper_bound(seg.table, local);
    const int i = std::clamp(int(cell - seg.table.begin()) - 1, 0, kTableSteps - 1);
    const double cellStart = double(i) / kTableSteps;
    double lo = cellStart, hi = double(i + 1) / kTableSteps;
    double t = lo + (hi - lo) * (local - seg.table[i]) / std::max(1e-12, seg.table[i + 1] - seg.table[i]);
    for (int round = 0; round < 30; ++round) {
        const double error = seg.table[i] + arcOf(seg, cellStart, t) - local;
        if (std::abs(error) < 1e-10)
            break;
        if (error > 0)
            hi = t;
        else
            lo = t;
        const double speed = norm(derivativeOf(seg, t));
        const double next = speed > 0 ? t - error / speed : (lo + hi) / 2;
        t = next > lo && next < hi ? next : (lo + hi) / 2;
    }
    return {pointOf(seg, t), derivativeOf(seg, t)};
}

std::vector<Unit> units(std::string_view text, const Measure &measure, bool dropRotation)
{
    std::vector<Unit> out;
    std::string tags;
    const auto measured = [&](const std::string &raw) { return measure(tags.empty() ? raw : "{" + tags + "}" + raw); };
    const auto space = [&](const std::string &shown) {
        out.push_back({tags, shown, measured(shown), true});
    };
    for (const motion::Piece &piece : motion::parseText(text)) {
        if (piece.block) {
            for (const std::string &tag : piece.tags)
                if (tag.starts_with('\\') && !positional(tagName(tag), dropRotation))
                    tags += tag;
            continue;
        }
        const std::u16string wide = core::toUtf16(std::u8string_view(
            reinterpret_cast<const char8_t *>(piece.text.data()), piece.text.size()));
        std::size_t at = 0;
        while (at < wide.size()) {
            if (wide[at] == u'\\' && at + 1 < wide.size() &&
                (wide[at + 1] == u'N' || wide[at + 1] == u'n' || wide[at + 1] == u'h')) {
                space(" ");
                at += 2;
                continue;
            }
            if (wide[at] == u' ') {
                space(" ");
                ++at;
                continue;
            }
            const std::size_t end = std::max(at + 1, core::graphemeEnd(wide, at));
            const std::u8string cluster = core::toUtf8(std::u16string_view(wide).substr(at, end - at));
            const std::string shown(cluster.begin(), cluster.end());
            out.push_back({tags, shown, measured(shown), false});
            at = end;
        }
    }
    return out;
}

Layout place(const Path &path, const std::vector<Unit> &units, const Options &options, std::optional<double> slide)
{
    Layout out;
    for (const Unit &u : units)
        out.width += u.width;
    const Path walked = options.reverse && !slide ? path.reversed() : path;
    const double length = walked.length(), room = length - out.width;
    double start = options.offset, gap = 0;
    if (slide) {
        start = options.offset + std::clamp(options.reverse ? 1 - *slide : *slide, 0.0, 1.0) * room;
    } else if (options.align == Align::Centre) {
        start = room / 2 + options.offset;
    } else if (options.align == Align::Right) {
        start = room - options.offset;
    } else if (options.align == Align::Spread && units.size() > 1) {
        gap = std::max(0.0, (length - options.offset - out.width) / double(units.size() - 1));
    }
    double d = start;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Unit &u = units[i];
        if (!u.space) {
            const Path::Sample sample = walked.at(d + u.width / 2);
            double theta = std::atan(sample.tangent.y / sample.tangent.x);
            if (sample.tangent.x < 0)
                theta += std::numbers::pi;
            out.glyphs.push_back({i, sample.at, options.rotate ? -theta * 180 / std::numbers::pi : 0});
        }
        d += u.width + (i + 1 < units.size() ? gap : 0);
    }
    out.overflow = std::max({0.0, d - length, -start});
    return out;
}

std::string glyphText(const Unit &unit, const Placed &placed, bool rotate)
{
    std::string out = "{" + unit.tags + "\\an2\\pos(" + number(placed.at.x) + "," + number(placed.at.y) + ")";
    if (rotate)
        out += "\\frz" + number(placed.rotation);
    return out + "}" + unit.text;
}

int slideSteps(int frames, int every)
{
    if (frames <= 0)
        return 0;
    every = std::max(1, every);
    return (frames + every - 1) / every;
}

}
