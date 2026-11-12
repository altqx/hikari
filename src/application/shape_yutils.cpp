#include "hikari/application/shape_kernel.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <numbers>

namespace hikari::application::shape::yutils {

namespace {

constexpr double kFpPrecision = 1000; // 10^FP_PRECISION
constexpr double kMiterLimit = 200;
constexpr double kMaxCircumference = 1.5;
constexpr int kSupersampling = 8;
constexpr double kMaxImagePixels = 1 << 28;

struct Unsupported {};

bool isLuaSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

struct Token {
    std::size_t start = 0, end = 0; // [start, end)
};

std::optional<Token> nextToken(std::string_view s, std::size_t from)
{
    while (from < s.size() && isLuaSpace(s[from]))
        ++from;
    if (from >= s.size())
        return std::nullopt;
    std::size_t end = from;
    while (end < s.size() && !isLuaSpace(s[end]))
        ++end;
    return Token{from, end};
}

bool lowerEquals(std::string_view s, std::string_view lower)
{
    if (s.size() != lower.size())
        return false;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((s[i] | 0x20) != lower[i])
            return false;
    return true;
}

// tonumber(token) for plain decimals; nullopt for a word tonumber rejects. Throws Unsupported for what
// LuaJIT reads as a number in ways not reproduced: hex, exponents, a sign of +, inf, nan, -0.
std::optional<double> luaNumber(std::string_view token)
{
    std::size_t i = token.starts_with('-') ? 1 : 0;
    std::size_t digits = 0, dots = 0;
    bool plain = i < token.size();
    for (std::size_t k = i; k < token.size(); ++k) {
        const char c = token[k];
        if (c >= '0' && c <= '9')
            ++digits;
        else if (c == '.')
            ++dots;
        else
            plain = false;
    }
    if (plain && digits > 0 && dots <= 1) {
        double value = 0;
        const char *first = token.data() + i;
        const auto [end, ec] = std::from_chars(first, token.data() + token.size(), value);
        if (ec != std::errc{} || end != token.data() + token.size() || !std::isfinite(value))
            throw Unsupported{};
        if (i == 1) {
            if (value == 0)
                throw Unsupported{};
            value = -value;
        }
        return value;
    }
    if (digits > 0)
        throw Unsupported{};
    std::string_view word = token;
    if (word.starts_with('-') || word.starts_with('+'))
        word.remove_prefix(1);
    if (lowerEquals(word, "inf") || lowerEquals(word, "infinity") || lowerEquals(word, "nan"))
        throw Unsupported{};
    return std::nullopt;
}

// tostring(number): LuaJIT's "%.14g".
void appendNumber(std::string &out, double value)
{
    if (!std::isfinite(value))
        throw Unsupported{};
    char buffer[32];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::general, 14);
    out.append(buffer, end);
}

std::string numberText(double value)
{
    std::string s;
    appendNumber(s, value);
    return s;
}

double round3(double x)
{
    return std::floor(x * kFpPrecision + 0.5) / kFpPrecision;
}

double roundInt(double x)
{
    return std::floor(x + 0.5);
}

double luaMod(double a, double b)
{
    return a - std::floor(a / b) * b;
}

double deg(double x)
{
    return x * (180.0 / std::numbers::pi);
}

double rad(double x)
{
    return x * (std::numbers::pi / 180.0);
}

double distance3(double x, double y, double z)
{
    return std::sqrt(x * x + y * y + z * z);
}

double degree(double x1, double y1, double z1, double x2, double y2, double z2)
{
    const double d = deg(std::acos((x1 * x2 + y1 * y2 + z1 * z2) / (distance3(x1, y1, z1) * distance3(x2, y2, z2))));
    return (x1 * y2 - y1 * x2) < 0 ? -d : d;
}

std::array<double, 2> ortho(double x1, double y1, double z1, double x2, double y2, double z2)
{
    return {y1 * z2 - z1 * y2, z1 * x2 - x1 * z2};
}

std::array<double, 2> stretch(double x, double y, double z, double length)
{
    const double current = distance3(x, y, z);
    if (current == 0)
        return {0, 0};
    const double factor = length / current;
    return {x * factor, y * factor};
}

std::array<double, 2> rotate2d(double x, double y, double angle)
{
    const double ra = rad(angle);
    return {std::cos(ra) * x - std::sin(ra) * y, std::sin(ra) * x + std::cos(ra) * y};
}

struct Intersection {
    enum Kind { None, Point, Outside } kind = None; // Outside: strict and off the segments (Lua's 1/0)
    double x = 0, y = 0;
};
Intersection lineIntersect(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                           bool strict)
{
    const double x10 = x0 - x1, y10 = y0 - y1, x32 = x2 - x3, y32 = y2 - y3;
    if ((x10 == 0 && y10 == 0) || (x32 == 0 && y32 == 0))
        throw Unsupported{}; // Yutils raises "lines mustn't have zero length"
    const double det = x10 * y32 - y10 * x32;
    if (det == 0)
        return {};
    const double pre = x0 * y1 - y0 * x1, post = x2 * y3 - y2 * x3;
    const double ix = (pre * x32 - x10 * post) / det, iy = (pre * y32 - y10 * post) / det;
    if (strict) {
        const double s = x10 != 0 ? (ix - x1) / x10 : (iy - y1) / y10;
        const double t = x32 != 0 ? (ix - x3) / x32 : (iy - y3) / y32;
        if (s < 0 || s > 1 || t < 0 || t > 1)
            return {Intersection::Outside};
    }
    return {Intersection::Point, ix, iy};
}

// Yutils.shape.filter. The callback gets x, y and the point's type word (empty for none); it returns
// the point's new coordinates, or nullopt to leave its text alone.
using Filter = std::function<std::optional<std::array<double, 2>>(double, double, std::string_view)>;
std::string filter(std::string shape, const Filter &callback)
{
    std::size_t pos = 0;
    std::optional<std::size_t> pointStart;
    std::string typ;
    bool hasType = false;
    std::optional<double> x;
    while (const auto token = nextToken(shape, pos)) {
        std::size_t tokenEnd = token->end;
        const std::string_view text = std::string_view(shape).substr(token->start, token->end - token->start);
        const auto number = luaNumber(text);
        if (!number) {
            pointStart = token->start;
            typ = std::string(text);
            hasType = true;
            x.reset();
        } else if (!x) {
            if (!pointStart)
                pointStart = token->start;
            x = *number;
        } else {
            if (const auto moved = callback(*x, *number, hasType ? std::string_view(typ) : std::string_view())) {
                std::string point;
                if (hasType) {
                    point = typ;
                    point += ' ';
                }
                appendNumber(point, round3((*moved)[0]));
                point += ' ';
                appendNumber(point, round3((*moved)[1]));
                shape.replace(*pointStart, token->end - *pointStart, point);
                tokenEnd = *pointStart + point.size();
            }
            pointStart.reset();
            hasType = false;
            typ.clear();
            x.reset();
        }
        pos = tokenEnd;
    }
    return shape;
}

std::optional<Bounds> boundsOf(const std::string &shape)
{
    std::optional<Bounds> b;
    filter(shape, [&](double x, double y, std::string_view) -> std::optional<std::array<double, 2>> {
        if (b)
            *b = {std::min(b->x0, x), std::min(b->y0, y), std::max(b->x1, x), std::max(b->y1, y)};
        else
            b = Bounds{x, y, x, y};
        return std::nullopt;
    });
    return b;
}

std::string moveText(std::string shape, double dx, double dy)
{
    return filter(std::move(shape), [&](double x, double y, std::string_view) {
        return std::optional<std::array<double, 2>>({x + dx, y + dy});
    });
}

using Curve = std::array<double, 8>;

bool curveIsFlat(const Curve &c, double tolerance)
{
    std::array<std::array<double, 2>, 3> vecs{};
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const double vx = c[2 * i + 2] - c[2 * i], vy = c[2 * i + 3] - c[2 * i + 1];
        if (!(vx == 0 && vy == 0))
            vecs[n++] = {vx, vy};
    }
    for (int i = 1; i < n; ++i)
        if (std::abs(degree(vecs[i - 1][0], vecs[i - 1][1], 0, vecs[i][0], vecs[i][1], 0)) > tolerance)
            return false;
    return true;
}

