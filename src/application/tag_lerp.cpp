#include "hikari/application/tag_lerp.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace hikari::application::lerp {
namespace {

struct Name {
    std::string_view name;
    TagSpec spec;
};

constexpr Name kNames[] = {
    {"c", {"1c", Kind::Colour}},     {"1c", {"1c", Kind::Colour}},     {"2c", {"2c", Kind::Colour}},
    {"3c", {"3c", Kind::Colour}},    {"4c", {"4c", Kind::Colour}},     {"alpha", {"alpha", Kind::Alpha}},
    {"1a", {"1a", Kind::Alpha}},     {"2a", {"2a", Kind::Alpha}},      {"3a", {"3a", Kind::Alpha}},
    {"4a", {"4a", Kind::Alpha}},     {"fs", {"fs", Kind::Number}},     {"fsp", {"fsp", Kind::Number}},
    {"fscx", {"fscx", Kind::Number}}, {"fscy", {"fscy", Kind::Number}}, {"bord", {"bord", Kind::Number}},
    {"xbord", {"xbord", Kind::Number}}, {"ybord", {"ybord", Kind::Number}}, {"shad", {"shad", Kind::Number}},
    {"xshad", {"xshad", Kind::Number}}, {"yshad", {"yshad", Kind::Number}}, {"blur", {"blur", Kind::Number}},
    {"be", {"be", Kind::Number}},    {"fax", {"fax", Kind::Number}},   {"fay", {"fay", Kind::Number}},
    {"fr", {"frz", Kind::Number}},   {"frz", {"frz", Kind::Number}},   {"frx", {"frx", Kind::Number}},
    {"fry", {"fry", Kind::Number}},  {"pos", {"pos", Kind::Point}},    {"org", {"org", Kind::Point}},
    {"clip", {"clip", Kind::Rect}},
};

std::string_view trimmed(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

std::optional<double> number(std::string_view s)
{
    s = trimmed(s);
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    double out = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc() || end == s.data())
        return std::nullopt;
    return out;
}

// libass reads "&H", then hex digits up to anything else.
std::optional<unsigned long> hex(std::string_view s)
{
    s = trimmed(s);
    while (!s.empty() && (s.front() == '&' || s.front() == 'H' || s.front() == 'h'))
        s.remove_prefix(1);
    unsigned long out = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out, 16);
    if (ec != std::errc() || end == s.data())
        return std::nullopt;
    return out;
}

std::optional<std::vector<double>> numbers(std::string_view args, std::size_t count)
{
    args = trimmed(args);
    if (args.size() < 2 || args.front() != '(' || args.back() != ')')
        return std::nullopt;
    args = args.substr(1, args.size() - 2);
    std::vector<double> out;
    while (true) {
        const auto comma = args.find(',');
        const auto value = number(args.substr(0, comma));
        if (!value)
            return std::nullopt;
        out.push_back(*value);
        if (comma == std::string_view::npos)
            break;
        args.remove_prefix(comma + 1);
    }
    if (out.size() != count)
        return std::nullopt;
    return out;
}

constexpr double kInterpolationNoise = 1e-7;

double roundHalfUp(double v)
{
    return std::floor(v + 0.5 + kInterpolationNoise);
}

int byte(double v)
{
    return std::clamp(static_cast<int>(roundHalfUp(v)), 0, 255);
}

std::string trimmedNumber(double v, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    double r = roundHalfUp(std::abs(v) * scale) / scale;
    if (v < 0)
        r = -r;
    if (r == 0)
        r = 0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, r);
    std::string out = buf;
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0')
            out.pop_back();
        if (out.back() == '.')
            out.pop_back();
    }
    return out;
}

struct Hsl {
    double h = 0, s = 0, l = 0;
};

Hsl toHsl(double r, double g, double b)
{
    r /= 255;
    g /= 255;
    b /= 255;
    const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
    Hsl out;
    out.l = (hi + lo) / 2;
    const double d = hi - lo;
    if (d == 0)
        return out;
    out.s = out.l > 0.5 ? d / (2 - hi - lo) : d / (hi + lo);
    if (hi == r)
        out.h = (g - b) / d + (g < b ? 6 : 0);
    else if (hi == g)
        out.h = (b - r) / d + 2;
    else
        out.h = (r - g) / d + 4;
    out.h /= 6;
    return out;
}

