// VT3: the Perspective tool. The quad's maths port arch1t3cht's Aegisub
// perspective tool (src/visual_tool_perspective.cpp, feature branch), under
// its licence:
//
// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "hikari/application/visual_perspective.h"

#include "hikari/application/automation_services.h"
#include "hikari/application/visual_script.h"
#include "hikari/application/visual_transform.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace hikari::application::visual {

namespace perspective {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180. / kPi;

struct V2 {
    double x = 0, y = 0;
};
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(double k, V2 a) { return {k * a.x, k * a.y}; }
double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
V2 v2(PointF p) { return {p.x, p.y}; }
PointF pf(V2 p) { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }

struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(double k, V3 a) { return {k * a.x, k * a.y, k * a.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// libass's turns (calc_transform_matrix): \frz with sz = -sin, \frx with
// sx = -sin, \fry with sy = sin, each about the origin.
V3 turnZ(V3 p, double frz)
{
    const double c = std::cos(frz), s = -std::sin(frz);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}
V3 turnX(V3 p, double frx)
{
    const double c = std::cos(frx), s = -std::sin(frx);
    return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
}
V3 turnY(V3 p, double fry)
{
    const double c = std::cos(fry), s = std::sin(fry);
    return {p.x * c - p.z * s, p.y, p.x * s + p.z * c};
}

// Solve2x2: a11 x1 + a12 x2 = b1, a21 x1 + a22 x2 = b2.
void solve2x2(double a11, double a12, double a21, double a22, double b1, double b2, double &x1, double &x2)
{
    if (std::abs(a11) < std::abs(a21)) {
        std::swap(b1, b2);
        std::swap(a11, a21);
        std::swap(a12, a22);
    }
    a21 = a21 / a11;
    a22 = a22 - a21 * a12;
    const double z1 = b1;
    const double z2 = b2 - a21 * z1;
    x2 = z2 / a22;
    x1 = (z1 - a12 * x2) / a11;
}

// The projective map of the unit square onto a quad, and its inverse.
using M3 = std::array<std::array<double, 3>, 3>;
M3 unitToQuad(const Quad &q)
{
    const double x0 = q[0].x, y0 = q[0].y, x1 = q[1].x, y1 = q[1].y;
    const double x2 = q[2].x, y2 = q[2].y, x3 = q[3].x, y3 = q[3].y;
    const double sx = x0 - x1 + x2 - x3, sy = y0 - y1 + y2 - y3;
    const double dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
    const double den = dx1 * dy2 - dx2 * dy1;
    const double g = (sx * dy2 - dx2 * sy) / den;
    const double h = (dx1 * sy - sx * dy1) / den;
    return {{{x1 - x0 + g * x1, x3 - x0 + h * x3, x0}, {y1 - y0 + g * y1, y3 - y0 + h * y3, y0}, {g, h, 1}}};
}
V2 apply(const M3 &m, V2 p)
{
    const double w = m[2][0] * p.x + m[2][1] * p.y + m[2][2];
    return {(m[0][0] * p.x + m[0][1] * p.y + m[0][2]) / w, (m[1][0] * p.x + m[1][1] * p.y + m[1][2]) / w};
}
M3 inverse(const M3 &m)
{
    const double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0],
                 h = m[2][1], i = m[2][2];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    return {{{A / det, -(b * i - c * h) / det, (b * f - c * e) / det},
             {B / det, (a * i - c * g) / det, -(a * f - c * d) / det},
             {C / det, -(a * h - b * g) / det, (a * e - b * d) / det}}};
}

bool finite(double v) { return std::isfinite(v); }

// --------------------------------------------------------------- tags

std::string ascii(u16v s)
{
    std::string out;
    out.reserve(s.size());
    for (const char16_t c : s)
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}

std::optional<double> number(u16v s)
{
    while (!s.empty() && s.front() == u' ')
        s.remove_prefix(1);
    const std::string a = ascii(s);
    if (a.empty())
        return std::nullopt;
    char *end = nullptr;
    const double v = std::strtod(a.c_str(), &end);
    if (end == a.c_str() || !finite(v))
        return std::nullopt;
    return v;
}

bool blank(char16_t c) { return c == u' ' || c == u'\t'; }

// libass's argument ends: the spaces and tabs after it are not its.
u16v trimEnd(u16v s)
{
    while (!s.empty() && blank(s.back()))
        s.remove_suffix(1);
    return s;
}

std::vector<double> arguments(u16v value)
{
    // "(a,b,...)" or "(a,b,...": the numbers in order, split as libass
    // splits them: each without the spaces and tabs around it, an empty one
    // left out (\pos(100,) has one), a backslash taking the rest as one.
    std::vector<double> out;
    if (value.empty() || value.front() != u'(')
        return out;
    value.remove_prefix(1);
    if (const auto close = value.find(u')'); close != u16v::npos)
        value = value.substr(0, close);
    while (true) {
        while (!value.empty() && blank(value.front()))
            value.remove_prefix(1);
        std::size_t comma = value.find_first_of(u",\\");
        if (comma != u16v::npos && value[comma] == u'\\')
            comma = u16v::npos;
        if (const u16v part = trimEnd(value.substr(0, comma)); !part.empty())
            out.push_back(number(part).value_or(std::nan("")));
        if (comma == u16v::npos)
            break;
        value.remove_prefix(comma + 1);
    }
    return out;
}

bool letter(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }

// One tag of a block: its name (one of kNames, or empty for another) and
// its value, with its place in the block (from its backslash).
struct Tag {
    u16v name, value;
    std::size_t from = 0, to = 0;
};

constexpr u16v kNames[] = {u"xbord", u"ybord", u"xshad", u"yshad", u"fscx", u"fscy", u"bord", u"shad",
                           u"move",  u"pos",   u"org",   u"frx",   u"fry",  u"frz",  u"fax",  u"fay",
                           u"an",    u"fr",    u"fs",    u"fn",    u"p",    u"a",    u"t",    u"r",
                           u"q",     u"fsp",   u"b",     u"i",     u"pbo",  u"fsc"};

// The outline and shadow, which the tool scales in the leading blocks.
constexpr u16v kOutlineTags[] = {u"bord", u"xbord", u"ybord", u"shad", u"xshad", u"yshad"};

u16v nameOf(u16v token)
{
    for (const u16v name : kNames) {
        if (!token.starts_with(name))
            continue;
        const u16v rest = token.substr(name.size());
        if (name == u"move" || name == u"pos" || name == u"org" || name == u"t") {
            if (!rest.empty() && rest.front() == u'(')
                return name;
            continue;
        }
        if (name == u"fn" || name == u"r") // a font or Style name follows
            return name;
        if (rest.empty() || !letter(rest.front()))
            return name;
    }
    return {};
}

// The tags of a block's text (between its braces), at its top level: a
// backslash inside \t(...) or \clip(...) does not start one. As libass,
// spaces and tabs may come between the backslash and the name.
std::vector<Tag> tagsOf(u16v block)
{
    std::vector<Tag> out;
    std::size_t i = block.find(u'\\');
    while (i != u16v::npos && i < block.size()) {
        std::size_t j = i + 1;
        int depth = 0;
        while (j < block.size()) {
            const char16_t c = block[j];
            if (c == u'(')
                ++depth;
            else if (c == u')' && depth > 0)
                --depth;
            else if (c == u'\\' && depth == 0)
                break;
            ++j;
        }
        u16v token = block.substr(i + 1, j - i - 1);
        while (!token.empty() && blank(token.front()))
            token.remove_prefix(1);
        const u16v name = nameOf(token);
        out.push_back({name, token.substr(name.size()), i, j});
        i = j;
    }
    return out;
}

// Whether text[i] starts libass's escape of a brace (\{ or \}, shown).
bool braceEscape(u16v text, std::size_t i)
{
    return text[i] == u'\\' && i + 1 < text.size() && (text[i + 1] == u'{' || text[i + 1] == u'}');
}

// The text's blocks: where each opens and closes. As libass, \{ shows a
// brace and a { with no } after it is text.
std::vector<std::pair<std::size_t, std::size_t>> blocksOf(u16v text)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (braceEscape(text, i)) {
            i += 2;
        } else if (text[i] == u'{') {
            const std::size_t close = text.find(u'}', i);
            if (close == u16v::npos)
                break;
            out.emplace_back(i, close);
            i = close + 1;
        } else {
            ++i;
        }
    }
    return out;
}

// A positional tag's numbers as libass reads them (a word is 0), when it
// has as many as the tag takes: \pos and \org 2, \move 4 or 6.
std::optional<std::vector<double>> positional(const Tag &tag)
{
    std::vector<double> a = arguments(tag.value);
    const bool fits = tag.name == u"move" ? (a.size() == 4 || a.size() == 6) : a.size() == 2;
    if (!fits)
        return std::nullopt;
    for (double &v : a)
        if (!finite(v))
            v = 0;
    return a;
}

// What follows a tag's value up to the next tag: an override block's
// comment (libass reads \frx10 note as \frx10), kept when the tag goes.
u16v tailOf(const Tag &tag)
{
    u16v v = tag.value;
    if (!v.empty() && v.front() == u'(') {
        const std::size_t close = v.find(u')');
        return close == u16v::npos ? u16v{} : v.substr(close + 1);
    }
    std::size_t i = 0;
    while (i < v.size() && v[i] == u' ')
        ++i;
    const std::string a = ascii(v.substr(i));
    char *end = nullptr;
    (void)std::strtod(a.c_str(), &end);
    return v.substr(i + static_cast<std::size_t>(end - a.c_str()));
}

std::u16string fmt(double v, int precision)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", precision, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    if (s == "-0")
        s = "0";
    return std::u16string(s.begin(), s.end());
}

int alignmentOfA(int a)
{
    // \a's legacy numbering: 1-3 bottom, +4 top, +8 middle; libass takes
    // VSFilter's \a4 and \a8 as \a5.
    if ((a & 3) == 0)
        a = 5;
    const int h = a & 3;
    if (a & 4)
        return h + 6;
    if (a & 8)
        return h + 3;
    return h;
}

PointF anchorShift(int an)
{
    // The alignment point's place in the box: 0, 0.5 or 1 across and down.
    const float h = an % 3 == 0 ? 1.f : (an % 3 == 2 ? 0.5f : 0.f);
    const float v = an <= 3 ? 1.f : (an <= 6 ? 0.5f : 0.f);
    return {h, v};
}

} // namespace

double screenZ(int scriptHeight, int layoutHeight)
{
    if (scriptHeight <= 0 || layoutHeight <= 0)
        return 312.5;
    return 312.5 * scriptHeight / layoutHeight;
}

Quad textToQuad(const Transform &t, const Box &box, double z)
{
    // TextToPersp: the box sheared about its top, moved to the alignment
    // point, scaled, put at \pos, turned about \org and projected.
    const PointF shift = anchorShift(t.an);
    const double w = box.width(), h = box.layoutHeight(); // its own size, however small
    const V2 corners[4] = {{box.topLeft.x, box.topLeft.y},
                           {box.bottomRight.x, box.topLeft.y},
                           {box.bottomRight.x, box.bottomRight.y},
                           {box.topLeft.x, box.bottomRight.y}};
    Quad out{};
    for (int i = 0; i < 4; ++i) {
        V2 p = corners[i];
        p = {p.x + p.y * t.fax, p.x * t.fay + p.y};
        p = p + V2{-w * shift.x, -h * shift.y};
        p = {p.x * t.fscx / 100., p.y * t.fscy / 100.};
        p = p + v2(t.pos) - v2(t.org);
        V3 q{p.x, p.y, 0};
        q = turnZ(q, t.frz / kDeg);
        q = turnX(q, t.frx / kDeg);
        q = turnY(q, t.fry / kDeg);
        const double k = z / (q.z + z);
        out[i] = pf(V2{q.x * k, q.y * k} + v2(t.org));
    }
    return out;
}

PointF centre(const Quad &q)
{
    // QuadMidpoint: where the diagonals cross.
    const V2 d1 = v2(q[2]) - v2(q[0]), d2 = v2(q[1]) - v2(q[3]), b = v2(q[3]) - v2(q[0]);
    double la1 = 0, la2 = 0;
    solve2x2(d1.x, d2.x, d1.y, d2.y, b.x, b.y, la1, la2);
    return pf(v2(q[0]) + la1 * d1);
}

bool convex(const Quad &q)
{
    // The diagonals cross inside both (arch1t3cht's UpdateDrag check).
    const V2 d1 = v2(q[2]) - v2(q[0]), d2 = v2(q[1]) - v2(q[3]), b = v2(q[3]) - v2(q[0]);
    double la1 = 0, la2 = 0;
    solve2x2(d1.x, d2.x, d1.y, d2.y, b.x, b.y, la1, la2);
    if (!finite(la1) || !finite(la2))
        return false;
    return la1 > 0 && la1 < 1 && -la2 > 0 && -la2 < 1;
}