void curveToLines(const Curve &c, double tolerance, std::vector<double> &points)
{
    if (curveIsFlat(c, tolerance)) {
        points.push_back(c[6]);
        points.push_back(c[7]);
        return;
    }
    constexpr double pct = 0.5;
    const auto [x0, y0, x1, y1, x2, y2, x3, y3] = c;
    const double x01 = (x0 + x1) * pct, y01 = (y0 + y1) * pct, x12 = (x1 + x2) * pct, y12 = (y1 + y2) * pct,
                 x23 = (x2 + x3) * pct, y23 = (y2 + y3) * pct;
    const double x012 = (x01 + x12) * pct, y012 = (y01 + y12) * pct, x123 = (x12 + x23) * pct,
                 y123 = (y12 + y23) * pct;
    const double x0123 = (x012 + x123) * pct, y0123 = (y012 + y123) * pct;
    curveToLines({x0, y0, x01, y01, x012, y012, x0123, y0123}, tolerance, points);
    curveToLines({x0123, y0123, x123, y123, x23, y23, x3, y3}, tolerance, points);
}

std::string flattenText(std::string shape, double tolerance)
{
    std::size_t curvesStart = 0;
    for (;;) {
        // shape:find("([^%s]+)%s+([^%s]+)%s+b%s+", curves_start)
        std::optional<Token> t1, t2;
        std::size_t curvesEnd = 0;
        bool found = false;
        for (auto a = nextToken(shape, curvesStart); a; a = nextToken(shape, a->end)) {
            const auto b = nextToken(shape, a->end);
            if (!b)
                break;
            const auto c = nextToken(shape, b->end);
            if (!c)
                break;
            if (c->end - c->start == 1 && shape[c->start] == 'b' && c->end < shape.size()) {
                t1 = a;
                t2 = b;
                curvesEnd = c->end;
                while (curvesEnd < shape.size() && isLuaSpace(shape[curvesEnd]))
                    ++curvesEnd;
                found = true;
                break;
            }
        }
        if (!found)
            return shape;
        const auto px0 = luaNumber(std::string_view(shape).substr(t1->start, t1->end - t1->start));
        const auto py0 = luaNumber(std::string_view(shape).substr(t2->start, t2->end - t2->start));
        if (!px0 || !py0)
            return shape;
        double x0 = *px0, y0 = *py0;
        shape[shape.find('b', t1->start)] = 'l';
        std::size_t curveStart = curvesEnd;
        for (;;) {
            std::array<Token, 6> tokens;
            std::size_t from = curveStart;
            bool six = true;
            for (auto &t : tokens) {
                const auto next = nextToken(shape, from);
                if (!next) {
                    six = false;
                    break;
                }
                t = *next;
                from = next->end;
            }
            if (!six)
                break;
            std::array<double, 6> v{};
            bool numbers = true;
            for (int i = 0; i < 6; ++i) {
                const auto n = luaNumber(std::string_view(shape).substr(tokens[i].start, tokens[i].end - tokens[i].start));
                if (!n)
                    numbers = false;
                else
                    v[i] = *n;
            }
            if (!numbers)
                break;
            std::vector<double> points{x0, y0};
            curveToLines({x0, y0, v[0], v[1], v[2], v[3], v[4], v[5]}, tolerance, points);
            std::string line;
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (i)
                    line += ' ';
                appendNumber(line, round3(points[i]));
            }
            const std::size_t start = tokens[0].start;
            shape.replace(start, tokens[5].end - start, line);
            x0 = v[4];
            y0 = v[5];
            curveStart = start + line.size();
        }
        curvesStart = curvesEnd;
    }
}