double hueChannel(double p, double q, double t)
{
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0 / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3)
        return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

std::array<double, 3> fromHsl(const Hsl &c)
{
    if (c.s == 0)
        return {c.l * 255, c.l * 255, c.l * 255};
    const double q = c.l < 0.5 ? c.l * (1 + c.s) : c.l + c.s - c.l * c.s;
    const double p = 2 * c.l - q;
    return {hueChannel(p, q, c.h + 1.0 / 3) * 255, hueChannel(p, q, c.h) * 255, hueChannel(p, q, c.h - 1.0 / 3) * 255};
}

}

const TagSpec *tagSpec(std::string_view name)
{
    for (const auto &n : kNames)
        if (n.name == name)
            return &n.spec;
    return nullptr;
}

std::optional<Value> parse(const TagSpec &spec, std::string_view args)
{
    Value out;
    switch (spec.kind) {
    case Kind::Number: {
        const auto v = number(args);
        if (!v)
            return std::nullopt;
        out.v[0] = *v;
        return out;
    }
    case Kind::Colour: {
        const auto v = hex(args);
        if (!v)
            return std::nullopt;
        out.v = {static_cast<double>((*v >> 16) & 0xFF), static_cast<double>((*v >> 8) & 0xFF),
                 static_cast<double>(*v & 0xFF), 0};
        return out;
    }
    case Kind::Alpha: {
        const auto v = hex(args);
        if (!v)
            return std::nullopt;
        out.v[0] = static_cast<double>(*v & 0xFF);
        return out;
    }
    case Kind::Point:
    case Kind::Rect: {
        const auto v = numbers(args, spec.kind == Kind::Point ? 2 : 4);
        if (!v)
            return std::nullopt;
        std::copy(v->begin(), v->end(), out.v.begin());
        return out;
    }
    }
    return std::nullopt;
}

std::string format(const TagSpec &spec, const Value &value)
{
    const std::string name = "\\" + std::string(spec.key);
    char buf[64];
    switch (spec.kind) {
    case Kind::Number:
        return name + trimmedNumber(value.v[0], 2);
    case Kind::Colour:
        std::snprintf(buf, sizeof buf, "&H%02X%02X%02X&", byte(value.v[0]), byte(value.v[1]), byte(value.v[2]));
        return name + buf;
    case Kind::Alpha:
        std::snprintf(buf, sizeof buf, "&H%02X&", byte(value.v[0]));
        return name + buf;
    case Kind::Point:
        return name + "(" + trimmedNumber(value.v[0], 2) + "," + trimmedNumber(value.v[1], 2) + ")";
    case Kind::Rect:
        return name + "(" + trimmedNumber(value.v[0], 0) + "," + trimmedNumber(value.v[1], 0) + "," +
               trimmedNumber(value.v[2], 0) + "," + trimmedNumber(value.v[3], 0) + ")";
    }
    return name;
}

Value lerp(const TagSpec &spec, const Value &from, const Value &to, double t, Blend blend)
{
    Value out;
    if (spec.kind == Kind::Colour && blend == Blend::Hsl) {
        const Hsl a = toHsl(from.v[2], from.v[1], from.v[0]), b = toHsl(to.v[2], to.v[1], to.v[0]);
        double dh = b.h - a.h;
        if (dh > 0.5)
            dh -= 1;
        else if (dh < -0.5)
            dh += 1;
        Hsl mid{a.h + dh * t, a.s + (b.s - a.s) * t, a.l + (b.l - a.l) * t};
        mid.h -= std::floor(mid.h);
        const auto rgb = fromHsl(mid);
        out.v = {rgb[2], rgb[1], rgb[0], 0};
        return out;
    }
    for (std::size_t i = 0; i < out.v.size(); ++i)
        out.v[i] = from.v[i] + (to.v[i] - from.v[i]) * t;
    return out;
}