PointF toUV(const Quad &quad, PointF xy)
{
    return pf(apply(inverse(unitToQuad(quad)), v2(xy)));
}

PointF fromUV(const Quad &quad, PointF uv)
{
    return pf(apply(unitToQuad(quad), v2(uv)));
}

Quad subQuad(const Quad &quad, PointF a, PointF b)
{
    const M3 m = unitToQuad(quad);
    return {pf(apply(m, {a.x, a.y})), pf(apply(m, {b.x, a.y})), pf(apply(m, {b.x, b.y})), pf(apply(m, {a.x, b.y}))};
}

bool planeHolds(const Quad &inner, PointF c1, PointF c2)
{
    if (!finite(c1.x) || !finite(c1.y) || !finite(c2.x) || !finite(c2.y) || !(c1.x < c2.x && c1.y < c2.y))
        return false;
    const PointF a{-c1.x / (c2.x - c1.x), -c1.y / (c2.y - c1.y)};
    const PointF b{(1 - c1.x) / (c2.x - c1.x), (1 - c1.y) / (c2.y - c1.y)};
    // The map's divisor keeps the inner quad's sign over the whole outer
    // rectangle (it is affine, so at its corners), and does not come near
    // 0: past it the outer quad wraps through infinity.
    const M3 m = unitToQuad(inner);
    const auto w = [&m](double u, double v) { return m[2][0] * u + m[2][1] * v + m[2][2]; };
    const double mid = w(0.5, 0.5);
    for (const V2 p : {V2{a.x, a.y}, V2{b.x, a.y}, V2{b.x, b.y}, V2{a.x, b.y}})
        if (!(w(p.x, p.y) / mid > 0.01))
            return false;
    const Quad outer = subQuad(inner, a, b);
    for (const PointF &p : outer)
        if (!finite(p.x) || !finite(p.y))
            return false;
    return convex(outer);
}

std::optional<Transform> quadToTransform(const Quad &quad, const Box &box, OrgMode mode, const Transform &from,
                                         double f)
{
    // InnerToText.
    V2 q0 = v2(quad[0]), q1 = v2(quad[1]), q2 = v2(quad[2]), q3 = v2(quad[3]);

    // A parallelogram projecting to the quad, whatever its place.
    double z1 = 0, z3 = 0;
    const V2 diag = q2 - q0, side2 = q1 - q2, side3 = q3 - q2;
    solve2x2(side2.x, side3.x, side2.y, side3.y, -diag.x, -diag.y, z1, z3);

    const V2 midpoint = v2(centre(quad));
    V2 org = v2(from.org);
    if (mode == OrgMode::Center) {
        org = midpoint;
    } else if (mode == OrgMode::NoFax) {
        // A translation t of q0 after which the quad unprojects to a
        // rectangle: a (x^2 + y^2) - b.t + c = 0; the one nearest the
        // centre's.
        const V2 v1 = q1 - q0, v3 = q3 - q0;
        const double a = (1 - z1) * (1 - z3);
        const V2 b = z1 * v1 + z3 * v3 - z1 * z3 * (v1 + v3);
        const double c = z1 * z3 * dot(v1, v3) + (z1 - 1) * (z3 - 1) * f * f;
        V2 t = q0 - midpoint;
        if (a == 0) {
            if (dot(b, b) != 0)
                t = t + ((c - dot(t, b)) / dot(b, b)) * b;
        } else {
            const V2 circleCentre = (1 / (2 * a)) * b;
            const double sqradius = (dot(b, b) / (4 * a) - c) / a;
            if (sqradius <= 0) {
                // arch1t3cht's tool set \org here and then overwrote it
                // with the centre's; the circle's centre is the nearest.
                t = circleCentre;
            } else {
                const double radius = std::sqrt(sqradius);
                const V2 toT = t - circleCentre;
                const double len = std::sqrt(dot(toT, toT));
                t = len == 0 ? circleCentre + V2{radius, 0} : circleCentre + (radius / len) * toT;
            }
        }
        org = q0 - t;
    }

    q0 = q0 - org;
    q1 = q1 - org;
    q2 = q2 - org;
    q3 = q3 - org;
    std::array<V3, 4> r{V3{q0.x, q0.y, f}, z1 * V3{q1.x, q1.y, f}, (z1 + z3 - 1) * V3{q2.x, q2.y, f},
                        z3 * V3{q3.x, q3.y, f}};

    // The depth of the plane's point that projects to \org.
    double la0 = 0, la1 = 0;
    const V3 side0 = r[1] - r[0], side1 = r[3] - r[0];
    solve2x2(side0.x, side1.x, side0.y, side1.y, -r[0].x, -r[0].y, la0, la1);
    const double orgz = (r[0] + la0 * side0 + la1 * side1).z;
    for (V3 &p : r)
        p = (f / orgz) * p - V3{0, 0, f};

    // The turns that bring the plane to the screen's: \fry, then \frx.
    V3 n = cross(r[1] - r[0], r[3] - r[0]);
    double fry = std::atan(-n.x / n.z);
    if (n.z < 0)
        fry += kPi;
    n = turnY(n, -fry);
    const double frx = std::atan(n.y / n.z);
    for (V3 &p : r)
        p = turnX(turnY(p, -fry), -frx);
    V3 ab = r[1] - r[0];
    double frz = std::atan(-ab.y / ab.x);
    if (ab.x < 0)
        frz += kPi;
    for (V3 &p : r)
        p = turnZ(p, -frz);

    // A parallelogram with a level top: its shear and size.
    ab = r[1] - r[0];
    const V3 ad = r[3] - r[0];
    const double rawfax = ad.x / ad.y;
    const double quadwidth = std::sqrt(ab.x * ab.x + ab.y * ab.y);
    const double quadheight = std::abs(ad.y);
    // The box's own size, however small (a \p4 drawing's is a fraction of
    // a unit): one with no width or height has no scale to recover.
    if (!(box.width() > 0) || !(box.height() > 0))
        return std::nullopt;
    const double scalex = quadwidth / box.width();
    const double scaley = quadheight / box.height();

    Transform t = from;
    t.org = pf(org);
    t.frx = frx * kDeg;
    t.fry = fry * kDeg;
    t.frz = frz * kDeg;
    t.fax = rawfax * scaley / scalex;
    t.fay = 0;
    t.fscx = 100 * scalex;
    t.fscy = 100 * scaley;
    const PointF shift = anchorShift(from.an);
    const V2 start{box.topLeft.x + box.topLeft.y * t.fax, box.topLeft.y};
    t.pos = pf(org + V2{r[0].x, r[0].y} - V2{scalex * start.x, scaley * start.y} +
               V2{quadwidth * shift.x, scaley * box.layoutHeight() * shift.y});
    // The outline and shadow keep their proportion to the text.
    const double kx = from.fscx != 0 ? t.fscx / from.fscx : 1, ky = from.fscy != 0 ? t.fscy / from.fscy : 1;
    t.bord = pf(V2{from.bord.x * kx, from.bord.y * ky});
    t.shad = pf(V2{from.shad.x * kx, from.shad.y * ky});

    for (const double v : {t.frx, t.fry, t.frz, t.fax, t.fscx, t.fscy, double(t.pos.x), double(t.pos.y),
                           double(t.org.x), double(t.org.y), double(t.bord.x), double(t.bord.y), double(t.shad.x),
                           double(t.shad.y)}) {
        if (!finite(v))
            return std::nullopt;
    }
    return t;
}

namespace {

struct Leading {
    std::vector<Tag> tags; // of the blocks before the text
    std::u16string drawing;
    int p = 0;
    double pbo = 0; // the drawing's baseline offset, in its points
    bool more = false; // something shown after the drawing
};

Leading leadingOf(u16v text)
{
    Leading out;
    std::size_t i = 0;
    while (i < text.size() && text[i] == u'{') {
        const std::size_t close = text.find(u'}', i);
        if (close == u16v::npos)
            break;
        for (const Tag &tag : tagsOf(text.substr(i + 1, close - i - 1)))
            out.tags.push_back(tag);
        i = close + 1;
    }
    for (const Tag &tag : out.tags) {
        if (tag.name == u"p")
            out.p = static_cast<int>(number(tag.value).value_or(0));
        else if (tag.name == u"pbo") // not reset by \r
            out.pbo = number(tag.value).value_or(0);
    }
    if (out.p > 0) {
        // libass's drawing runs to the next {. Text or another drawing
        // after it is laid out beside it, which the box does not hold.
        const std::size_t end = text.find(u'{', i);
        out.drawing = std::u16string(text.substr(i, end == u16v::npos ? u16v::npos : end - i));
        if (end != u16v::npos) {
            const u16v rest = text.substr(end);
            std::size_t k = 0;
            for (const auto &[open, close] : blocksOf(rest)) {
                if (open != k)
                    break;
                k = close + 1;
            }
            out.more = k < rest.size();
        }
    }
    return out;
}

} // namespace

// Past this many bytes of its fields a Style is not read: each read takes
// its values, and a Script's Style can be anything.
constexpr std::size_t kMaxStyleBytes = 64 * 1024;

std::size_t styleBytes(const core::StyleValues &s)
{
    std::size_t n = 0;
    for (const std::u8string *f : {&s.name, &s.fontname, &s.fontsize, &s.scaleX, &s.scaleY, &s.spacing, &s.angle,
                                   &s.outlineWidth, &s.shadow, &s.alignment, &s.marginLeft, &s.marginRight,
                                   &s.marginVertical, &s.encoding})
        n += f->size();
    return n;
}

struct StyleCache::Decoded {
    std::vector<core::StyleValues> all;
    std::unordered_set<const core::StyleValues *> oversized; // past kMaxStyleBytes
    // The last Style of each name, as libass looks a Line's and a \r's up
    // (a Style without a name is Default to libass).
    std::unordered_map<std::u8string_view, const core::StyleValues *> last;
};

std::shared_ptr<const StyleCache::Decoded> StyleCache::get(const VisualHost &host)
{
    const EditSession *session = host.session();
    if (!session)
        return std::make_shared<const Decoded>();
    const core::Document *document = &session->document();
    if (m_decoded && m_session == session->serial() && m_revision == session->revision() && m_document == document)
        return m_decoded;
    auto decoded = std::make_shared<Decoded>();
    decoded->all = core::decodeStyles(*document);
    for (const core::StyleValues &s : decoded->all) {
        if (styleBytes(s) > kMaxStyleBytes)
            decoded->oversized.insert(&s);
        decoded->last.insert_or_assign(s.name.empty() ? std::u8string_view(u8"Default") : std::u8string_view(s.name),
                                       &s);
    }
    m_session = session->serial();
    m_revision = session->revision();
    m_document = document;
    m_decoded = std::move(decoded);
    return m_decoded;
}