// Lua 5.1's auxsort (lib_table.c), which LuaJIT keeps: table.sort is not stable, and to_pixels' row
// stops depend on the order it leaves equal positions in.
template <typename T, typename Less>
void luaSort(std::vector<T> &a, Less less)
{
    const auto at = [&](int i) -> T & { return a[static_cast<std::size_t>(i - 1)]; };
    const std::function<void(int, int)> aux = [&](int l, int u) {
        while (l < u) {
            if (less(at(u), at(l)))
                std::swap(at(l), at(u));
            if (u - l == 1)
                break;
            int i = (l + u) / 2;
            if (less(at(i), at(l)))
                std::swap(at(i), at(l));
            else if (less(at(u), at(i)))
                std::swap(at(i), at(u));
            if (u - l == 2)
                break;
            const T pivot = at(i);
            std::swap(at(i), at(u - 1));
            i = l;
            int j = u - 1;
            for (;;) {
                while (less(at(++i), pivot))
                    if (i > u)
                        throw Unsupported{};
                while (less(pivot, at(--j)))
                    if (j < l)
                        throw Unsupported{};
                if (j < i)
                    break;
                std::swap(at(i), at(j));
            }
            std::swap(at(u - 1), at(i));
            if (i - l < u - i) {
                j = l;
                i = i - 1;
                l = i + 2;
            } else {
                j = i + 1;
                i = u;
                u = j - 2;
            }
            aux(j, i);
        }
    };
    aux(1, static_cast<int>(a.size()));
}

