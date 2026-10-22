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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

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

std::vector<double> arguments(u16v value)
{
    // "(a,b,...)" or "(a,b,...": the numbers in order.
    std::vector<double> out;
    if (value.empty() || value.front() != u'(')
        return out;
    value.remove_prefix(1);
    if (const auto close = value.find(u')'); close != u16v::npos)
        value = value.substr(0, close);
    std::size_t from = 0;
    while (from <= value.size()) {
        const std::size_t comma = value.find(u',', from);
        const u16v part = value.substr(from, comma == u16v::npos ? u16v::npos : comma - from);
        out.push_back(number(part).value_or(std::nan("")));
        if (comma == u16v::npos)
            break;
        from = comma + 1;
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
                           u"an",    u"fr",    u"fs",    u"fn",    u"p",    u"a",    u"t",    u"r"};

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
// backslash inside \t(...) or \clip(...) does not start one.
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
        const u16v token = block.substr(i + 1, j - i - 1);
        const u16v name = nameOf(token);
        out.push_back({name, token.substr(name.size()), i, j});
        i = j;
    }
    return out;
}

// The text's blocks: where each opens and closes.
std::vector<std::pair<std::size_t, std::size_t>> blocksOf(u16v text)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t i = 0;
    while ((i = text.find(u'{', i)) != u16v::npos) {
        const std::size_t close = text.find(u'}', i);
        if (close == u16v::npos)
            break;
        out.emplace_back(i, close);
        i = close + 1;
    }
    return out;
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
    // \a's legacy numbering: 1-3 bottom, +4 top, +8 middle.
    const int h = (a & 3) == 0 ? 2 : (a & 3);
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
    const double w = std::max(box.width(), 1.), h = std::max(box.height(), 1.);
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
    const double scalex = quadwidth / std::max(box.width(), 1.);
    const double scaley = quadheight / std::max(box.height(), 1.);

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
               V2{quadwidth * shift.x, quadheight * shift.y});
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
    }
    if (out.p > 0) {
        const std::size_t end = text.find(u'{', i);
        out.drawing = std::u16string(text.substr(i, end == u16v::npos ? u16v::npos : end - i));
    }
    return out;
}

// The Style a \r resets to: the one it names, else the Line's (libass's
// reset_render_context with a Style it cannot find).
core::StyleValues resetStyle(const transform::Context &ctx, const core::StyleValues &lineStyle, u16v name)
{
    if (!name.empty()) {
        const std::u8string wanted = core::toUtf8(u16(name));
        for (const core::StyleValues &s : ctx.styles)
            if (s.name == wanted)
                return s;
    }
    return lineStyle;
}

std::optional<Box> measureBox(const VisualHost &host, const transform::Context &ctx, const core::LineRecord &line,
                              u16v text, const core::StyleValues &style, const Leading &lead)
{
    if (lead.p > 0) {
        // A drawing's points at its \p scale.
        const double k = std::ldexp(1., 1 - lead.p);
        double minx = 0, miny = 0, maxx = 0, maxy = 0;
        bool any = false;
        for (const transform::ClipPoint &p : transform::vectorPoints(lead.drawing)) {
            const double x = p.x * k, y = p.y * k;
            if (!any) {
                minx = maxx = x;
                miny = maxy = y;
                any = true;
            }
            minx = std::min(minx, x);
            maxx = std::max(maxx, x);
            miny = std::min(miny, y);
            maxy = std::max(maxy, y);
        }
        if (!any)
            return std::nullopt;
        return Box{{static_cast<float>(minx), static_cast<float>(miny)},
                   {static_cast<float>(maxx), static_cast<float>(maxy)}};
    }
    (void)line;
    TextMeasurePort *measure = host.textMeasure();
    if (!measure)
        return std::nullopt;
    // The Style at a scale of 100 with the leading \fs and \fn.
    core::StyleValues s = style;
    s.scaleX = u8"100";
    s.scaleY = u8"100";
    for (const Tag &tag : lead.tags) {
        if (tag.name == u"r") {
            s = resetStyle(ctx, style, tag.value);
            s.scaleX = u8"100";
            s.scaleY = u8"100";
        } else if (tag.name == u"fs" && number(tag.value))
            s.fontsize = core::toUtf8(fmt(*number(tag.value), 4));
        else if (tag.name == u"fn" && !tag.value.empty())
            s.fontname = core::toUtf8(tag.value);
    }
    std::vector<std::string> fields;
    for (const auto &f : core::legacy::styleRawFields(s))
        fields.emplace_back(f.begin(), f.end());
    // The plain text, line by line.
    std::u16string plain;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == u'{') {
            const std::size_t close = text.find(u'}', i);
            if (close != u16v::npos) {
                i = close + 1;
                continue;
            }
        }
        plain.push_back(text[i]);
        ++i;
    }
    // \n breaks a line only with WrapStyle 2; otherwise it is a space
    // (libass), as \h is a hard one.
    const bool softBreaks = ctx.document && ctx.document->scriptInfo(u8"WrapStyle").value_or(u8"") == u8"2";
    double width = 0, height = 0;
    std::size_t from = 0;
    while (true) {
        std::size_t br = plain.find(u"\\N", from);
        const std::size_t brn = plain.find(u"\\n", from);
        if (softBreaks && brn != u16v::npos && (br == u16v::npos || brn < br))
            br = brn;
        u16 piece = plain.substr(from, br == u16v::npos ? u16v::npos : br - from);
        for (std::size_t h = piece.find(u"\\h"); h != u16v::npos; h = piece.find(u"\\h", h + 1))
            piece.replace(h, 2, u" ");
        for (std::size_t n = piece.find(u"\\n"); n != u16v::npos; n = piece.find(u"\\n", n + 1))
            piece.replace(n, 2, u" ");
        const std::u8string utf8 = core::toUtf8(piece.empty() ? u16(u" ") : piece);
        const auto e = measure->measure(fields, std::string(utf8.begin(), utf8.end()));
        if (!e)
            return std::nullopt;
        if (!piece.empty())
            width = std::max(width, e->width);
        height += e->height;
        if (br == u16v::npos)
            break;
        from = br + 2;
    }
    return Box{{0, 0}, {static_cast<float>(width), static_cast<float>(height)}};
}

} // namespace