namespace {

// The context (read without its Styles) with the Styles of a cache.
struct StyleIndex {
    StyleIndex(const transform::Context &context, const StyleCache::Decoded &decoded)
        : ctx(context), last(decoded.last), oversized(decoded.oversized)
    {
    }
    // A Line's Style as libass's ass_lookup_style finds it: without its
    // leading *s, "default" in any case being Default, the last of the
    // name, else the last Default. Null: libass's own Default, which no
    // Style of the Script holds.
    const core::StyleValues *find(std::u8string_view name) const
    {
        while (name.starts_with(u8'*'))
            name.remove_prefix(1);
        const auto isDefault = [](std::u8string_view n) {
            constexpr std::u8string_view d = u8"default";
            if (n.size() != d.size())
                return false;
            for (std::size_t i = 0; i < n.size(); ++i)
                if ((n[i] >= u8'A' && n[i] <= u8'Z' ? n[i] - u8'A' + u8'a' : n[i]) != d[i])
                    return false;
            return true;
        };
        if (isDefault(name))
            name = u8"Default";
        if (const auto it = last.find(name); it != last.end())
            return it->second;
        if (const auto it = last.find(u8"Default"); it != last.end())
            return it->second;
        return nullptr;
    }
    const core::StyleValues &line(std::u8string_view name) const
    {
        const core::StyleValues *s = find(name);
        return s ? *s : builtin();
    }
    bool tooLarge(const core::StyleValues &s) const { return oversized.contains(&s); }
    // libass's own Default (set_default_style): Arial 18 at weight 200,
    // outline 2, shadow 3, margins 20.
    static const core::StyleValues &builtin()
    {
        static const core::StyleValues s = [] {
            core::StyleValues v;
            v.name = u8"Default";
            v.fontname = u8"Arial";
            v.fontsize = u8"18";
            v.scaleX = v.scaleY = u8"100";
            v.spacing = v.angle = u8"0";
            v.outlineWidth = u8"2";
            v.shadow = u8"3";
            v.alignment = u8"2";
            v.marginLeft = v.marginRight = v.marginVertical = u8"20";
            v.complete = true;
            return v;
        }();
        return s;
    }
    const transform::Context &ctx;
    const std::unordered_map<std::u8string_view, const core::StyleValues *> &last;
    const std::unordered_set<const core::StyleValues *> &oversized;
};

// The Style a \r resets to: the one it names (without the spaces after
// it; Default without a Default Style is libass's own), else the Line's
// (libass's reset_render_context with a Style it cannot find).
const core::StyleValues *resetTarget(const StyleIndex &styles, u16v name)
{
    name = trimEnd(name);
    if (name.empty())
        return nullptr;
    const std::u8string wanted = core::toUtf8(u16(name));
    if (const auto it = styles.last.find(wanted); it != styles.last.end())
        return it->second;
    return wanted == u8"Default" ? &StyleIndex::builtin() : nullptr;
}

const core::StyleValues &resetStyle(const StyleIndex &styles, const core::StyleValues &lineStyle, u16v name)
{
    const core::StyleValues *s = resetTarget(styles, name);
    return s ? *s : lineStyle;
}

// Where the leading tags that count start: after the last \r, which resets
// the ones before it (0 without one).
std::size_t afterLastReset(const std::vector<Tag> &tags)
{
    std::size_t from = tags.size();
    while (from > 0 && tags[from - 1].name != u"r")
        --from;
    return from;
}

// Whether a \t animates what the box or its transform is read from
// (libass evaluates it at the frame; one box does not hold its frames).
bool animatesTheQuad(const Tag &tag)
{
    // libass runs every tag in a \t: an animatable one with the time, any
    // other at once (a \pos, an \an, a \fn, a \r ...). Only colours,
    // alphas, blurs and clips leave the box where the quad has it.
    static constexpr u16v kStill[] = {u"1c", u"2c",   u"3c", u"4c", u"1a", u"2a",    u"3a",
                                      u"4a", u"alpha", u"blur", u"be", u"clip", u"iclip", u"c"};
    if (tag.name != u"t")
        return false;
    const std::size_t first = tag.value.find(u'\\');
    if (first == u16v::npos)
        return false;
    for (const Tag &inner : tagsOf(tag.value.substr(first))) {
        // Known names are the quad's own; the still ones are not among them.
        const bool still = inner.name.empty() && std::any_of(std::begin(kStill), std::end(kStill), [&inner](u16v n) {
                               return inner.value.starts_with(n);
                           });
        if (!still)
            return true;
    }
    return false;
}

// Whether a \t anywhere in the text animates the box (see animatesTheQuad).
bool animatesTheBox(u16v text)
{
    for (const auto &[open, close] : blocksOf(text))
        for (const Tag &tag : tagsOf(text.substr(open + 1, close - open - 1)))
            if (animatesTheQuad(tag))
                return true;
    return false;
}

// What one read may do on the UI thread: the Line and every Line it
// stacks on draw from it, their text parsed, rows and characters measured
// through the font and the code of drawings read point by point. libass
// shows any number of rows and points, but a box of more than these is no
// sign the tool places, and a hostile script (a comment of \N, a block of
// megabytes of tags, a drawing of megabytes, or many Lines shown at once)
// gets no quad rather than unbounded work.
constexpr std::size_t kMaxParsed = 512 * 1024; // bytes of Line text
// The bytes of the text the tool reads: in translation mode the translation.
std::size_t renderedSize(const core::LineRecord &line)
{
    return (line.translation.empty() ? line.text : line.translation).size();
}
struct MeasureBudget {
    std::size_t rows = 64, shown = 4096, drawn = 256 * 1024, parsed = kMaxParsed;
    // Whether `line`'s text may be parsed, charging it.
    bool parse(const core::LineRecord &line)
    {
        const std::size_t size = renderedSize(line);
        if (size > parsed)
            return false;
        parsed -= size;
        return true;
    }
};

// libass's ass_strtod at code[i]: blanks, a sign, digits with one point,
// an exponent; false, and `i` kept, when no digit is read.
bool drawingNumber(u16v code, std::size_t &i, double &out)
{
    std::size_t k = i;
    while (k < code.size() && (code[k] == u' ' || (code[k] >= u'\t' && code[k] <= u'\r')))
        ++k;
    const std::size_t numberStart = k;
    if (k < code.size() && (code[k] == u'-' || code[k] == u'+'))
        ++k;
    bool digits = false, point = false;
    for (; k < code.size(); ++k) {
        if (code[k] >= u'0' && code[k] <= u'9')
            digits = true;
        else if (code[k] == u'.' && !point)
            point = true;
        else
            break;
    }
    if (!digits)
        return false;
    if (k < code.size() && (code[k] == u'e' || code[k] == u'E')) {
        std::size_t e = k + 1;
        if (e < code.size() && (code[e] == u'-' || code[e] == u'+'))
            ++e;
        if (e < code.size() && code[e] >= u'0' && code[e] <= u'9') {
            while (e < code.size() && code[e] >= u'0' && code[e] <= u'9')
                ++e;
            k = e;
        }
    }
    // ASCII by now; from_chars takes no '+' and no locale's point.
    std::string ascii;
    for (std::size_t j = numberStart + (code[numberStart] == u'+' ? 1 : 0); j < k; ++j)
        ascii += static_cast<char>(code[j]);
    out = 0;
    std::from_chars(ascii.data(), ascii.data() + ascii.size(), out);
    i = k;
    return true;
}

// The points libass draws a drawing through (ass_drawing.c
// drawing_tokenize, whose cbox they make): nothing before the first m with
// a point, all of it rejected by an n before any m, a b's points in threes,
// an s's three then a p's only once there are three. Nullopt: no ink.
std::optional<std::array<double, 4>> drawingBounds(u16v code)
{
    std::optional<std::array<double, 4>> box;
    const auto add = [&box](double x, double y) {
        if (!box)
            box = std::array<double, 4>{x, y, x, y};
        (*box)[0] = std::min((*box)[0], x);
        (*box)[1] = std::min((*box)[1], y);
        (*box)[2] = std::max((*box)[2], x);
        (*box)[3] = std::max((*box)[3], y);
    };
    const auto point = [&code](std::size_t &i, double &x, double &y) {
        std::size_t k = i;
        if (!drawingNumber(code, k, x) || !drawingNumber(code, k, y))
            return false;
        i = k;
        return true;
    };
    // Points in batches of `batch` until one cannot be read; a partial
    // batch is read but not drawn.
    std::size_t points = 0;
    const auto many = [&](std::size_t &i, std::size_t batch) {
        double buf[3][2];
        std::size_t n = 0;
        double x, y;
        while (i < code.size() && point(i, x, y)) {
            buf[n][0] = x;
            buf[n][1] = y;
            if (++n != batch)
                continue;
            for (std::size_t j = 0; j < n; ++j)
                add(buf[j][0], buf[j][1]);
            points += n;
            n = 0;
        }
    };
    bool root = false, mSeen = false;
    for (std::size_t i = 0; i < code.size();) {
        const char16_t cmd = code[i++];
        double x, y;
        switch (cmd) {
        case u'm':
        case u'n':
            if (cmd == u'm')
                mSeen = true;
            if (!root) {
                if (!point(i, x, y))
                    continue;
                if (!mSeen)
                    return std::nullopt;
                add(x, y);
                ++points;
                root = true;
            }
            many(i, 1);
            break;
        case u'l':
            if (root)
                many(i, 1);
            break;
        case u'b':
            if (root)
                many(i, 3);
            break;
        case u's':
            if (!root)
                break;
            {
                double xs[3], ys[3];
                std::size_t k = i;
                bool three = true;
                for (int j = 0; j < 3 && three; ++j)
                    three = point(k, xs[j], ys[j]);
                if (!three) {
                    // libass reads what it can of the three and draws none.
                    for (int j = 0; j < 3 && point(i, xs[0], ys[0]); ++j) {
                    }
                    break;
                }
                i = k;
                for (int j = 0; j < 3; ++j)
                    add(xs[j], ys[j]);
                points += 3;
            }
            [[fallthrough]];
        case u'p':
            if (points >= 3)
                many(i, 1);
            break;
        default:
            break; // c adds back points already drawn; others are skipped
        }
    }
    return root ? box : std::nullopt;
}

// `edited`: the Line the tool writes, whose outline and shadow are scaled
// in its leading blocks only, so a later change of them is not taken.
std::optional<Box> measureBox(const VisualHost &host, const StyleIndex &styles, const core::LineRecord &line,
                              u16v text, const core::StyleValues &style, const Leading &lead, bool edited,
                              MeasureBudget &budget)
{
    if (animatesTheBox(text))
        return std::nullopt;
    if (lead.p > 0) {
        if (lead.more)
            return std::nullopt;
        // A drawing's points at its \p scale.
        if (lead.drawing.size() > budget.drawn)
            return std::nullopt;
        budget.drawn -= lead.drawing.size();
        const double k = std::ldexp(1., 1 - lead.p);
        const auto bounds = drawingBounds(lead.drawing);
        if (!bounds)
            return std::nullopt;
        const double minx = (*bounds)[0] * k, miny = (*bounds)[1] * k, maxx = (*bounds)[2] * k,
                     maxy = (*bounds)[3] * k;
        // libass sets the drawing's ascent to its height less \pbo and its
        // descent to \pbo; the line holds neither below 0, so a \pbo past
        // the height drops the ink and one outside 0..height grows the line.
        const double height = maxy - miny, pbo = lead.pbo * k;
        const double dy = std::max(0., pbo - height);
        Box box{{static_cast<float>(minx), static_cast<float>(miny + dy)},
                {static_cast<float>(maxx), static_cast<float>(maxy + dy)}};
        if (pbo < 0 || pbo > height)
            box.lineHeight = std::max(height - pbo, 0.) + std::max(pbo, 0.);
        return box;
    }
    TextMeasurePort *measure = host.textMeasure();
    if (!measure)
        return std::nullopt;
    // The Style at a scale of 100 with the leading \fs, \fn, \fsp, \b and
    // \i, as libass applies them: a signed \fs is relative; one at 0 or
    // below, and any of them without a value it takes, go back to the
    // current Style's (whose spacing is not below 0). \fscx is kept for
    // the wrap.
    const std::size_t from = afterLastReset(lead.tags);
    // On libass's own Default (the Line's, or a \rDefault's without a
    // Default Style) the weight is 200, which only a \b the measure takes
    // replaces.
    bool builtin = !styles.find(line.style);
    if (from > 0)
        if (const core::StyleValues *reset = resetTarget(styles, lead.tags[from - 1].value))
            builtin = reset == &StyleIndex::builtin();
    const core::StyleValues &current = from > 0 ? resetStyle(styles, style, lead.tags[from - 1].value) : style;
    const auto styleSpacing = [&current] {
        return core::toUtf8(fmt(std::max(0.0, transform::styleNumber(current.spacing, 0)), 4));
    };
    core::StyleValues s = current;
    s.spacing = styleSpacing();
    double size = transform::styleNumber(current.fontsize, 0);
    const double styleScaleX = std::max(0.0, transform::styleNumber(current.scaleX, 100));
    double scaleX = styleScaleX;
    for (std::size_t k = from; k < lead.tags.size(); ++k) {
        const Tag &tag = lead.tags[k];
        const auto n = number(tag.value);
        if (tag.name == u"fsp") {
            s.spacing = n ? core::toUtf8(fmt(*n, 4)) : styleSpacing();
        } else if (tag.name == u"b") {
            // libass takes 0 or 400 as normal, 1 or 700 as bold and another
            // weight from 100 as that weight, which the measure cannot.
            const int w = n ? static_cast<int>(*n) : -1;
            if (w == 0 || w == 1 || w == 400 || w == 700) {
                s.bold = w == 1 || w == 700;
                builtin = false;
            } else if (w >= 100) {
                return std::nullopt;
            } else {
                s.bold = current.bold;
            }
        } else if (tag.name == u"i") {
            const int v = n ? static_cast<int>(*n) : -1;
            s.italic = v == 0 || v == 1 ? v == 1 : current.italic;
        } else if (tag.name == u"fscx") {
            scaleX = n ? std::max(0.0, *n) : styleScaleX;
        } else if (tag.name == u"fsc") {
            scaleX = styleScaleX;
        } else if (tag.name == u"fs") {
            // Relative only with the sign first: libass reads \fs +10 as
            // 10, its number past the space.
            u16v v = tag.value;
            const bool relative = !v.empty() && (v.front() == u'+' || v.front() == u'-');
            while (!v.empty() && v.front() == u' ')
                v.remove_prefix(1);
            double val = number(v).value_or(0);
            if (relative)
                val = size * (1 + val / 10);
            size = val > 0 ? val : transform::styleNumber(current.fontsize, 0);
        } else if (tag.name == u"fn") {
            // As libass: without the spaces around it; none, or 0, is the
            // current Style's.
            u16v name = trimEnd(tag.value);
            while (!name.empty() && blank(name.front()))
                name.remove_prefix(1);
            s.fontname = name.empty() || name == u"0" ? current.fontname : core::toUtf8(u16(name));
        }
    }
    // A vertical font (@name) lays the text out down, which the measure
    // does not; a font the renderer is given (an attachment, an external
    // font) the measure cannot see, and would measure another in its place.
    if (builtin || s.fontname.starts_with(u8"@") || host.fontLeased(std::string(s.fontname.begin(), s.fontname.end())))
        return std::nullopt;
    s.fontsize = core::toUtf8(fmt(size, 4));
    s.scaleX = u8"100";
    s.scaleY = u8"100";
    std::vector<std::string> fields;
    for (const auto &f : core::legacy::styleRawFields(s))
        fields.emplace_back(f.begin(), f.end());
    // The shown text, line by line, in one pass as libass reads it: \N
    // breaks a line, \n too under wrap style 2 (the script's WrapStyle, or
    // the last \q before it) and is a space otherwise, \h a hard space,
    // \{ and \} braces, a tab a space. A drawing within the text, or a tag
    // changing the glyphs after some (their size, font, scale, spacing,
    // weight, slant or angles, or a \r), is not measured: one Style's run
    // does not hold it.
    const transform::Context &ctx = styles.ctx;
    const auto wrapOf = [&ctx] {
        const auto v = ctx.document ? ctx.document->scriptInfo(u8"WrapStyle") : std::nullopt;
        const int n = v ? std::atoi(std::string(v->begin(), v->end()).c_str()) : 0;
        return n >= 0 && n <= 3 ? n : 0;
    };
    int wrap = wrapOf();
    if (budget.rows == 0)
        return std::nullopt;
    std::size_t shown = 0;
    std::vector<u16> lines(1);
    const std::size_t lastClose = text.rfind(u'}'); // a { before it opens a block
    static constexpr u16v kGlyphTags[] = {u"fs",  u"fn",  u"fscx", u"fscy", u"fsc", u"fsp", u"b",  u"i",
                                          u"frx", u"fry", u"frz",  u"fr",   u"fax", u"fay", u"r"};
    bool started = false; // something shown, or a line broken
    for (std::size_t i = 0; i < text.size();) {
        const char16_t ch = text[i];
        if (ch == u'{' && lastClose != u16v::npos && i < lastClose) {
            const std::size_t close = text.find(u'}', i);
            for (const Tag &tag : tagsOf(text.substr(i + 1, close - i - 1))) {
                if (tag.name == u"q") {
                    const auto n = number(tag.value);
                    wrap = n && *n >= 0 && *n <= 3 ? static_cast<int>(*n) : wrapOf();
                } else if (tag.name == u"p" && number(tag.value).value_or(0) > 0) {
                    return std::nullopt;
                } else if (started && std::find(std::begin(kGlyphTags), std::end(kGlyphTags), tag.name) !=
                                          std::end(kGlyphTags)) {
                    return std::nullopt;
                } else if (started && edited &&
                           std::find(std::begin(kOutlineTags), std::end(kOutlineTags), tag.name) !=
                               std::end(kOutlineTags)) {
                    return std::nullopt;
                }
            }
            i = close + 1;
            continue;
        }
        if (ch == u'\\' && i + 1 < text.size()) {
            const char16_t next = text[i + 1];
            if (next == u'N' || (next == u'n' && wrap == 2)) {
                if (lines.size() == budget.rows)
                    return std::nullopt;
                lines.emplace_back();
                started = true;
                i += 2;
                continue;
            }
            if (next == u'n' || next == u'h' || next == u'{' || next == u'}') {
                lines.back().push_back(next == u'n' ? u' ' : next == u'h' ? u'\u00A0' : next);
                started = true;
                i += 2;
                if (++shown > budget.shown)
                    return std::nullopt;
                continue;
            }
        }
        lines.back().push_back(ch == u'\t' ? u' ' : ch);
        started = true;
        ++i;
        if (++shown > budget.shown)
            return std::nullopt;
    }
    budget.rows -= lines.size();
    budget.shown -= shown;
    // libass drops the spaces at either end of a row (trim_whitespace: a
    // tab and a soft \n are spaces by then, \h is not one), so a row of
    // them shows nothing and a Line of them no rectangle.
    for (u16 &piece : lines) {
        const std::size_t first = piece.find_first_not_of(u' ');
        piece.erase(0, first == u16::npos ? piece.size() : first);
        piece.erase(piece.find_last_not_of(u' ') + 1);
    }
    // The measure spaces each UTF-16 unit as legacy's text_extents does,
    // libass each character: a pair of surrogates is spaced twice.
    const bool spaced = transform::styleNumber(s.spacing, 0) != 0;
    u16 all;
    for (const u16 &piece : lines) {
        if (spaced && std::any_of(piece.begin(), piece.end(), [](char16_t c) { return c >= 0xD800 && c < 0xE000; }))
            return std::nullopt;
        all += piece;
    }
    // A character the font lacks libass may take from a font only it is
    // given, where the measure takes one of the system's.
    if (host.fallsBackToLeased(std::string(s.fontname.begin(), s.fontname.end()), s.bold, s.italic, all))
        return std::nullopt;
    double width = 0, height = 0;
    for (const u16 &piece : lines) {
        const std::u8string utf8 = core::toUtf8(piece.empty() ? u16(u" ") : piece);
        const auto e = measure->measure(fields, std::string(utf8.begin(), utf8.end()));
        if (!e)
            return std::nullopt;
        if (!piece.empty())
            width = std::max(width, e->width);
        height += e->height;
    }
    // Wider than the room between its margins, libass wraps it (but under
    // wrap style 2): rows this box does not hold. The margins are the
    // Line's, else its own Style's (a \r does not move the Line).
    if (wrap != 2) {
        const auto margin = [](std::int64_t own, const std::u8string &styleMargin) {
            return own != 0 ? static_cast<double>(own) : transform::styleNumber(styleMargin, 0);
        };
        const double room = ctx.width - margin(line.marginLeft.value, style.marginLeft) -
                            margin(line.marginRight.value, style.marginRight);
        if (width * scaleX / 100 > room)
            return std::nullopt;
    }
    return Box{{0, 0}, {static_cast<float>(width), static_cast<float>(height)}};
}

// Whether one of `tags` comes after the leading blocks: an outline or
// shadow the tool does not scale, a border libass takes the largest of.
template <std::size_t N>
bool afterText(u16v text, const u16v (&tags)[N])
{
    std::size_t k = 0;
    bool leading = true;
    for (const auto &[open, close] : blocksOf(text)) {
        leading = leading && open == k;
        k = close + 1;
        if (leading)
            continue;
        for (const Tag &tag : tagsOf(text.substr(open + 1, close - open - 1)))
            if (std::find(std::begin(tags), std::end(tags), tag.name) != std::end(tags))
                return true;
    }
    return false;
}

bool outlineAfterText(u16v text)
{
    return afterText(text, kOutlineTags);
}

// libass's scrolling effects: Banner;, and Scroll up; or Scroll down; with
// their three values (fewer and libass ignores it). The event moves with the
// time and takes no part in collisions.
bool scrolls(const core::LineRecord &line)
{
    const std::u8string &e = line.effect;
    if (e.starts_with(u8"Banner;"))
        return true;
    if (!e.starts_with(u8"Scroll up;") && !e.starts_with(u8"Scroll down;"))
        return false;
    return std::count(e.begin(), e.end(), u8';') >= 3;
}

LineState readTagsIn(const VisualHost &host, const StyleIndex &styles, const core::LineRecord &line);

// Whether `line`'s Style, or the one its leading \r resets to, is past the
// bytes a read takes.
bool stylesTooLarge(const StyleIndex &styles, const core::LineRecord &line)
{
    const core::StyleValues &style = styles.line(line.style);
    if (styles.tooLarge(style))
        return true;
    // Held while the tags (views into it) are read (#382).
    const u16 text = transform::lineText(line);
    const Leading lead = leadingOf(text);
    const std::size_t from = afterLastReset(lead.tags);
    return from > 0 && styles.tooLarge(resetStyle(styles, style, lead.tags[from - 1].value));
}

// How far an unpositioned Line is moved clear of the Lines before it shown
// with it: legacy's GetDialogueAdditionalPosition (Visuals.cpp:143-215),
// the rule the other tools place such a Line by, over this tool's own
// reading, which takes linear time (legacy's tag parse does not), and with
// libass's collisions (ass_render.c finish_frame, fix_collisions): only
// Lines of its Layer that collide (no \pos, \move, \org, \t or scrolling
// effect) count, each a rectangle of its box at its leading scale with its
// outline (not its shadow); one with nothing shown is no obstacle. Nullopt
// when one in its band cannot be measured: where it is placed is not known.
std::optional<double> stackOffset(const VisualHost &host, const StyleIndex &styles, const core::LineRecord &line,
                                  const Transform &t, const Box &box, MeasureBudget &budget)
{
    const transform::Context &ctx = styles.ctx;
    const std::int64_t time = host.videoTimeMs();
    const auto shown = [time](const core::LineRecord &l) {
        return time >= transform::startMs(l) && time < transform::endMs(l);
    };
    if (!ctx.document || !shown(line))
        return 0.0;
    // Its rectangle at its default position.
    const auto rect = [&ctx](const core::LineRecord &l, const core::StyleValues &st, const Transform &lt,
                             const Box &b) {
        const double w = b.width() * lt.fscx / 100 + 2 * lt.bord.x;
        const double h = b.layoutHeight() * lt.fscy / 100 + 2 * lt.bord.y;
        const int a = lt.an;
        const PointF p = transform::defaultPosition(l, st, a, ctx.width, ctx.height);
        double x = p.x, y = p.y;
        if (a % 3 == 2)
            x -= w / 2;
        else if (a % 3 == 0)
            x -= w;
        if (a < 4)
            y -= h;
        else if (a < 7)
            y -= h / 2;
        return std::array<double, 4>{x, y, x + w, y + h};
    };
    const auto mine = rect(line, styles.line(line.style), t, box);
    if (mine[2] <= mine[0] || mine[3] <= mine[1])
        return 0.0; // no area: libass does not move it
    // libass's fit_rect on a track without placement history: each earlier
    // Line that collides is placed in its turn clear of those placed before
    // it (`used`, by top), and this one last; a bottom-aligned Line moves
    // up, another down.
    std::vector<std::array<double, 4>> used;
    const auto fit = [&used](const std::array<double, 4> &r, int an) {
        double shift = 0;
        const auto meets = [&r, &shift](const std::array<double, 4> &f) {
            return !(r[3] + shift <= f[1] || r[1] + shift >= f[3] || r[2] <= f[0] || r[0] >= f[2]);
        };
        if (an >= 4) {
            for (const auto &f : used)
                if (meets(f))
                    shift = f[3] - r[1];
        } else {
            for (auto f = used.rbegin(); f != used.rend(); ++f)
                if (meets(*f))
                    shift = (*f)[1] - r[3];
        }
        const std::array<double, 4> placed{r[0], r[1] + shift, r[2], r[3] + shift};
        used.insert(std::upper_bound(used.begin(), used.end(), placed[1],
                                     [](double y, const std::array<double, 4> &f) { return y < f[1]; }),
                    placed);
        return shift;
    };
    // Past this many obstacles the stack is off the frame, but where is not
    // known (the work of reading them is the budget's).
    constexpr int kMaxMeasured = 256, kMaxCandidates = 4096;
    int measured = 0, candidates = 0;
    for (const core::LineRecord *dial : ctx.document->lines()) {
        if ((!ctx.ignoreFiltered && dial->visibility == core::LineVisibility::Hidden) || dial->unparsed)
            continue;
        if (dial->id == line.id)
            break;
        if (dial->comment || !shown(*dial) || dial->layer.value != line.layer.value)
            continue; // libass renders no Comment
        // Every Line shown with it is looked at, however little it shows.
        if (++candidates > kMaxCandidates || !budget.parse(*dial) || stylesTooLarge(styles, *dial))
            return std::nullopt;
        const LineState other = readTagsIn(host, styles, *dial);
        if (!other.stacks)
            continue;
        const u16 text = transform::lineText(*dial);
        const core::StyleValues &style = styles.line(dial->style);
        // libass's rectangle takes the largest border of its glyphs, which
        // one box at one border does not hold.
        static constexpr u16v kBorderTags[] = {u"bord", u"xbord", u"ybord"};
        const auto otherBox = measureBox(host, styles, *dial, text, style, leadingOf(text), false, budget);
        if (!otherBox || afterText(text, kBorderTags))
            return std::nullopt;
        if (otherBox->width() <= 0)
            continue; // nothing shown
        const auto r = rect(*dial, style, other.transform, *otherBox);
        if (r[2] <= r[0] || r[3] <= r[1])
            continue; // no area: libass does not move others for it
        if (++measured > kMaxMeasured)
            return std::nullopt;
        fit(r, other.transform.an);
    }
    return fit(mine, t.an);
}

LineState readTagsIn(const VisualHost &host, const StyleIndex &styles, const core::LineRecord &line)
{
    const transform::Context &ctx = styles.ctx;
    const u16 text = transform::lineText(line);
    const core::StyleValues &style = styles.line(line.style);
    LineState out;
    Transform &t = out.transform;
    t.an = static_cast<int>(transform::styleNumber(style.alignment, 2));
    if (t.an < 1 || t.an > 9)
        t.an = 2;
    // A Style's values (the Line's, or one a \r resets to); libass holds
    // its scales, outline and shadow at 0 and above.
    const auto styleValues = [&t](const core::StyleValues &st) {
        t.frx = t.fry = t.fax = t.fay = 0;
        t.frz = transform::styleNumber(st.angle, 0);
        t.fscx = std::max(0.0, transform::styleNumber(st.scaleX, 100));
        t.fscy = std::max(0.0, transform::styleNumber(st.scaleY, 100));
        const float bord = static_cast<float>(std::max(0.0, transform::styleNumber(st.outlineWidth, 0)));
        const float shad = static_cast<float>(std::max(0.0, transform::styleNumber(st.shadow, 0)));
        t.bord = {bord, bord};
        t.shad = {shad, shad};
    };
    styleValues(style);

    // The first \pos or \move, \org and \an or \a anywhere, as libass
    // takes them: one with the wrong number of values is skipped, the first
    // \an or \a decides even when out of range (the Style's then).
    std::optional<V2> pos, org;
    bool haveAn = false, timed = false;
    for (const auto &[open, close] : blocksOf(text)) {
        for (const Tag &tag : tagsOf(u16v(text).substr(open + 1, close - open - 1))) {
            // libass takes any tag from t (\t, with its times or not) as a
            // \t, which keeps the Line out of collisions.
            timed = timed || tag.name == u"t" || (tag.name.empty() && tag.value.starts_with(u't'));
            if ((tag.name == u"pos" || tag.name == u"move") && !pos) {
                const auto a = positional(tag);
                if (!a)
                    continue;
                if (tag.name == u"pos") {
                    pos = V2{(*a)[0], (*a)[1]};
                    continue;
                }
                out.hasMove = true;
                out.move = {(*a)[0], (*a)[1], (*a)[2], (*a)[3]};
                if (a->size() == 6)
                    out.moveTimes = u"," + fmt(std::trunc((*a)[4]), 0) + u"," + fmt(std::trunc((*a)[5]), 0);
                // The position at the video's time (libass: linear between
                // t1 and t2, the Line's span without them or when neither
                // is above 0).
                double t1 = 0, t2 = 0;
                if (a->size() == 6) {
                    t1 = std::trunc((*a)[4]);
                    t2 = std::trunc((*a)[5]);
                }
                if (t1 > t2)
                    std::swap(t1, t2);
                if (t1 <= 0 && t2 <= 0) {
                    t1 = 0;
                    t2 = static_cast<double>(transform::endMs(line) - transform::startMs(line));
                }
                const double now = static_cast<double>(host.videoTimeMs()) - transform::startMs(line);
                const double k = now <= t1 ? 0 : (now >= t2 ? 1 : (now - t1) / (t2 - t1));
                pos = V2{(*a)[0] + ((*a)[2] - (*a)[0]) * k, (*a)[1] + ((*a)[3] - (*a)[1]) * k};
            } else if (tag.name == u"org" && !org) {
                if (const auto a = positional(tag))
                    org = V2{(*a)[0], (*a)[1]};
            } else if ((tag.name == u"an" || tag.name == u"a") && !haveAn) {
                haveAn = true;
                const int n = static_cast<int>(number(tag.value).value_or(0));
                if (tag.name == u"an" ? (n >= 1 && n <= 9) : (n >= 1 && n <= 11))
                    t.an = tag.name == u"an" ? n : alignmentOfA(n);
            }
        }
    }

    // The leading blocks' values, the last of each; the last \r resets the
    // ones before it.
    const Leading lead = leadingOf(text);
    const std::size_t from = afterLastReset(lead.tags);
    const core::StyleValues &current = from > 0 ? resetStyle(styles, style, lead.tags[from - 1].value) : style;
    if (from > 0)
        styleValues(current);
    for (std::size_t k = from; k < lead.tags.size(); ++k) {
        const Tag &tag = lead.tags[k];
        if (tag.name == u"fsc") { // both scales back to the current Style's
            t.fscx = std::max(0.0, transform::styleNumber(current.scaleX, 100));
            t.fscy = std::max(0.0, transform::styleNumber(current.scaleY, 100));
            continue;
        }
        auto v = number(tag.value);
        if (!v) {
            // Without a value libass resets it: the angles X and Y and the
            // shears to 0, the rest to the current Style's.
            if (tag.name == u"frx" || tag.name == u"fry" || tag.name == u"fax" || tag.name == u"fay")
                v = 0.0;
            else if (tag.name == u"frz" || tag.name == u"fr")
                v = transform::styleNumber(current.angle, 0);
            else if (tag.name == u"fscx")
                v = transform::styleNumber(current.scaleX, 100);
            else if (tag.name == u"fscy")
                v = transform::styleNumber(current.scaleY, 100);
            else if (tag.name == u"bord" || tag.name == u"xbord" || tag.name == u"ybord")
                v = transform::styleNumber(current.outlineWidth, 0);
            else if (tag.name == u"shad" || tag.name == u"xshad" || tag.name == u"yshad")
                v = transform::styleNumber(current.shadow, 0);
            else
                continue;
        }
        // libass holds the scales, the outlines and \shad at 0 and above
        // (not \xshad or \yshad).
        if (tag.name == u"fscx" || tag.name == u"fscy" || tag.name == u"bord" || tag.name == u"xbord" ||
            tag.name == u"ybord" || tag.name == u"shad")
            v = std::max(0.0, *v);
        if (tag.name == u"frx")
            t.frx = *v;
        else if (tag.name == u"fry")
            t.fry = *v;
        else if (tag.name == u"frz" || tag.name == u"fr")
            t.frz = *v;
        else if (tag.name == u"fax")
            t.fax = *v;
        else if (tag.name == u"fay")
            t.fay = *v;
        else if (tag.name == u"fscx")
            t.fscx = *v;
        else if (tag.name == u"fscy")
            t.fscy = *v;
        else if (tag.name == u"bord")
            t.bord = {static_cast<float>(*v), static_cast<float>(*v)};
        else if (tag.name == u"xbord")
            t.bord.x = static_cast<float>(*v);
        else if (tag.name == u"ybord")
            t.bord.y = static_cast<float>(*v);
        else if (tag.name == u"shad")
            t.shad = {static_cast<float>(*v), static_cast<float>(*v)};
        else if (tag.name == u"xshad")
            t.shad.x = static_cast<float>(*v);
        else if (tag.name == u"yshad")
            t.shad.y = static_cast<float>(*v);
    }

    if (!pos) {
        pos = v2(transform::defaultPosition(line, style, t.an, ctx.width, ctx.height));
        out.stacks = !org && !timed && !scrolls(line); // readLine stacks it, knowing its box
    }
    t.pos = pf(*pos);
    t.org = org ? pf(*org) : t.pos;
    out.ownOrg = org.has_value();
    return out;
}

} // namespace