template <typename F>
auto guarded(F f) -> std::optional<decltype(f())>
{
    try {
        return f();
    } catch (const Unsupported &) {
        return std::nullopt;
    }
}

}

std::optional<Bounding> bounding(std::string_view shape)
{
    return guarded([&] { return Bounding{boundsOf(std::string(shape))}; });
}

std::optional<std::string> move(std::string_view shape, double x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;
    return guarded([&] { return moveText(std::string(shape), x, y); });
}

std::optional<std::string> transform(std::string_view shape, const Matrix &m)
{
    for (const double v : m)
        if (!std::isfinite(v))
            return std::nullopt;
    return guarded([&] {
        return filter(std::string(shape), [&](double x, double y, std::string_view) {
            constexpr double z = 0, w = 1;
            const double tx = x * m[0] + y * m[4] + z * m[8] + w * m[12];
            const double ty = x * m[1] + y * m[5] + z * m[9] + w * m[13];
            const double tw = x * m[3] + y * m[7] + z * m[11] + w * m[15];
            return std::optional<std::array<double, 2>>({tx / tw, ty / tw});
        });
    });
}

std::optional<std::string> flatten(std::string_view shape, double curveTolerance)
{
    if (!std::isfinite(curveTolerance))
        return std::nullopt;
    return guarded([&] { return flattenText(std::string(shape), curveTolerance); });
}