LineState readTags(const VisualHost &host, const core::LineRecord &line)
{
    const transform::Context ctx = transform::context(host);
    const u16 text = transform::lineText(line);
    const core::StyleValues style = transform::lineStyle(ctx, line.style);
    LineState out;
    Transform &t = out.transform;
    t.an = static_cast<int>(transform::styleNumber(style.alignment, 2));
    if (t.an < 1 || t.an > 9)
        t.an = 2;
    // A Style's values (the Line's, or one a \r resets to).
    const auto styleValues = [&t](const core::StyleValues &st) {
        t.frx = t.fry = t.fax = t.fay = 0;
        t.frz = transform::styleNumber(st.angle, 0);
        t.fscx = transform::styleNumber(st.scaleX, 100);
        t.fscy = transform::styleNumber(st.scaleY, 100);
        const float bord = static_cast<float>(transform::styleNumber(st.outlineWidth, 0));
        const float shad = static_cast<float>(transform::styleNumber(st.shadow, 0));
        t.bord = {bord, bord};
        t.shad = {shad, shad};
    };
    styleValues(style);

    // The first \pos, \move, \org and \an anywhere.
    std::optional<V2> pos, org;
    bool haveAn = false;
    for (const auto &[open, close] : blocksOf(text)) {
        for (const Tag &tag : tagsOf(u16v(text).substr(open + 1, close - open - 1))) {
            if ((tag.name == u"pos" || tag.name == u"move") && !pos && !out.hasMove) {
                const auto a = arguments(tag.value);
                if (tag.name == u"pos" && a.size() >= 2 && finite(a[0]) && finite(a[1])) {
                    pos = V2{a[0], a[1]};
                } else if (tag.name == u"move" && a.size() >= 4 && finite(a[0]) && finite(a[1]) && finite(a[2]) &&
                           finite(a[3])) {
                    out.hasMove = true;
                    out.move = {a[0], a[1], a[2], a[3]};
                    const std::size_t comma = [&] {
                        std::size_t c = 0, n = 0;
                        for (std::size_t i = 0; i < tag.value.size(); ++i)
                            if (tag.value[i] == u',' && ++n == 4)
                                c = i;
                        return c;
                    }();
                    if (comma) {
                        const std::size_t end = tag.value.find(u')');
                        out.moveTimes = u16(tag.value.substr(comma, end == u16v::npos ? u16v::npos : end - comma));
                    }
                    // The position at the video's time (libass: linear
                    // between t1 and t2, the Line's span without them).
                    const double lineMs = transform::endMs(line) - transform::startMs(line);
                    double t1 = 0, t2 = lineMs;
                    if (a.size() >= 6 && finite(a[4]) && finite(a[5])) {
                        t1 = a[4];
                        t2 = a[5];
                    }
                    if (t1 > t2)
                        std::swap(t1, t2);
                    const double now = static_cast<double>(host.videoTimeMs()) - transform::startMs(line);
                    const double k = now <= t1 ? 0 : (now >= t2 || t2 == t1 ? 1 : (now - t1) / (t2 - t1));
                    pos = V2{a[0] + (a[2] - a[0]) * k, a[1] + (a[3] - a[1]) * k};
                }
            } else if (tag.name == u"org" && !org) {
                const auto a = arguments(tag.value);
                if (a.size() >= 2 && finite(a[0]) && finite(a[1]))
                    org = V2{a[0], a[1]};
            } else if ((tag.name == u"an" || tag.name == u"a") && !haveAn) {
                if (const auto v = number(tag.value)) {
                    const int n = static_cast<int>(*v);
                    const int an = tag.name == u"an" ? n : alignmentOfA(n);
                    if (an >= 1 && an <= 9) {
                        t.an = an;
                        haveAn = true;
                    }
                }
            }
        }
    }

    // The leading blocks' values, the last of each; a \r resets them.
    const Leading lead = leadingOf(text);
    for (const Tag &tag : lead.tags) {
        if (tag.name == u"r") {
            styleValues(resetStyle(ctx, style, tag.value));
            continue;
        }
        const auto v = number(tag.value);
        if (!v)
            continue;
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

    if (!pos)
        pos = v2(transform::defaultPosition(line, style, t.an, ctx.width, ctx.height));
    t.pos = pf(*pos);
    t.org = org ? pf(*org) : t.pos;
    return out;
}

std::optional<LineState> readLine(const VisualHost &host, const core::LineRecord &line)
{
    LineState out = readTags(host, line);
    const transform::Context ctx = transform::context(host);
    const u16 text = transform::lineText(line);
    const auto box =
        measureBox(host, ctx, line, text, transform::lineStyle(ctx, line.style), leadingOf(text));
    if (!box)
        return std::nullopt;
    out.box = *box;
    return out;
}

std::u16string writeTransform(std::u16string_view text, const Transform &t, const core::StyleValues &style,
                              std::optional<PointF> movedFrom)
{
    u16 out(text);
    if (out.empty() || out.front() != u'{' || out.find(u'}') == u16::npos)
        out.insert(0, u"{}");

    // The Line's first \pos or \move decides, as readTags reads it: a \move
    // is moved, a \pos (or none) is written again.
    bool moved = false;
    bool decided = false;
    for (const auto &[open, close] : blocksOf(out)) {
        for (const Tag &tag : tagsOf(u16v(out).substr(open + 1, close - open - 1))) {
            if (tag.name != u"pos" && tag.name != u"move")
                continue;
            const auto a = arguments(tag.value);
            if (tag.name == u"pos") {
                if (a.size() >= 2 && finite(a[0]) && finite(a[1]))
                    decided = true;
                if (decided)
                    break;
                continue;
            }
            if (a.size() < 4 || !finite(a[0]) || !finite(a[1]) || !finite(a[2]) || !finite(a[3]))
                continue;
            decided = true;
            if (!movedFrom)
                break;
            const double dx = t.pos.x - movedFrom->x, dy = t.pos.y - movedFrom->y;
            u16 move = u"\\move(" + fmt(a[0] + dx, 2) + u"," + fmt(a[1] + dy, 2) + u"," + fmt(a[2] + dx, 2) + u"," +
                       fmt(a[3] + dy, 2);
            if (a.size() >= 6)
                move += u"," + fmt(a[4], 0) + u"," + fmt(a[5], 0);
            move += u")";
            out.replace(open + 1 + tag.from, tag.to - tag.from, move);
            moved = true;
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
    tags += u"\\org(" + fmt(t.org.x, 2) + u"," + fmt(t.org.y, 2) + u")";
    put(u"fax", t.fax, 6, 0);
    put(u"fscx", t.fscx, 2, transform::styleNumber(style.scaleX, 100));
    put(u"fscy", t.fscy, 2, transform::styleNumber(style.scaleY, 100));
    put(u"frz", t.frz, 4, transform::styleNumber(style.angle, 0));
    put(u"frx", t.frx, 4, 0);
    put(u"fry", t.fry, 4, 0);
    const double bord = transform::styleNumber(style.outlineWidth, 0);
    const double shad = transform::styleNumber(style.shadow, 0);
    put(u"xbord", t.bord.x, 2, bord);
    put(u"ybord", t.bord.y, 2, bord);
    put(u"xshad", t.shad.x, 2, shad);
    put(u"yshad", t.shad.y, 2, shad);

    // Every leading block loses the tags written (the last block first, so
    // the earlier ones' places hold); the target block takes the new ones.
    static constexpr u16v kWritten[] = {u"fax", u"fay",  u"fscx", u"fscy",  u"frx",   u"fry",   u"frz",  u"fr",
                                        u"bord", u"xbord", u"ybord", u"shad", u"xshad", u"yshad", u"pos", u"org"};
    for (std::size_t b = leading.size(); b-- > 0;) {
        const auto [open, close] = leading[b];
        const u16v block = u16v(out).substr(open + 1, close - open - 1);
        u16 kept;
        std::size_t insertAt = u16::npos;
        for (const Tag &tag : tagsOf(block)) {
            if (std::find(std::begin(kWritten), std::end(kWritten), tag.name) != std::end(kWritten))
                continue;
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
        // Text before the block's first backslash (a comment) stays first.
        const std::size_t firstTag = block.find(u'\\');
        const u16 lead(block.substr(0, firstTag == u16v::npos ? block.size() : firstTag));
        if (b != target && lead.empty() && kept.empty())
            out.erase(open, close - open + 1); // emptied
        else
            out.replace(open + 1, close - open - 1, lead + kept);
    }
    return out;
}

} // namespace perspective

// --------------------------------------------------------------- the tool

using namespace perspective;

namespace {

constexpr std::uint32_t kInnerColour = 0xFFBB0000;
constexpr std::uint32_t kOuterColour = 0xAAFF0000;
constexpr std::uint32_t kGridColour = 0x55FF0000;
constexpr std::uint32_t kOrgColour = 0xFFFF8800;
constexpr float kHit = 8.f; // device pixels, as the other tools' handles

PointF add(PointF a, PointF b) { return {a.x + b.x, a.y + b.y}; }
PointF sub(PointF a, PointF b) { return {a.x - b.x, a.y - b.y}; }
bool near(PointF a, PointF b) { return std::abs(a.x - b.x) < kHit && std::abs(a.y - b.y) < kHit; }

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
    const transform::Context ctx = transform::context(host);
    m_valid = false;
    if (!ctx.active) {
        m_line.reset();
        host.toolChanged();
        return;
    }
    const bool newLine = m_line != ctx.active->id;
    if (newLine) {
        m_selected = -1;
        m_line = ctx.active->id;
        m_c1 = {0.25f, 0.25f};
        m_c2 = {0.75f, 0.75f};
    }
    m_time = host.videoTimeMs();
    if (const auto state = readLine(host, *ctx.active)) {
        m_state = *state;
        m_screenZ = screenZ(host.view().scriptHeight(), layoutHeight(host));
        m_inner = textToQuad(m_state.transform, m_state.box, m_screenZ);
        m_valid = convex(m_inner);
    }
    if (newLine)
        restorePlane();
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
    if (std::isfinite(c1.x) && std::isfinite(c1.y) && std::isfinite(c2.x) && std::isfinite(c2.y) && c1.x < c2.x &&
        c1.y < c2.y) {
        m_c1 = c1;
        m_c2 = c2;
    }
}

void PerspectiveTool::follow(const VisualHost &host) const
{
    // The controller only redraws when the time changes; the position read
    // is the time's, so it is read again (not during an edit, which works
    // from where it began).
    if (!m_valid || !m_state.hasMove || host.videoTimeMs() == m_time || m_drag != Drag::None || m_valueEdit ||
        m_keyEdit)
        return;
    m_time = host.videoTimeMs();
    const transform::Context ctx = transform::context(host);
    if (!ctx.active)
        return;
    if (const auto state = readLine(host, *ctx.active)) {
        const Quad inner = textToQuad(state->transform, state->box, m_screenZ);
        if (convex(inner)) {
            m_state = *state;
            m_inner = inner;
        }
    }
}

bool PerspectiveTool::beginEdit(VisualHost &host)
{
    if (host.gesture())
        return true;
    const auto targets = host.batchTargets();
    if (targets.empty())
        return false;
    return host.beginGesture(targets, std::string(familyInfo(Family::Perspective).history)).has_value();
}

void PerspectiveTool::stageAll(VisualHost &host)
{
    Gesture *g = host.gesture();
    if (!g)
        return;
    const transform::Context ctx = transform::context(host);
    for (const core::LineId &id : g->targets()) {
        const core::LineRecord &line = g->before(id);
        // Each target's own outline and shadow, scaled as the active
        // Line's; its \move moved by the active Line's change of position.
        Transform t = m_state.transform;
        const LineState own = readTags(host, line);
        const double kx = m_startTransform.fscx != 0 ? t.fscx / m_startTransform.fscx : 1;
        const double ky = m_startTransform.fscy != 0 ? t.fscy / m_startTransform.fscy : 1;
        t.bord = {static_cast<float>(own.transform.bord.x * kx), static_cast<float>(own.transform.bord.y * ky)};
        t.shad = {static_cast<float>(own.transform.shad.x * kx), static_cast<float>(own.transform.shad.y * ky)};
        const u16 text = transform::lineText(line);
        const core::StyleValues style = transform::lineStyle(ctx, line.style);
        g->stage(id, u8(writeTransform(text, t, style, m_startTransform.pos)), transform::editsTranslation(line));
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
    // A release that changes no Line records nothing (T3-release-no-change).
    if (!host.gesture()->changesAnyLine()) {
        host.cancelGesture();
        reset(host);
        return false;
    }
    rememberPlane(host);
    const bool committed = host.commitGesture().has_value();
    if (!committed)
        reset(host);
    return committed;
}

bool PerspectiveTool::applyInner(const Quad &inner, VisualHost &host)
{
    (void)host;
    if (!convex(inner))
        return false;
    Transform from = m_startTransform;
    from.org = m_state.transform.org;
    const auto t = quadToTransform(inner, m_state.box, m_orgMode, from, m_screenZ);
    if (!t)
        return false;
    m_inner = inner;
    m_state.transform = *t;
    return true;
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
        inner = m_startInner;
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
    if (m_plane && !convex(outer())) {
        m_c1 = keepC1;
        m_c2 = keepC2;
        return false;
    }
    if (!applyInner(inner, host)) {
        m_c1 = keepC1;
        m_c2 = keepC2;
        return false;
    }
    return true;
}

void PerspectiveTool::move(PointF delta, VisualHost &host)
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
        }
        return;
    }
    Quad inner = m_startInner;
    for (PointF &p : inner)
        p = add(p, delta);
    (void)applyInner(inner, host);
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
        if (m_orgMode == OrgMode::Keep && near(dev, view.scriptToView(m_state.transform.org))) {
            m_drag = Drag::Org;
        }
        for (int i = 0; i < 4 && m_drag == Drag::None; ++i) {
            if (near(dev, view.scriptToView(m_inner[static_cast<std::size_t>(i)]))) {
                m_drag = Drag::Inner;
                m_corner = i;
            }
        }
        for (int i = 0; i < 4 && m_drag == Drag::None && m_plane; ++i) {
            if (near(dev, view.scriptToView(o[static_cast<std::size_t>(i)]))) {
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
        if (applyCorner(m_corner, at, false, host))
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
            changed = applyCorner(m_corner, to, event.alt, host);
            break;
        }
        case Drag::Org: {
            PointF to = add(m_startOrg, delta);
            if (event.control)
                to = snap(to, 8);
            m_state.transform.org = to;
            changed = applyInner(m_inner, host);
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
            move(delta, host);
            changed = true;
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
    if (m_selected >= 0) {
        const Quad q = startQuad(m_selected >= 4);
        (void)applyCorner(m_selected, add(q[static_cast<std::size_t>(m_selected % 4)], delta), false, host);
    } else {
        m_drag = Drag::Plane;
        move(delta, host);
        m_drag = Drag::None;
    }
    stageAll(host);
    return true;
}

Overlay PerspectiveTool::overlay(const VisualHost &host) const
{
    follow(host);
    Overlay out;
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
    if (m_plane) {
        const PointF loop[5] = {o[0], o[1], o[2], o[3], o[0]};
        drawDashedLine(out, loop, 5, 6, kOuterColour);
    }
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
        if (!applyInner(m_startInner, host)) {
            m_state.transform.org = m_startOrg;
            return refuse();
        }
    } else {
        // An angle or the shear: the quad follows the tags.
        Transform t = m_startTransform;
        (name == "frx" ? t.frx : name == "fry" ? t.fry : name == "frz" ? t.frz : t.fax) = value;
        const Quad q = textToQuad(t, m_state.box, m_screenZ);
        if (!convex(q))
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
        m_plane = value != 0;
        if (m_plane)
            restorePlane();
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