LineState readTags(const VisualHost &host, const core::LineRecord &line)
{
    StyleCache cache;
    const auto decoded = cache.get(host);
    const transform::Context ctx = transform::context(host, false);
    return readTagsIn(host, StyleIndex(ctx, *decoded), line);
}

std::optional<LineState> readLine(const VisualHost &host, const core::LineRecord &line)
{
    StyleCache cache;
    return readLine(host, line, cache);
}

std::optional<LineState> readLine(const VisualHost &host, const core::LineRecord &line, StyleCache &cache)
{
    // A scrolling Line moves with the time, which the quad does not.
    MeasureBudget budget;
    if (scrolls(line) || !budget.parse(line))
        return std::nullopt;
    const auto decoded = cache.get(host);
    const transform::Context ctx = transform::context(host, false);
    const StyleIndex styles(ctx, *decoded);
    if (stylesTooLarge(styles, line))
        return std::nullopt;
    LineState out = readTagsIn(host, styles, line);
    const u16 text = transform::lineText(line);
    const auto box =
        measureBox(host, styles, line, text, styles.line(line.style), leadingOf(text), true, budget);
    if (!box)
        return std::nullopt;
    out.box = *box;
    if (out.stacks) {
        const auto offset = stackOffset(host, styles, line, out.transform, *box, budget);
        if (!offset)
            return std::nullopt;
        out.transform.pos.y += static_cast<float>(*offset);
        out.transform.org = out.transform.pos;
    }
    return out;
}