std::optional<std::string> toOutline(std::string_view shape, double widthXY, std::optional<double> widthY,
                                     OutlineMode mode)
{
    if (!std::isfinite(widthXY) || (widthY && !std::isfinite(*widthY)))
        return std::nullopt;
    if ((widthY && (widthXY < 0 || *widthY < 0 || !(widthXY > 0 || *widthY > 0))) || widthXY <= 0)
        return std::nullopt;
    return guarded([&] {
        double width = widthXY, xscale = 1, yscale = 1;
        if (widthY && widthXY != *widthY) {
            width = std::max(widthXY, *widthY);
            xscale = widthXY / width;
            yscale = *widthY / width;
        }
        using P = std::array<double, 2>;
        std::vector<std::vector<P>> figures;
        std::vector<P> figure;
        std::optional<P> lastMove;
        const auto closeFigure = [&] {
            if (figure.size() > 2) {
                if (lastMove && figure.back() == *lastMove)
                    figure.pop_back();
                figures.push_back(figure);
            }
        };
        filter(std::string(shape), [&](double x, double y, std::string_view typ) -> std::optional<P> {
            if (!typ.empty() && typ != "m" && typ != "l")
                throw Unsupported{}; // Yutils raises: only moves and lines
            if (!lastMove || typ == "m") {
                closeFigure();
                figure.clear();
                lastMove = P{x, y};
            }
            if (figure.empty() || !(figure.back()[0] == x && figure.back()[1] == y))
                figure.push_back({x, y});
            return std::nullopt;
        });
        closeFigure();

        std::string stroke;
        bool firstOutline = true;
        for (const auto &fig : figures) {
            const int count = static_cast<int>(fig.size());
            for (int pass = 1; pass <= 2; ++pass) {
                std::vector<std::string> outline;
                const auto add = [&](std::initializer_list<double> values, bool lineMark) {
                    std::string s;
                    const std::size_t n = outline.size() + 1;
                    if (n == 1 && !lineMark)
                        s = "m ";
                    else if (n == 2)
                        s = "l ";
                    bool first = true;
                    for (const double v : values) {
                        if (!first)
                            s += ' ';
                        first = false;
                        appendNumber(s, round3(v));
                    }
                    outline.push_back(std::move(s));
                };
                const int begin = pass == 1 ? count : 1, end = pass == 1 ? 1 : count, step = pass == 1 ? -1 : 1;
                for (int pi = begin; step > 0 ? pi <= end : pi >= end; pi += step) {
                    const auto fp = [&](int i) { return fig[static_cast<std::size_t>(i - 1)]; };
                    const P point = fp(pi);
                    P pre, post;
                    if (pass == 1) {
                        if (pi == 1) {
                            pre = fp(pi + 1);
                            post = fp(count);
                        } else if (pi == count) {
                            pre = fp(1);
                            post = fp(pi - 1);
                        } else {
                            pre = fp(pi + 1);
                            post = fp(pi - 1);
                        }
                    } else {
                        if (pi == 1) {
                            pre = fp(count);
                            post = fp(pi + 1);
                        } else if (pi == count) {
                            pre = fp(pi - 1);
                            post = fp(1);
                        } else {
                            pre = fp(pi - 1);
                            post = fp(pi + 1);
                        }
                    }
                    double vec1x = point[0] - pre[0], vec1y = point[1] - pre[1], vec2x = point[0] - post[0],
                           vec2y = point[1] - post[1];
                    auto [o1x, o1y] = ortho(vec1x, vec1y, 0, 0, 0, 1);
                    std::tie(o1x, o1y) = std::tuple_cat(stretch(o1x, o1y, 0, width));
                    auto [o2x, o2y] = ortho(vec2x, vec2y, 0, 0, 0, -1);
                    std::tie(o2x, o2y) = std::tuple_cat(stretch(o2x, o2y, 0, width));
                    const auto gap = lineIntersect(point[0] + o1x - vec1x, point[1] + o1y - vec1y, point[0] + o1x,
                                                   point[1] + o1y, point[0] + o2x - vec2x, point[1] + o2y - vec2y,
                                                   point[0] + o2x, point[1] + o2y, true);
                    if (gap.kind == Intersection::Point) {
                        add({point[0] + (gap.x - point[0]) * xscale, point[1] + (gap.y - point[1]) * yscale}, false);
                        continue;
                    }
                    add({point[0] + o1x * xscale, point[1] + o1y * yscale}, false);
                    if (mode == OutlineMode::Miter) {
                        const auto is = lineIntersect(point[0] + o1x - vec1x, point[1] + o1y - vec1y, point[0] + o1x,
                                                      point[1] + o1y, point[0] + o2x - vec2x, point[1] + o2y - vec2y,
                                                      point[0] + o2x, point[1] + o2y, false);
                        if (is.kind == Intersection::Point) {
                            const double ivx = is.x - point[0], ivy = is.y - point[1];
                            const double len = std::sqrt(ivx * ivx + ivy * ivy);
                            if (len > kMiterLimit) {
                                const double fix = kMiterLimit / len;
                                add({point[0] + (o1x + (ivx - o1x) * fix) * xscale,
                                     point[1] + (o1y + (ivy - o1y) * fix) * yscale,
                                     point[0] + (o2x + (ivx - o2x) * fix) * xscale,
                                     point[1] + (o2y + (ivy - o2y) * fix) * yscale},
                                    true);
                            } else {
                                add({point[0] + ivx * xscale, point[1] + ivy * yscale}, true);
                            }
                        } else {
                            std::tie(vec1x, vec1y) = std::tuple_cat(stretch(vec1x, vec1y, 0, kMiterLimit));
                            std::tie(vec2x, vec2y) = std::tuple_cat(stretch(vec2x, vec2y, 0, kMiterLimit));
                            add({point[0] + (o1x + vec1x) * xscale, point[1] + (o1y + vec1y) * yscale,
                                 point[0] + (o2x + vec2x) * xscale, point[1] + (o2y + vec2y) * yscale},
                                true);
                        }
                    } else if (mode == OutlineMode::Round) {
                        const double d = degree(o1x, o1y, 0, o2x, o2y, 0);
                        const double circ = std::abs(rad(d)) * width;
                        if (circ > kMaxCircumference) {
                            const double rest = luaMod(circ, kMaxCircumference);
                            const double limit = circ - kMaxCircumference;
                            for (double cur = rest > 0 ? rest : kMaxCircumference; cur <= limit;
                                 cur += kMaxCircumference) {
                                const auto [cx, cy] = rotate2d(o1x, o1y, cur / circ * d);
                                add({point[0] + cx * xscale, point[1] + cy * yscale}, true);
                            }
                        }
                    }
                    add({point[0] + o2x * xscale, point[1] + o2y * yscale}, true);
                }
                if (!firstOutline)
                    stroke += ' ';
                firstOutline = false;
                for (std::size_t i = 0; i < outline.size(); ++i) {
                    if (i)
                        stroke += ' ';
                    stroke += outline[i];
                }
            }
        }
        return stroke;
    });
}