std::optional<Value> styleDefault(const TagSpec &spec, const core::StyleValues &style)
{
    const auto colour = [](const core::Colour &c) {
        return Value{{static_cast<double>(c.b), static_cast<double>(c.g), static_cast<double>(c.r), 0}};
    };
    const auto num = [](const std::u8string &text, double fallback) {
        const std::string s(text.begin(), text.end());
        return Value{{s.empty() ? fallback : std::strtod(s.c_str(), nullptr)}};
    };
    const std::string_view k = spec.key;
    if (k == "1c")
        return colour(style.primary);
    if (k == "2c")
        return colour(style.secondary);
    if (k == "3c")
        return colour(style.outline);
    if (k == "4c")
        return colour(style.back);
    if (k == "alpha" || k == "1a")
        return Value{{static_cast<double>(style.primary.a)}};
    if (k == "2a")
        return Value{{static_cast<double>(style.secondary.a)}};
    if (k == "3a")
        return Value{{static_cast<double>(style.outline.a)}};
    if (k == "4a")
        return Value{{static_cast<double>(style.back.a)}};
    if (k == "fs")
        return num(style.fontsize, 0);
    if (k == "fsp")
        return num(style.spacing, 0);
    if (k == "fscx")
        return num(style.scaleX, 100);
    if (k == "fscy")
        return num(style.scaleY, 100);
    if (k == "bord" || k == "xbord" || k == "ybord")
        return num(style.outlineWidth, 0);
    if (k == "shad" || k == "xshad" || k == "yshad")
        return num(style.shadow, 0);
    if (k == "frz")
        return num(style.angle, 0);
    if (spec.kind == Kind::Number)
        return Value{};
    return std::nullopt;
}

std::optional<Drawing> parseDrawing(std::string_view args)
{
    args = trimmed(args);
    if (args.size() < 2 || args.front() != '(' || args.back() != ')')
        return std::nullopt;
    args = trimmed(args.substr(1, args.size() - 2));
    Drawing out;
    if (const auto comma = args.find(','); comma != std::string_view::npos) {
        const auto scale = number(args.substr(0, comma));
        if (!scale || *scale < 1 || *scale != std::floor(*scale))
            return std::nullopt;
        out.scale = static_cast<int>(*scale);
        args.remove_prefix(comma + 1);
    }
    std::size_t i = 0;
    while (i < args.size()) {
        const char c = args[i];
        if (c == ' ' || c == '\t') {
            ++i;
        } else if (std::string_view("mnlbspc").find(c) != std::string_view::npos) {
            out.shape += c;
            ++i;
        } else {
            const char *begin = args.data() + i;
            if (*begin == '+')
                ++begin;
            double v = 0;
            const auto [end, ec] = std::from_chars(begin, args.data() + args.size(), v);
            if (ec != std::errc() || end == begin || out.shape.empty())
                return std::nullopt;
            out.shape += '#';
            out.numbers.push_back(v);
            i = static_cast<std::size_t>(end - args.data());
        }
    }
    if (out.numbers.empty())
        return std::nullopt;
    return out;
}

std::optional<Drawing> lerp(const Drawing &from, const Drawing &to, double t)
{
    if (from.shape != to.shape || from.scale != to.scale)
        return std::nullopt;
    Drawing out = from;
    for (std::size_t i = 0; i < out.numbers.size(); ++i)
        out.numbers[i] = from.numbers[i] + (to.numbers[i] - from.numbers[i]) * t;
    return out;
}

std::string format(std::string_view name, const Drawing &drawing)
{
    std::string out = "\\" + std::string(name) + "(";
    if (drawing.scale > 0)
        out += std::to_string(drawing.scale) + ",";
    std::size_t n = 0;
    for (std::size_t i = 0; i < drawing.shape.size(); ++i) {
        if (i > 0)
            out += ' ';
        out += drawing.shape[i] == '#' ? trimmedNumber(drawing.numbers[n++], 0) : std::string(1, drawing.shape[i]);
    }
    return out + ")";
}

double accelerate(double t, double accel)
{
    t = std::clamp(t, 0.0, 1.0);
    return accel == 1 ? t : std::pow(t, accel);
}

StopAt stopAt(double t, std::size_t stops)
{
    if (stops < 2)
        return {0, 0};
    const double at = std::clamp(t, 0.0, 1.0) * static_cast<double>(stops - 1);
    const auto index = std::min(static_cast<std::size_t>(at), stops - 2);
    return {index, at - static_cast<double>(index)};
}

}