std::u16string writeTransform(std::u16string_view text, const Transform &t, const core::StyleValues &style,
                              std::optional<PointF> movedFrom, bool explicitOrg)
{
    u16 out(text);
    if (out.empty() || out.front() != u'{' || out.find(u'}') == u16::npos)
        out.insert(0, u"{}");

    // The Line's first \pos or \move decides, as readTags reads it: a \move
    // is moved, a \pos (or none) is written again.
    bool moved = false, ownOrg = false;
    for (const auto &[open, close] : blocksOf(out))
        for (const Tag &tag : tagsOf(u16v(out).substr(open + 1, close - open - 1)))
            ownOrg = ownOrg || (tag.name == u"org" && positional(tag));
    for (const auto &[open, close] : blocksOf(out)) {
        bool decided = false;
        for (const Tag &tag : tagsOf(u16v(out).substr(open + 1, close - open - 1))) {
            if (tag.name != u"pos" && tag.name != u"move")
                continue;
            const auto a = positional(tag);
            if (!a)
                continue;
            decided = true;
            if (tag.name == u"move" && movedFrom) {
                const double dx = t.pos.x - movedFrom->x, dy = t.pos.y - movedFrom->y;
                u16 move = u"\\move(" + fmt((*a)[0] + dx, 2) + u"," + fmt((*a)[1] + dy, 2) + u"," +
                           fmt((*a)[2] + dx, 2) + u"," + fmt((*a)[3] + dy, 2);
                if (a->size() == 6)
                    move += u"," + fmt(std::trunc((*a)[4]), 0) + u"," + fmt(std::trunc((*a)[5]), 0);
                move += u")";
                move += tailOf(tag);
                out.replace(open + 1 + tag.from, tag.to - tag.from, move);
                moved = true;
            }
            break;
        }
        if (decided)
            break;
    }

    // The leading blocks (those before the text, which readTags reads).
    std::vector<std::pair<std::size_t, std::size_t>> leading;
    for (std::size_t i = 0; i < out.size() && out[i] == u'{';) {
        const std::size_t close = out.find(u'}', i);
        if (close == u16::npos)
            break;
        leading.emplace_back(i, close);
        i = close + 1;
    }
    // The tags go after the last \r among them (which would reset them),
    // else into the first block before its first \t.
    std::size_t target = 0;
    bool afterReset = false;
    for (std::size_t b = 0; b < leading.size(); ++b) {
        const auto [open, close] = leading[b];
        for (const Tag &tag : tagsOf(u16v(out).substr(open + 1, close - open - 1)))
            if (tag.name == u"r") {
                target = b;
                afterReset = true;
            }
    }

    u16 tags;
    // After a \r the base is the Style it names, so every tag is written;
    // otherwise one at the Line's Style's value is left out.
    const auto put = [&tags, afterReset](u16v name, double value, int precision, double initial) {
        const u16 v = fmt(value, precision);
        if (afterReset || v != fmt(initial, precision))
            tags += u16(u"\\") + u16(name) + v;
    };
    if (!moved)
        tags += u"\\pos(" + fmt(t.pos.x, 2) + u"," + fmt(t.pos.y, 2) + u")";
    // libass turns a \move Line without an \org about its moving position:
    // kept when `t` does not turn it (the origin then does nothing) or
    // turns it about its position, unless the origin is the one edited.
    const bool turns = fmt(t.frx, 4) != u"0" || fmt(t.fry, 4) != u"0" || fmt(t.frz, 4) != u"0";
    const bool aboutPosition = fmt(t.org.x, 2) == fmt(t.pos.x, 2) && fmt(t.org.y, 2) == fmt(t.pos.y, 2);
    if (explicitOrg || !(moved && !ownOrg && (!turns || aboutPosition)))
        tags += u"\\org(" + fmt(t.org.x, 2) + u"," + fmt(t.org.y, 2) + u")";
    put(u"fax", t.fax, 6, 0);
    put(u"fscx", t.fscx, 2, std::max(0.0, transform::styleNumber(style.scaleX, 100)));
    put(u"fscy", t.fscy, 2, std::max(0.0, transform::styleNumber(style.scaleY, 100)));
    put(u"frz", t.frz, 4, transform::styleNumber(style.angle, 0));
    put(u"frx", t.frx, 4, 0);
    put(u"fry", t.fry, 4, 0);
    const double bord = std::max(0.0, transform::styleNumber(style.outlineWidth, 0));
    const double shad = std::max(0.0, transform::styleNumber(style.shadow, 0));
    put(u"xbord", t.bord.x, 2, bord);
    put(u"ybord", t.bord.y, 2, bord);
    put(u"xshad", t.shad.x, 2, shad);
    put(u"yshad", t.shad.y, 2, shad);

    // The leading blocks again, in one pass: each loses the tags written
    // (what follows one's value, a comment, goes to the block's start) and,
    // when a \pos is written, any \move (which would come first); the
    // target takes the new ones; a block left empty goes.
    static constexpr u16v kWritten[] = {u"fax",  u"fay",  u"fscx", u"fscy",  u"fsc",   u"frx",   u"fry",
                                        u"frz",  u"fr",   u"bord", u"xbord", u"ybord", u"shad",  u"xshad",
                                        u"yshad", u"pos", u"org"};
    u16 head;
    for (std::size_t b = 0; b < leading.size(); ++b) {
        const auto [open, close] = leading[b];
        const u16v block = u16v(out).substr(open + 1, close - open - 1);
        // Text before the block's first backslash (a comment) stays first.
        const std::size_t firstTag = block.find(u'\\');
        u16 lead(block.substr(0, firstTag == u16v::npos ? block.size() : firstTag));
        u16 kept;
        std::size_t insertAt = u16::npos;
        for (const Tag &tag : tagsOf(block)) {
            if (std::find(std::begin(kWritten), std::end(kWritten), tag.name) != std::end(kWritten) ||
                (!moved && tag.name == u"move")) {
                lead += tailOf(tag);
                continue;
            }
            if (!afterReset && tag.name == u"t" && insertAt == u16::npos)
                insertAt = kept.size();
            kept += block.substr(tag.from, tag.to - tag.from);
            if (afterReset && tag.name == u"r")
                insertAt = kept.size(); // after the last one
        }
        if (b == target) {
            if (insertAt == u16::npos)
                kept += tags;
            else
                kept.insert(insertAt, tags);
        }
        if (b != target && lead.empty() && kept.empty())
            continue; // emptied
        head += u'{';
        head += lead;
        head += kept;
        head += u'}';
    }
    const std::size_t rest = leading.empty() ? 0 : leading.back().second + 1;
    return head + out.substr(rest);
}

} // namespace perspective