std::optional<std::vector<Pixel>> toPixels(std::string_view input)
{
    return guarded([&] {
        constexpr double upscale = kSupersampling;
        constexpr double downscale = 1 / upscale;
        std::string shape = filter(std::string(input), [&](double x, double y, std::string_view) {
            return std::optional<std::array<double, 2>>({x * upscale, y * upscale});
        });
        const auto box = boundsOf(shape);
        if (!box)
            throw Unsupported{}; // Yutils raises "not enough shape points"
        const double shiftX = -(box->x0 - luaMod(box->x0, upscale)), shiftY = -(box->y0 - luaMod(box->y0, upscale));
        shape = moveText(std::move(shape), shiftX, shiftY);

        const double widthD = std::ceil((box->x1 + shiftX) * downscale) * upscale;
        const double heightD = std::ceil((box->y1 + shiftY) * downscale) * upscale;
        if (!(widthD >= 0 && heightD >= 0) || widthD * heightD > kMaxImagePixels)
            throw Unsupported{};
        const auto width = static_cast<long long>(widthD), height = static_cast<long long>(heightD);
        std::vector<char> image(static_cast<std::size_t>(width * height), 0);

        struct Line {
            double x, y, vx, vy;
        };
        std::vector<Line> lines;
        std::optional<std::array<double, 2>> lastPoint, lastMove;
        const auto offImage = [&](double a, double b) { return (a < 0 && b < 0) || (a > heightD && b > heightD); };
        const auto closeFigure = [&] {
            if (lastMove && (*lastMove)[1] != (*lastPoint)[1] && !offImage((*lastPoint)[1], (*lastMove)[1]))
                lines.push_back({(*lastPoint)[0], (*lastPoint)[1], (*lastMove)[0] - (*lastPoint)[0],
                                 (*lastMove)[1] - (*lastPoint)[1]});
        };
        filter(flattenText(shape, 1), [&](double x, double y, std::string_view typ) -> std::optional<std::array<double, 2>> {
            x = roundInt(x);
            y = roundInt(y);
            if (typ == "m") {
                closeFigure();
                lastMove = std::array<double, 2>{x, y};
            } else if (lastPoint && (*lastPoint)[1] != y && !offImage((*lastPoint)[1], y)) {
                lines.push_back({(*lastPoint)[0], (*lastPoint)[1], x - (*lastPoint)[0], y - (*lastPoint)[1]});
            }
            lastPoint = std::array<double, 2>{x, y};
            return std::nullopt;
        });
        closeFigure();

        const auto moved = boundsOf(shape);
        struct Stop {
            double x;
            int direction;
        };
        const long long rowFirst = static_cast<long long>(std::max(std::floor(moved->y0), 0.0));
        const long long rowEnd = static_cast<long long>(std::min(std::ceil(moved->y1), heightD));
        for (long long y = rowFirst; y < rowEnd; ++y) {
            std::vector<Stop> stops;
            const double scan = static_cast<double>(y) + 0.5;
            for (const Line &l : lines) {
                if (l.vy == 0)
                    continue;
                const double s = (scan - l.y) / l.vy;
                if (s >= 0 && s <= 1) {
                    const double cx = l.x + s * l.vx;
                    stops.push_back({cx < 0 ? 0 : cx > widthD ? widthD : cx, l.vy > 0 ? 1 : -1});
                }
            }
            if (stops.size() < 2)
                continue;
            luaSort(stops, [](const Stop &a, const Stop &b) { return a.x < b.x; });
            int status = 0;
            for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
                status += stops[i].direction;
                if (status == 0)
                    continue;
                const long long from = static_cast<long long>(std::ceil(stops[i].x - 0.5));
                const long long to = static_cast<long long>(std::floor(stops[i + 1].x + 0.5));
                for (long long x = from; x < to; ++x) {
                    const long long index = y * width + x;
                    if (index >= 0 && index < width * height)
                        image[static_cast<std::size_t>(index)] = 1;
                }
            }
        }

        std::vector<Pixel> pixels;
        const auto up = static_cast<long long>(upscale);
        for (long long y = 0; y <= height - up; y += up) {
            for (long long x = 0; x <= width - up; x += up) {
                double opacity = 0;
                for (long long yy = 0; yy < up; ++yy)
                    for (long long xx = 0; xx < up; ++xx)
                        if (image[static_cast<std::size_t>((y + yy) * width + (x + xx))])
                            opacity += 255;
                if (opacity > 0)
                    pixels.push_back({opacity * (downscale * downscale), (static_cast<double>(x) - shiftX) * downscale,
                                      (static_cast<double>(y) - shiftY) * downscale});
            }
        }
        return pixels;
    });
}

}