// --------------------------------------------------------------- the tool

using namespace perspective;

namespace {

constexpr std::uint32_t kInnerColour = 0xFFBB0000;
constexpr std::uint32_t kOuterColour = 0xAAFF0000;
constexpr std::uint32_t kGridColour = 0x55FF0000;
constexpr std::uint32_t kOrgColour = 0xFFFF8800;
constexpr float kHit = 8.f; // legacy pixels, as the other tools' handles; times unitOf

PointF add(PointF a, PointF b) { return {a.x + b.x, a.y + b.y}; }
PointF sub(PointF a, PointF b) { return {a.x - b.x, a.y - b.y}; }
bool near(PointF a, PointF b, float unit)
{
    return std::abs(a.x - b.x) < kHit * unit && std::abs(a.y - b.y) < kHit * unit;
}

bool inside(const Quad &q, PointF p)
{
    if (!convex(q))
        return false;
    const PointF uv = toUV(q, p);
    return uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;
}

int layoutHeight(const VisualHost &host)
{
    if (const EditSession *s = host.session()) {
        if (const auto v = s->document().scriptInfo(u8"LayoutResY")) {
            const int n = std::atoi(std::string(v->begin(), v->end()).c_str());
            if (n > 0)
                return n;
        }
    }
    // libass lays out at the size the frame is rendered at, the video's.
    if (host.view().frameHeight() > 0)
        return host.view().frameHeight();
    return host.view().scriptHeight();
}

std::u8string u8(const std::u16string &text)
{
    return core::toUtf8(text);
}

// The part of a to b the view shows (Liang–Barsky), so a dashed edge is
// dashed only across the view however far off it its corners are.
std::optional<std::array<PointF, 2>> inView(PointF a, PointF b, const VideoView &view)
{
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
        return std::nullopt;
    constexpr double kMargin = 16;
    const double x0 = -kMargin, y0 = -kMargin;
    const double x1 = view.clientWidth() + kMargin, y1 = view.clientHeight() + kMargin;
    const double dx = double(b.x) - a.x, dy = double(b.y) - a.y;
    double from = 0, to = 1;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x - x0, x1 - a.x, a.y - y0, y1 - a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return std::nullopt;
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0)
            from = std::max(from, r);
        else
            to = std::min(to, r);
        if (from >= to)
            return std::nullopt;
    }
    return std::array<PointF, 2>{PointF{float(a.x + from * dx), float(a.y + from * dy)},
                                 PointF{float(a.x + to * dx), float(a.y + to * dy)}};
}

} // namespace

Quad PerspectiveTool::outer() const
{
    // The inner quad is c1 .. c2 of the outer one's plane.
    const PointF a{-m_c1.x / (m_c2.x - m_c1.x), -m_c1.y / (m_c2.y - m_c1.y)};
    const PointF b{(1 - m_c1.x) / (m_c2.x - m_c1.x), (1 - m_c1.y) / (m_c2.y - m_c1.y)};
    return subQuad(m_inner, a, b);
}

Quad PerspectiveTool::startQuad(bool outerQuad) const
{
    if (!outerQuad)
        return m_startInner;
    const PointF a{-m_startC1.x / (m_startC2.x - m_startC1.x), -m_startC1.y / (m_startC2.y - m_startC1.y)};
    const PointF b{(1 - m_startC1.x) / (m_startC2.x - m_startC1.x), (1 - m_startC1.y) / (m_startC2.y - m_startC1.y)};
    return subQuad(m_startInner, a, b);
}

void PerspectiveTool::selected(VisualHost &host)
{
    (void)host;
    m_drag = Drag::None;
    m_selected = -1;
    m_valueEdit = m_keyEdit = false;
}

void PerspectiveTool::reset(VisualHost &host)
{
    m_valueEdit = m_keyEdit = false;
    m_drag = Drag::None;
    const transform::Context ctx = transform::context(host, false);
    m_valid = false;
    if (!ctx.active) {
        m_line.reset();
        host.toolChanged();
        return;
    }
    // LineIds are a Document's: another session's planes are not this one's.
    const std::uint64_t session = host.session() ? host.session()->serial() : 0;
    if (session != m_session) {
        m_session = session;
        m_planes.clear();
        m_line.reset();
    }
    const bool newLine = m_line != ctx.active->id;
    if (newLine) {
        m_selected = -1;
        m_line = ctx.active->id;
        m_c1 = {0.25f, 0.25f};
        m_c2 = {0.75f, 0.75f};
    }
    m_time = host.videoTimeMs();
    m_screenZ = screenZ(host.view().scriptHeight(), layoutHeight(host));
    if (const auto state = readLine(host, *ctx.active, m_styles)) {
        m_state = *state;
        m_inner = textToQuad(m_state.transform, m_state.box, m_screenZ);
        m_valid = convex(m_inner);
        m_read = readAt(host);
    }
    // A saved plane holds on any reading again (an edit, an Undo, a new
    // Style), not only on another Line's; an edit open keeps its own.
    if (newLine || (m_drag == Drag::None && !m_valueEdit && !m_keyEdit && !host.gesture()))
        restorePlane();
    if (m_plane && !host.gesture())
        fitPlane();
    host.toolChanged();
}

void PerspectiveTool::restorePlane()
{
    // arch1t3cht's: the inner quad's corners in the saved quad's plane, so
    // the plane stays where it was set when the inner quad changed since.
    if (!m_line || !m_valid)
        return;
    const auto saved = m_planes.find(*m_line);
    if (saved == m_planes.end())
        return;
    const PointF c1 = toUV(saved->second, m_inner[0]), c2 = toUV(saved->second, m_inner[2]);
    if (planeHolds(m_inner, c1, c2)) {
        m_c1 = c1;
        m_c2 = c2;
    }
}

void PerspectiveTool::fitPlane() const
{
    if (!m_valid || planeHolds(m_inner, m_c1, m_c2))
        return;
    // The default (the plane twice the inner quad), else a smaller one: a
    // strongly turned quad's plane meets its horizon close by.
    for (const float m : {0.25f, 0.2f, 0.15f, 0.1f, 0.05f, 0.02f, 0.f}) {
        m_c1 = {m, m};
        m_c2 = {1 - m, 1 - m};
        if (planeHolds(m_inner, m_c1, m_c2))
            return;
    }
}

void PerspectiveTool::follow(const VisualHost &host) const
{
    // The controller only redraws when the time changes; a \move's position
    // and an unpositioned Line's stacking are the time's, so the Line is read
    // again (not during an edit, which works from where it began). Where it
    // cannot be read (an obstacle the stacking cannot measure, say) or would
    // fold, it has no quad until it can be again.
    if (!m_line || host.videoTimeMs() == m_time || m_drag != Drag::None || m_valueEdit || m_keyEdit)
        return;
    m_time = host.videoTimeMs();
    // A Line neither moving nor stacked is where it was until the script
    // changes: it is not parsed again every frame.
    if (m_valid && !m_state.hasMove && !m_state.stacks && m_read && m_read == readAt(host))
        return;
    const transform::Context ctx = transform::context(host, false);
    if (!ctx.active || ctx.active->id != *m_line)
        return;
    const auto state = readLine(host, *ctx.active, m_styles);
    const Quad inner = state ? textToQuad(state->transform, state->box, m_screenZ) : Quad{};
    if (!state || !convex(inner)) {
        if (m_plane && m_valid)
            m_planes[*m_line] = outer(); // where it was, for when it is read again
        m_valid = false;
        return;
    }
    // A plane shown stays where it is matched to the shot, and one dropped
    // with its quad comes back where it was.
    std::optional<Quad> plane;
    if (m_plane && m_valid)
        plane = outer();
    else if (const auto saved = m_planes.find(*m_line); m_plane && saved != m_planes.end())
        plane = saved->second;
    m_state = *state;
    m_inner = inner;
    m_valid = true;
    m_read = readAt(host);
    if (plane) {
        const PointF c1 = toUV(*plane, m_inner[0]), c2 = toUV(*plane, m_inner[2]);
        if (planeHolds(m_inner, c1, c2)) {
            m_c1 = c1;
            m_c2 = c2;
        }
    }
    if (m_plane)
        fitPlane();
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> PerspectiveTool::readAt(const VisualHost &host)
{
    const EditSession *session = host.session();
    if (!session)
        return std::nullopt;
    return std::pair{session->serial(), session->revision()};
}

bool PerspectiveTool::beginEdit(VisualHost &host)
{
    if (host.gesture())
        return true;
    const auto targets = host.batchTargets();
    if (targets.empty())
        return false;
    if (!host.beginGesture(targets, std::string(familyInfo(Family::Perspective).history)).has_value())
        return false;
    m_orgEdited = false;
    // Each step parses every target again: their text is bounded as a
    // read's is.
    std::size_t parsed = 0;
    for (const core::LineId &id : host.gesture()->targets())
        parsed += perspective::renderedSize(host.gesture()->before(id));
    if (parsed > perspective::kMaxParsed) {
        host.cancelGesture();
        return false;
    }
    // ... and their Styles as a read's are.
    const transform::Context ctx = transform::context(host, false);
    const auto decoded = m_styles.get(host);
    const perspective::StyleIndex styles(ctx, *decoded);
    for (const core::LineId &id : host.gesture()->targets())
        if (perspective::stylesTooLarge(styles, host.gesture()->before(id))) {
            host.cancelGesture();
            return false;
        }
    return true;
}

void PerspectiveTool::stageAll(VisualHost &host)
{
    Gesture *g = host.gesture();
    if (!g)
        return;
    const transform::Context ctx = transform::context(host, false);
    const auto decoded = m_styles.get(host);
    const StyleIndex styles(ctx, *decoded); // once: the targets can be many, the Styles too
    for (const core::LineId &id : g->targets()) {
        const core::LineRecord &line = g->before(id);
        // Each target's own outline and shadow, scaled as the active
        // Line's; its \move moved by the active Line's change of position.
        Transform t = m_state.transform;
        const LineState own = readTagsIn(host, styles, line);
        const double kx = m_startTransform.fscx != 0 ? t.fscx / m_startTransform.fscx : 1;
        const double ky = m_startTransform.fscy != 0 ? t.fscy / m_startTransform.fscy : 1;
        t.bord = {static_cast<float>(own.transform.bord.x * kx), static_cast<float>(own.transform.bord.y * ky)};
        t.shad = {static_cast<float>(own.transform.shad.x * kx), static_cast<float>(own.transform.shad.y * ky)};
        const u16 text = transform::lineText(line);
        const core::StyleValues &style = styles.line(line.style);
        g->stage(id, u8(writeTransform(text, t, style, m_startTransform.pos, m_orgEdited)),
                 transform::editsTranslation(line));
    }
    host.toolChanged();
}

void PerspectiveTool::rememberPlane(VisualHost &host)
{
    if (!m_plane)
        return;
    if (Gesture *g = host.gesture())
        for (const core::LineId &id : g->targets())
            m_planes[id] = outer();
}

bool PerspectiveTool::finish(VisualHost &host)
{
    m_drag = Drag::None;
    m_valueEdit = m_keyEdit = false;
    if (!host.gesture())
        return false;
    // A locked outer quad's resize changes no Line: kept all the same.
    rememberPlane(host);
    // A release that changes no Line records nothing (T3-release-no-change).
    if (!host.gesture()->changesAnyLine()) {
        host.cancelGesture();
        reset(host);
        return false;
    }
    const bool committed = host.commitGesture().has_value();
    if (!committed)
        reset(host);
    return committed;
}

bool PerspectiveTool::applyInner(const Quad &inner, VisualHost &host, bool explicitOrg)
{
    if (!convex(inner) || (m_plane && !planeHolds(inner, m_c1, m_c2)))
        return false;
    Transform from = m_startTransform;
    from.org = m_state.transform.org;
    auto t = explicitOrg ? quadToTransform(inner, m_state.box, OrgMode::Keep, from, m_screenZ)
                         : quadToTransform(inner, m_state.box, m_orgMode, from, m_screenZ);
    if (t && !explicitOrg && m_state.hasMove && !m_state.ownOrg && !keepsMovingOrigins(*t, host, false)) {
        // libass turns it about its moving position: the turn about the
        // position itself, found by moving a kept \org onto the position
        // the quad then gives until they meet.
        std::optional<Transform> about;
        Transform f = from;
        PointF guess = m_state.transform.pos;
        for (int i = 0; i < 64 && !about; ++i) {
            f.org = guess;
            const auto k = quadToTransform(inner, m_state.box, OrgMode::Keep, f, m_screenZ);
            if (!k)
                break;
            if (std::hypot(k->pos.x - guess.x, k->pos.y - guess.y) < 1e-3) {
                about = k;
                about->org = about->pos;
            }
            guess = k->pos;
        }
        t = about;
    }
    if (!t || !keepsMovingOrigins(*t, host, explicitOrg) || !scalesEveryOutline(*t, host))
        return false;
    m_inner = inner;
    m_state.transform = *t;
    m_orgEdited = m_orgEdited || explicitOrg;
    return true;
}

bool PerspectiveTool::keepsMovingOrigins(const Transform &t, VisualHost &host, bool explicitOrg) const
{
    if (std::abs(t.frx) < 5e-5 && std::abs(t.fry) < 5e-5 && std::abs(t.frz) < 5e-5)
        return true; // no turn: the origin does nothing
    const auto about = [&t](PointF pos) {
        return std::abs(t.org.x - pos.x) < 0.005 && std::abs(t.org.y - pos.y) < 0.005;
    };
    if (m_state.hasMove && !m_state.ownOrg && !explicitOrg && !about(t.pos))
        return false;
    // The other targets' \move moves with the active Line's position; each
    // keeps its origin only about its own, which the active Line's \org is
    // not.
    const Gesture *g = host.gesture();
    if (!g || g->targets().size() < 2)
        return true;
    const transform::Context ctx = transform::context(host, false);
    const auto decoded = m_styles.get(host);
    const StyleIndex styles(ctx, *decoded);
    for (const core::LineId &id : g->targets()) {
        if (m_line && id == *m_line)
            continue;
        const LineState own = readTagsIn(host, styles, g->before(id));
        if (own.hasMove && !own.ownOrg)
            return false;
    }
    return true;
}

bool PerspectiveTool::scalesEveryOutline(const Transform &t, VisualHost &host) const
{
    if (t.fscx == m_startTransform.fscx && t.fscy == m_startTransform.fscy)
        return true; // nothing to scale
    const Gesture *g = host.gesture();
    if (!g)
        return true;
    // A later scale or \r sets the rest of the text back to its own size
    // and outline, which the leading blocks' rescale does not reach; a \t
    // of the box ends at its own, unscaled.
    static constexpr u16v kRescaled[] = {u"fscx", u"fscy", u"fsc", u"r"};
    for (const core::LineId &id : g->targets()) {
        const u16 text = transform::lineText(g->before(id));
        if (outlineAfterText(text) || afterText(text, kRescaled) || animatesTheBox(text))
            return false;
    }
    return true;
}

bool PerspectiveTool::planeOnly(int corner) const
{
    return m_plane && m_lock && corner >= 4;
}

bool PerspectiveTool::applyCorner(int corner, PointF to, bool fromCentre, VisualHost &host)
{
    const bool isOuter = corner >= 4;
    const int i = corner % 4;
    const Quad start = startQuad(isOuter);
    Quad changed = start;
    changed[static_cast<std::size_t>(i)] = to;
    if (fromCentre) {
        // The opposite corner mirrors the drag in the plane.
        const PointF rel = sub(toUV(start, to), {0.5f, 0.5f});
        for (int j = 0; j < 4; ++j) {
            const float fx = ((i == 1 || i == 2) ? -1.f : 1.f) * ((j == 1 || j == 2) ? -1.f : 1.f);
            const float fy = (i >= 2 ? -1.f : 1.f) * (j >= 2 ? -1.f : 1.f);
            changed[static_cast<std::size_t>(j)] = fromUV(start, {0.5f + rel.x * fx, 0.5f + rel.y * fy});
        }
    }
    if (!convex(changed))
        return false;
    PointF c1 = m_startC1, c2 = m_startC2;
    Quad inner = changed;
    if (m_plane && !isOuter && !m_lock) {
        // The plane holds: the inner quad stays a rectangle of it.
        const Quad plane = startQuad(true);
        if (fromCentre) {
            c1 = toUV(plane, changed[0]);
            c2 = toUV(plane, changed[2]);
        } else {
            const PointF uv = toUV(plane, to);
            c1 = {(i == 0 || i == 3) ? uv.x : c1.x, i < 2 ? uv.y : c1.y};
            c2 = {(i == 0 || i == 3) ? c2.x : uv.x, i < 2 ? c2.y : uv.y};
        }
        inner = subQuad(plane, c1, c2);
    } else if (m_plane && isOuter && m_lock) {
        // The inner quad holds: the outer one is resized in its plane.
        PointF d1{-c1.x / (c2.x - c1.x), -c1.y / (c2.y - c1.y)};
        PointF d2{(1 - c1.x) / (c2.x - c1.x), (1 - c1.y) / (c2.y - c1.y)};
        if (fromCentre) {
            d1 = toUV(m_startInner, changed[0]);
            d2 = toUV(m_startInner, changed[2]);
        } else {
            const PointF uv = toUV(m_startInner, to);
            d1 = {(i == 0 || i == 3) ? uv.x : d1.x, i < 2 ? uv.y : d1.y};
            d2 = {(i == 0 || i == 3) ? d2.x : uv.x, i < 2 ? d2.y : uv.y};
        }
        c1 = {-d1.x / (d2.x - d1.x), -d1.y / (d2.y - d1.y)};
        c2 = {(1 - d1.x) / (d2.x - d1.x), (1 - d1.y) / (d2.y - d1.y)};
        // No Line changes (planeOnly): the transform is not rewritten.
        if (!(c1.x < c2.x && c1.y < c2.y) || !std::isfinite(c1.x) || !std::isfinite(c1.y) ||
            !std::isfinite(c2.x) || !std::isfinite(c2.y) || !planeHolds(m_inner, c1, c2))
            return false;
        m_c1 = c1;
        m_c2 = c2;
        return true;
    } else if (m_plane && isOuter) {
        // The outer quad is the plane: the inner one follows it.
        inner = subQuad(changed, c1, c2);
    }
    if (!(c1.x < c2.x && c1.y < c2.y) || !std::isfinite(c1.x) || !std::isfinite(c1.y) || !std::isfinite(c2.x) ||
        !std::isfinite(c2.y))
        return false;
    const PointF keepC1 = m_c1, keepC2 = m_c2;
    m_c1 = c1;
    m_c2 = c2;
    if (!applyInner(inner, host)) {
        m_c1 = keepC1;
        m_c2 = keepC2;
        return false;
    }
    return true;
}

bool PerspectiveTool::move(PointF delta, VisualHost &host)
{
    if (m_drag == Drag::Body && m_plane && !m_lock) {
        // Within the plane.
        const Quad plane = startQuad(true);
        const PointF from = toUV(plane, centre(m_startInner));
        const PointF to = toUV(plane, add(centre(m_startInner), delta));
        const PointF d = sub(to, from);
        const PointF keepC1 = m_c1, keepC2 = m_c2;
        m_c1 = add(m_startC1, d);
        m_c2 = add(m_startC2, d);
        if (!applyInner(subQuad(plane, m_c1, m_c2), host)) {
            m_c1 = keepC1;
            m_c2 = keepC2;
            return false;
        }
        return true;
    }
    Quad inner = m_startInner;
    for (PointF &p : inner)
        p = add(p, delta);
    return applyInner(inner, host);
}

PointF PerspectiveTool::snap(PointF p, int skip) const
{
    // DrawingAndClip::FindSnapPoint's rule: within ten units of another
    // handle's x or y, the first such handle's.
    std::vector<PointF> handles;
    const Quad o = startQuad(true);
    for (int i = 0; i < 4; ++i) {
        if (i != skip)
            handles.push_back(m_startInner[static_cast<std::size_t>(i)]);
        if (m_plane && i + 4 != skip)
            handles.push_back(o[static_cast<std::size_t>(i)]);
    }
    if (skip != 8)
        handles.push_back(m_startOrg);
    bool xfound = false, yfound = false;
    for (const PointF h : handles) {
        if (!xfound && std::abs(h.x - p.x) <= 10.f) {
            xfound = true;
            p.x = h.x;
        }
        if (!yfound && std::abs(h.y - p.y) <= 10.f) {
            yfound = true;
            p.y = h.y;
        }
    }
    return p;
}

PointF PerspectiveTool::axisLock(int corner, PointF d) const
{
    // The nearer of the two edges through the corner (the plane's axes there).
    const Quad q = startQuad(corner >= 4);
    const int i = corner % 4;
    const PointF u = (i < 2) ? sub(q[1], q[0]) : sub(q[2], q[3]);
    const PointF v = (i == 0 || i == 3) ? sub(q[3], q[0]) : sub(q[2], q[1]);
    const auto project = [&d](PointF a) {
        const double len = std::hypot(a.x, a.y);
        if (len == 0)
            return d;
        const double k = (d.x * a.x + d.y * a.y) / (len * len);
        return PointF{static_cast<float>(a.x * k), static_cast<float>(a.y * k)};
    };
    const PointF pu = project(u), pv = project(v);
    const double eu = std::hypot(pu.x - d.x, pu.y - d.y), ev = std::hypot(pv.x - d.x, pv.y - d.y);
    return eu <= ev ? pu : pv;
}

void PerspectiveTool::pointer(const Pointer &event, VisualHost &host)
{
    follow(host);
    if (event.kind == Pointer::Kind::Wheel || !m_valid)
        return;
    const VideoView &view = host.view();
    const PointF dev{static_cast<float>(event.x), static_cast<float>(event.y)};
    const PointF at = view.viewToScript(dev);
    const auto capture = [this] {
        m_press = {0, 0};
        m_startInner = m_inner;
        m_startC1 = m_c1;
        m_startC2 = m_c2;
        m_startOrg = m_state.transform.org;
        m_startTransform = m_state.transform;
    };

    if (event.kind == Pointer::Kind::Press && event.button == Pointer::Button::Left) {
        if (host.gesture())
            return;
        capture();
        m_press = at;
        m_drag = Drag::None;
        const Quad o = outer();
        if (m_orgMode == OrgMode::Keep && near(dev, view.scriptToView(m_state.transform.org), unitOf(host))) {
            m_drag = Drag::Org;
        }
        for (int i = 0; i < 4 && m_drag == Drag::None; ++i) {
            if (near(dev, view.scriptToView(m_inner[static_cast<std::size_t>(i)]), unitOf(host))) {
                m_drag = Drag::Inner;
                m_corner = i;
            }
        }
        for (int i = 0; i < 4 && m_drag == Drag::None && m_plane; ++i) {
            if (near(dev, view.scriptToView(o[static_cast<std::size_t>(i)]), unitOf(host))) {
                m_drag = Drag::Outer;
                m_corner = i + 4;
            }
        }
        if (m_drag == Drag::None && inside(m_inner, at))
            m_drag = Drag::Body;
        else if (m_drag == Drag::None && m_plane && inside(o, at))
            m_drag = Drag::Plane;
        if (m_drag == Drag::None)
            return;
        if (m_drag == Drag::Inner || m_drag == Drag::Outer)
            m_selected = m_corner;
        if (!beginEdit(host)) {
            m_drag = Drag::None;
            return;
        }
        host.setPointerShape(PointerShape::Sizing);
        host.toolChanged();
        return;
    }

    if (event.kind == Pointer::Kind::DoubleClick && event.button == Pointer::Button::Left) {
        // The nearest corner of the quad that leads goes to the pointer.
        if (!host.gesture()) {
            capture();
            if (!beginEdit(host))
                return;
        }
        const bool useOuter = m_plane && !m_lock;
        const Quad q = startQuad(useOuter);
        int best = 0;
        double bestd = -1;
        for (int i = 0; i < 4; ++i) {
            const PointF c = q[static_cast<std::size_t>(i)];
            const double d = std::hypot(c.x - at.x, c.y - at.y);
            if (bestd < 0 || d < bestd) {
                bestd = d;
                best = i;
            }
        }
        m_corner = best + (useOuter ? 4 : 0);
        m_selected = m_corner;
        m_drag = useOuter ? Drag::Outer : Drag::Inner;
        m_press = q[static_cast<std::size_t>(best)];
        if (applyCorner(m_corner, at, false, host) && !planeOnly(m_corner))
            stageAll(host);
        // Its release commits; no drag follows.
        m_drag = Drag::None;
        return;
    }

    if (event.kind == Pointer::Kind::Move && event.leftDown && m_drag != Drag::None && host.gesture()) {
        PointF delta = sub(at, m_press);
        bool changed = false;
        switch (m_drag) {
        case Drag::Inner:
        case Drag::Outer: {
            if (event.shift)
                delta = axisLock(m_corner, delta);
            const Quad q = startQuad(m_drag == Drag::Outer);
            PointF to = add(q[static_cast<std::size_t>(m_corner % 4)], delta);
            if (event.control)
                to = snap(to, m_corner);
            changed = applyCorner(m_corner, to, event.alt, host) && !planeOnly(m_corner);
            break;
        }
        case Drag::Org: {
            PointF to = add(m_startOrg, delta);
            if (event.control)
                to = snap(to, 8);
            const PointF keep = m_state.transform.org;
            m_state.transform.org = to;
            changed = applyInner(m_inner, host, true);
            if (!changed)
                m_state.transform.org = keep;
            break;
        }
        case Drag::Body:
        case Drag::Plane:
            if (event.shift) {
                if (std::abs(delta.x) >= std::abs(delta.y))
                    delta.y = 0;
                else
                    delta.x = 0;
            }
            changed = move(delta, host);
            break;
        case Drag::None:
            break;
        }
        if (changed)
            stageAll(host);
        return;
    }

    if (event.kind == Pointer::Kind::Release && event.button == Pointer::Button::Left) {
        if (m_drag == Drag::None && !host.gesture())
            return;
        host.setPointerShape(PointerShape::Arrow);
        if (!m_valueEdit && !m_keyEdit)
            (void)finish(host);
    }
}

bool PerspectiveTool::takesKey(const Key &event) const
{
    return nudgeDirection(event).has_value();
}

bool PerspectiveTool::key(const Key &event, VisualHost &host)
{
    follow(host);
    if (!m_valid)
        return false;
    if (event.release) {
        if (!isArrowKey(event.key) || !m_keyEdit)
            return false;
        if (!event.autoRepeat)
            (void)finish(host);
        return true;
    }
    const auto dir = nudgeDirection(event);
    if (!dir)
        return false;
    if (host.gesture() && !m_keyEdit)
        return false;
    if (!host.gesture()) {
        m_startTransform = m_state.transform;
        if (!beginEdit(host))
            return false;
        m_keyEdit = true;
    }
    // Each step from where the last left the quad.
    m_startInner = m_inner;
    m_startC1 = m_c1;
    m_startC2 = m_c2;
    m_startOrg = m_state.transform.org;
    const float step = event.shift ? 0.1f : 1.f;
    const PointF delta{dir->x * step, dir->y * step};
    bool moved = false;
    if (m_selected >= 0) {
        const Quad q = startQuad(m_selected >= 4);
        moved = applyCorner(m_selected, add(q[static_cast<std::size_t>(m_selected % 4)], delta), false, host) &&
                !planeOnly(m_selected);
    } else {
        m_drag = Drag::Plane;
        moved = move(delta, host);
        m_drag = Drag::None;
    }
    // A step that would fold the quad changes nothing (the key is still
    // the tool's: the release ends the edit, which records nothing then).
    if (moved)
        stageAll(host);
    return true;
}

Overlay PerspectiveTool::overlay(const VisualHost &host) const
{
    follow(host);
    Overlay out;
    out.unit = unitOf(host);
    if (!m_valid)
        return out;
    const VideoView &view = host.view();
    const auto toView = [&view](const Quad &q) {
        Quad v{};
        for (int i = 0; i < 4; ++i)
            v[static_cast<std::size_t>(i)] = view.scriptToView(q[static_cast<std::size_t>(i)]);
        return v;
    };
    const Quad inner = toView(m_inner);
    const Quad o = m_plane ? toView(outer()) : Quad{};
    if (m_grid) {
        // The plane's grid: eighths of the outer quad, or quarters of the
        // inner one without the plane.
        const Quad &g = m_plane ? outer() : m_inner;
        const int n = m_plane ? 8 : 4;
        for (int k = 1; k < n; ++k) {
            const float s = static_cast<float>(k) / n;
            out.lines.push_back({view.scriptToView(fromUV(g, {s, 0})), view.scriptToView(fromUV(g, {s, 1})), 1.f,
                                 kGridColour});
            out.lines.push_back({view.scriptToView(fromUV(g, {0, s})), view.scriptToView(fromUV(g, {1, s})), 1.f,
                                 kGridColour});
        }
    }
    if (m_plane)
        for (std::size_t i = 0; i < 4; ++i)
            if (const auto edge = inView(o[i], o[(i + 1) % 4], view))
                drawDashedLine(out, edge->data(), 2, 6, kOuterColour);
    for (int i = 0; i < 4; ++i)
        out.lines.push_back({inner[static_cast<std::size_t>(i)], inner[static_cast<std::size_t>((i + 1) % 4)], 2.f,
                             kInnerColour});
    const PointF org = view.scriptToView(m_state.transform.org);
    if (m_orgMode == OrgMode::Keep)
        drawCircle(out, org, m_drag == Drag::Org);
    out.lines.push_back({{org.x - 6.f, org.y}, {org.x + 6.f, org.y}, 2.f, kOrgColour});
    out.lines.push_back({{org.x, org.y - 6.f}, {org.x, org.y + 6.f}, 2.f, kOrgColour});
    for (int i = 0; i < 4; ++i)
        drawRect(out, inner[static_cast<std::size_t>(i)], m_selected == i);
    if (m_plane)
        for (int i = 0; i < 4; ++i)
            drawCircle(out, o[static_cast<std::size_t>(i)], m_selected == i + 4);
    return out;
}

std::vector<ToolValue> PerspectiveTool::values(const VisualHost &host) const
{
    follow(host);
    const Transform &t = m_state.transform;
    const bool keep = m_orgMode == OrgMode::Keep;
    const auto text = [this](double v, int precision) { return m_valid ? fmt(v, precision) : std::u16string(); };
    return {{"orgx", u"X", text(t.org.x, 2), m_valid && keep, u"Origin", {}, 1},
            {"orgy", u"Y", text(t.org.y, 2), m_valid && keep, u"Origin", {}, 1},
            {"frx", u"X", text(t.frx, 2), m_valid, u"Rotation", u"°", 0.5},
            {"fry", u"Y", text(t.fry, 2), m_valid, u"Rotation", u"°", 0.5},
            {"frz", u"Z", text(t.frz, 2), m_valid, u"Rotation", u"°", 0.5},
            {"fax", u"Shear", text(t.fax, 3), m_valid, {}, {}, 0.01}};
}

bool PerspectiveTool::setValue(const std::string &name, const std::u16string &text, VisualHost &host, ValueEdit edit)
{
    follow(host);
    double value = 0;
    if (!host.gesture())
        m_valueEdit = false;
    if (!m_valid || !parseValue(text, value) || (host.gesture() && !m_valueEdit))
        return false;
    const bool org = name == "orgx" || name == "orgy";
    if (!org && name != "frx" && name != "fry" && name != "frz" && name != "fax")
        return false;
    if (org && m_orgMode != OrgMode::Keep)
        return false;
    const bool opened = !host.gesture();
    if (opened) {
        m_startInner = m_inner;
        m_startC1 = m_c1;
        m_startC2 = m_c2;
        m_startOrg = m_state.transform.org;
        m_startTransform = m_state.transform;
        if (!beginEdit(host))
            return false;
    }
    // A refused value leaves no gesture of its own open (the controller
    // cancels a scrub's only once one of its values is taken).
    const auto refuse = [&host, opened] {
        if (opened)
            host.cancelGesture();
        return false;
    };
    if (org) {
        // Keep: the quad stays and the tags follow the new \org.
        PointF o = m_startOrg;
        (name == "orgx" ? o.x : o.y) = static_cast<float>(value);
        m_state.transform.org = o;
        if (!applyInner(m_startInner, host, true)) {
            m_state.transform.org = m_startOrg;
            return refuse();
        }
    } else {
        // An angle or the shear: the quad follows the tags.
        Transform t = m_startTransform;
        (name == "frx" ? t.frx : name == "fry" ? t.fry : name == "frz" ? t.frz : t.fax) = value;
        t.fay = 0; // the tool writes no \fay, as a corner's edit

        const Quad q = textToQuad(t, m_state.box, m_screenZ);
        if (!convex(q) || (m_plane && !planeHolds(q, m_c1, m_c2)) || !keepsMovingOrigins(t, host, false))
            return refuse();
        m_state.transform = t;
        m_inner = q;
    }
    stageAll(host);
    if (edit == ValueEdit::Sample)
        return m_valueEdit = host.gesture() != nullptr;
    const std::uint64_t before = host.session() ? host.session()->revision() : 0;
    m_valueEdit = false;
    (void)finish(host);
    return host.session() && host.session()->revision() != before;
}

std::vector<ToolOption> PerspectiveTool::options(const VisualHost &host) const
{
    (void)host;
    ToolOption plane;
    plane.name = "plane";
    plane.iconRole = "perspective-plane";
    plane.tooltip = u"Show surrounding plane\nA rectangle of the text's plane: match it to one in the shot, then place "
                    u"the text inside it";
    plane.checked = m_plane;
    plane.group = u"Plane";
    ToolOption lock;
    lock.name = "lockOuter";
    lock.iconRole = "perspective-lock-outer";
    lock.tooltip = u"Lock outer quad\nOff: the inner quad is resized within the plane. On: the outer quad is, and "
                   u"the inner one sets the perspective";
    lock.checked = m_lock;
    lock.enabled = m_plane;
    lock.group = u"Plane";
    ToolOption grid;
    grid.name = "grid";
    grid.iconRole = "perspective-grid";
    grid.tooltip = u"Show grid";
    grid.checked = m_grid;
    grid.group = u"Plane";
    ToolOption mode;
    mode.name = "orgMode";
    mode.kind = ToolOption::Kind::Choice;
    mode.tooltip = u"\\org mode";
    mode.choices = {u"Center", u"No \\fax", u"Keep"};
    mode.index = static_cast<int>(m_orgMode);
    mode.group = u"Origin";
    return {plane, lock, grid, mode};
}

bool PerspectiveTool::setOption(const std::string &name, int value, VisualHost &host)
{
    if (host.gesture())
        return false;
    if (name == "plane") {
        if (m_plane == (value != 0))
            return false;
        if (m_plane && m_line && m_valid)
            m_planes[*m_line] = outer(); // where it was when hidden
        if (value != 0 && m_valid) {
            // Shown only where it holds (its outer quad short of the
            // horizon), the saved one else the largest default that does.
            const PointF keepC1 = m_c1, keepC2 = m_c2;
            restorePlane();
            fitPlane();
            if (!planeHolds(m_inner, m_c1, m_c2)) {
                m_c1 = keepC1;
                m_c2 = keepC2;
                return false;
            }
        }
        m_plane = value != 0;
        if (m_plane && m_line && m_valid)
            m_planes[*m_line] = outer(); // absolute from now on
        if (!m_plane && (m_selected >= 4))
            m_selected = -1;
    } else if (name == "lockOuter") {
        if (!m_plane || m_lock == (value != 0))
            return false;
        m_lock = value != 0;
    } else if (name == "grid") {
        if (m_grid == (value != 0))
            return false;
        m_grid = value != 0;
    } else if (name == "orgMode") {
        if (value < 0 || value > 2 || static_cast<int>(m_orgMode) == value)
            return false;
        m_orgMode = static_cast<OrgMode>(value);
    } else {
        return false;
    }
    host.toolChanged();
    return true;
}

} // namespace hikari::application::visual
