#include "motion_tags.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>

namespace hikari::application::motion {

namespace {

enum class Kind {
    Other,
    Comment,
    Pos,
    Org,
    Move,
    Fad,
    Fade,
    An,
    RectClip,
    RectIClip,
    VectClip,
    VectIClip,
    T,
    Karaoke,
    Reset,
    Fn,
    Fs,
    Fsp,
    Fscx,
    Fscy,
    Frz,
    Frx,
    Fry,
    Bord,
    XBord,
    YBord,
    Shad,
    XShad,
    YShad,
    Alpha,
    A1,
    A2,
    A3,
    A4,
    C1,
    C2,
    C3,
    C4,
    Be,
    Blur,
    Fax,
    Fay,
    Bold,
    Italic,
    Underline,
    Strike,
    Drawing
};

struct Name {
    std::string_view text;
    Kind kind;
};

constexpr Name kNames[] = {
    {"alpha", Kind::Alpha}, {"iclip", Kind::VectIClip}, {"xbord", Kind::XBord}, {"ybord", Kind::YBord},
    {"xshad", Kind::XShad}, {"yshad", Kind::YShad},     {"clip", Kind::VectClip}, {"bord", Kind::Bord},
    {"shad", Kind::Shad},   {"blur", Kind::Blur},       {"fscx", Kind::Fscx},     {"fscy", Kind::Fscy},
    {"move", Kind::Move},   {"fade", Kind::Fade},       {"fsp", Kind::Fsp},       {"frx", Kind::Frx},
    {"fry", Kind::Fry},     {"frz", Kind::Frz},         {"fax", Kind::Fax},       {"fay", Kind::Fay},
    {"pos", Kind::Pos},     {"org", Kind::Org},         {"fad", Kind::Fade},      {"pbo", Kind::Other},
    {"fs", Kind::Fs},       {"fn", Kind::Fn},           {"fr", Kind::Frz},        {"fe", Kind::Other},
    {"be", Kind::Be},       {"kf", Kind::Karaoke},      {"ko", Kind::Karaoke},    {"kt", Kind::Other},
    {"an", Kind::An},       {"1c", Kind::C1},           {"2c", Kind::C2},         {"3c", Kind::C3},
    {"4c", Kind::C4},       {"1a", Kind::A1},           {"2a", Kind::A2},         {"3a", Kind::A3},
    {"4a", Kind::A4},       {"p", Kind::Drawing},       {"b", Kind::Bold},        {"i", Kind::Italic},
    {"u", Kind::Underline}, {"s", Kind::Strike},        {"k", Kind::Karaoke},     {"K", Kind::Karaoke},
    {"q", Kind::Other},     {"r", Kind::Reset},         {"t", Kind::T},           {"a", Kind::Other},
    {"c", Kind::C1},
};

constexpr double kRad = std::numbers::pi / 180.0;
constexpr double kDeg = 180.0 / std::numbers::pi;

bool numberChar(char c)
{
    return (c >= '0' && c <= '9') || c == '.' || c == '-';
}

std::optional<double> number(std::string_view text)
{
    if (text.empty())
        return std::nullopt;
    double value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<double> leadingNumber(std::string_view text)
{
    std::size_t n = 0;
    while (n < text.size() && numberChar(text[n]))
        ++n;
    return number(text.substr(0, n));
}

std::vector<std::string_view> splitArgs(std::string_view args)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= args.size(); ++i)
        if (i == args.size() || args[i] == ',') {
            out.push_back(args.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

std::string_view inner(std::string_view value)
{
    if (value.empty() || value.front() != '(')
        return {};
    value.remove_prefix(1);
    if (!value.empty() && value.back() == ')')
        value.remove_suffix(1);
    return value;
}

std::optional<std::vector<double>> numbers(std::string_view args)
{
    std::vector<double> out;
    for (const auto part : splitArgs(args)) {
        const auto v = number(part);
        if (!v)
            return std::nullopt;
        out.push_back(*v);
    }
    return out;
}

struct TagView {
    Kind kind = Kind::Other;
    std::string_view name, value;
};

TagView view(std::string_view tag)
{
    if (tag.empty() || tag.front() != '\\')
        return {Kind::Comment, {}, tag};
    const std::string_view body = tag.substr(1);
    for (const auto &n : kNames) {
        if (!body.starts_with(n.text))
            continue;
        TagView out{n.kind, n.text, body.substr(n.text.size())};
        if (out.kind == Kind::Fade) {
            const auto args = numbers(inner(out.value));
            out.kind = !args ? Kind::Other : args->size() == 2 ? Kind::Fad : args->size() == 7 ? Kind::Fade : Kind::Other;
        } else if (out.kind == Kind::VectClip || out.kind == Kind::VectIClip) {
            const auto args = numbers(inner(out.value));
            if (args && args->size() == 4)
                out.kind = out.kind == Kind::VectClip ? Kind::RectClip : Kind::RectIClip;
        }
        return out;
    }
    return {Kind::Other, {}, body};
}

Kind kindOf(std::string_view tag)
{
    return view(tag).kind;
}

bool isClip(Kind k)
{
    return k == Kind::RectClip || k == Kind::RectIClip || k == Kind::VectClip || k == Kind::VectIClip;
}

bool isAlpha(Kind k)
{
    return k == Kind::Alpha || k == Kind::A1 || k == Kind::A2 || k == Kind::A3 || k == Kind::A4;
}

bool isColour(Kind k)
{
    return k == Kind::C1 || k == Kind::C2 || k == Kind::C3 || k == Kind::C4;
}

constexpr Kind kOnce[] = {Kind::Pos, Kind::Org, Kind::Move, Kind::Fad, Kind::Fade, Kind::An, Kind::RectClip, Kind::RectIClip};

bool once(Kind k)
{
    return std::ranges::contains(kOnce, k);
}

bool repeats(Kind k)
{
    return !once(k) && k != Kind::Other && k != Kind::Comment && k != Kind::T && k != Kind::VectClip &&
           k != Kind::VectIClip;
}

bool transformable(Kind k)
{
    switch (k) {
    case Kind::Fs: case Kind::Fsp: case Kind::Fscx: case Kind::Fscy: case Kind::Frz: case Kind::Frx: case Kind::Fry:
    case Kind::Bord: case Kind::XBord: case Kind::YBord: case Kind::Shad: case Kind::XShad: case Kind::YShad:
    case Kind::Alpha: case Kind::A1: case Kind::A2: case Kind::A3: case Kind::A4: case Kind::C1: case Kind::C2:
    case Kind::C3: case Kind::C4: case Kind::Be: case Kind::Blur: case Kind::Fax: case Kind::Fay: case Kind::RectClip:
    case Kind::RectIClip:
        return true;
    default:
        return false;
    }
}

std::string_view outName(Kind k)
{
    switch (k) {
    case Kind::Fs: return "fs";
    case Kind::Fsp: return "fsp";
    case Kind::Fscx: return "fscx";
    case Kind::Fscy: return "fscy";
    case Kind::Frz: return "frz";
    case Kind::Frx: return "frx";
    case Kind::Fry: return "fry";
    case Kind::Bord: return "bord";
    case Kind::XBord: return "xbord";
    case Kind::YBord: return "ybord";
    case Kind::Shad: return "shad";
    case Kind::XShad: return "xshad";
    case Kind::YShad: return "yshad";
    case Kind::Alpha: return "alpha";
    case Kind::A1: return "1a";
    case Kind::A2: return "2a";
    case Kind::A3: return "3a";
    case Kind::A4: return "4a";
    case Kind::C1: return "1c";
    case Kind::C2: return "2c";
    case Kind::C3: return "3c";
    case Kind::C4: return "4c";
    case Kind::Be: return "be";
    case Kind::Blur: return "blur";
    case Kind::Fax: return "fax";
    case Kind::Fay: return "fay";
    case Kind::RectClip: return "clip";
    case Kind::RectIClip: return "iclip";
    default: return {};
    }
}

std::string printf1(const char *format, double value)
{
    char buffer[64];
    const int n = std::snprintf(buffer, sizeof buffer, format, value);
    return std::string(buffer, static_cast<std::size_t>(std::max(n, 0)));
}

std::string formatInt(double value)
{
    return std::to_string(static_cast<long long>(value));
}

std::string formatHex(double value)
{
    char buffer[32];
    const int n = std::snprintf(buffer, sizeof buffer, "%02llX",
                                static_cast<unsigned long long>(static_cast<long long>(value)));
    return std::string(buffer, static_cast<std::size_t>(std::max(n, 0)));
}

using Value = std::array<double, 4>;

std::optional<Value> hexValue(std::string_view value)
{
    if (!value.starts_with("&H") && !value.starts_with("&h"))
        return std::nullopt;
    value.remove_prefix(2);
    const std::size_t end = value.find('&');
    if (end == std::string_view::npos || end == 0)
        return std::nullopt;
    unsigned long long v = 0;
    const auto [p, error] = std::from_chars(value.data(), value.data() + end, v, 16);
    if (error != std::errc{} || p != value.data() + end)
        return std::nullopt;
    return Value{static_cast<double>((v >> 16) & 255), static_cast<double>((v >> 8) & 255),
                 static_cast<double>(v & 255), static_cast<double>(v & 255)};
}

std::optional<Value> valueOf(const TagView &v)
{
    if (isColour(v.kind)) {
        const auto c = hexValue(v.value);
        return c ? std::optional<Value>(Value{(*c)[0], (*c)[1], (*c)[2], 0}) : std::nullopt;
    }
    if (isAlpha(v.kind)) {
        const auto c = hexValue(v.value);
        return c ? std::optional<Value>(Value{(*c)[3], 0, 0, 0}) : std::nullopt;
    }
    if (v.kind == Kind::RectClip || v.kind == Kind::RectIClip) {
        const auto args = numbers(inner(v.value));
        if (!args || args->size() != 4)
            return std::nullopt;
        return Value{(*args)[0], (*args)[1], (*args)[2], (*args)[3]};
    }
    const auto n = leadingNumber(v.value);
    return n ? std::optional<Value>(Value{*n, 0, 0, 0}) : std::nullopt;
}

std::string formatValue(Kind k, const Value &v)
{
    const std::string name = "\\" + std::string(outName(k));
    if (k == Kind::Fs || k == Kind::Be)
        return name + formatInt(v[0]);
    if (isColour(k))
        return name + "&H" + formatHex(v[0]) + formatHex(v[1]) + formatHex(v[2]) + "&";
    if (isAlpha(k))
        return name + "&H" + formatHex(v[0]) + "&";
    if (k == Kind::RectClip || k == Kind::RectIClip)
        return name + "(" + formatLua(v[0]) + "," + formatLua(v[1]) + "," + formatLua(v[2]) + "," + formatLua(v[3]) + ")";
    return name + formatG(v[0]);
}

std::optional<Value> styleValue(Kind k, const Props &p, double resX, double resY)
{
    switch (k) {
    case Kind::Fs: return Value{p.fontsize};
    case Kind::Fsp: return Value{p.spacing};
    case Kind::Fscx: return Value{p.scaleX};
    case Kind::Fscy: return Value{p.scaleY};
    case Kind::Frz: return Value{p.angle};
    case Kind::Bord: return Value{p.outline};
    case Kind::Shad: return Value{p.shadow};
    case Kind::A1: return Value{p.alpha[0]};
    case Kind::A2: return Value{p.alpha[1]};
    case Kind::A3: return Value{p.alpha[2]};
    case Kind::A4: return Value{p.alpha[3]};
    case Kind::C1: return Value{p.bgr[0][0], p.bgr[0][1], p.bgr[0][2]};
    case Kind::C2: return Value{p.bgr[1][0], p.bgr[1][1], p.bgr[1][2]};
    case Kind::C3: return Value{p.bgr[2][0], p.bgr[2][1], p.bgr[2][2]};
    case Kind::C4: return Value{p.bgr[3][0], p.bgr[3][1], p.bgr[3][2]};
    case Kind::RectClip: case Kind::RectIClip: return Value{0, 0, resX, resY};
    default: return transformable(k) ? std::optional<Value>(Value{}) : std::nullopt;
    }
}

std::vector<std::string> splitTags(std::string_view block)
{
    std::vector<std::string> out;
    int depth = 0;
    for (const char c : block) {
        if (c == '\\' && depth == 0)
            out.emplace_back();
        if (out.empty())
            out.emplace_back();
        out.back() += c;
        depth += c == '(' ? 1 : c == ')' && depth > 0 ? -1 : 0;
    }
    return out;
}

struct Transform {
    double start = 0, end = 0, accel = 1;
    std::string_view effect;
};

Transform transformOf(std::string_view value, std::int64_t duration)
{
    const std::string_view content = inner(value);
    const std::size_t slash = content.find('\\');
    Transform out;
    out.effect = slash == std::string_view::npos ? std::string_view{} : content.substr(slash);
    std::string_view head = content.substr(0, std::min(slash, content.size()));
    if (head.ends_with(','))
        head.remove_suffix(1);
    std::vector<double> args;
    if (!head.empty())
        for (const auto part : splitArgs(head))
            args.push_back(number(part).value_or(0));
    if (args.size() == 1 || args.size() >= 3)
        out.accel = args.size() == 1 ? args[0] : args[2];
    if (args.size() >= 2) {
        out.start = args[0];
        out.end = args[1];
    }
    if (out.end == 0)
        out.end = static_cast<double>(duration);
    return out;
}

using Position = std::pair<std::size_t, std::size_t>;

template <class F>
void forEachTag(std::vector<Piece> &pieces, F &&f)
{
    for (std::size_t p = 0; p < pieces.size(); ++p)
        if (pieces[p].block)
            for (std::size_t t = 0; t < pieces[p].tags.size(); ++t)
                f(Position{p, t}, pieces[p].tags[t]);
}

void dropEmptyBlocks(std::vector<Piece> &pieces)
{
    std::erase_if(pieces, [](const Piece &p) { return p.block && p.tags.empty(); });
}

void dedupe(std::vector<Piece> &pieces)
{
    std::set<Position> drop;
    std::map<Kind, Position> first;
    forEachTag(pieces, [&](Position at, const std::string &tag) {
        const Kind k = kindOf(tag);
        if (!once(k))
            return;
        if (first.contains(k))
            drop.insert(at);
        else
            first.emplace(k, at);
    });
    for (const auto [a, b] : {std::pair{Kind::Move, Kind::Pos}, std::pair{Kind::Fade, Kind::Fad},
                              std::pair{Kind::RectClip, Kind::RectIClip}})
        if (first.contains(a) && first.contains(b)) {
            const Kind later = first[a] < first[b] ? b : a;
            forEachTag(pieces, [&](Position at, const std::string &tag) {
                if (kindOf(tag) == later)
                    drop.insert(at);
            });
        }
    for (std::size_t p = 0; p < pieces.size();) {
        std::size_t end = p;
        while (end < pieces.size() && pieces[end].block)
            ++end;
        std::set<Kind> seen;
        for (std::size_t q = end; q-- > p;)
            for (std::size_t t = pieces[q].tags.size(); t-- > 0;) {
                const Kind k = kindOf(pieces[q].tags[t]);
                if (repeats(k) && !seen.insert(k).second)
                    drop.insert({q, t});
            }
        p = end == p ? p + 1 : end;
    }
    for (std::size_t p = pieces.size(); p-- > 0;)
        for (std::size_t t = pieces[p].tags.size(); t-- > 0;)
            if (drop.contains({p, t}) || pieces[p].tags[t] == "\\clip()")
                pieces[p].tags.erase(pieces[p].tags.begin() + static_cast<std::ptrdiff_t>(t));
    dropEmptyBlocks(pieces);
}

bool has(const std::vector<std::string> &tags, std::size_t from, std::initializer_list<Kind> kinds)
{
    for (std::size_t i = from; i < tags.size(); ++i)
        if (std::ranges::contains(kinds, kindOf(tags[i])))
            return true;
    return false;
}

std::vector<std::string> missingTags(const std::vector<std::string> &tags, std::size_t from, const Props &p,
                                     const Setup &setup, bool fades)
{
    const auto &o = setup.options;
    const bool scale = o.main.scale;
    std::vector<std::string> out;
    const auto add = [&](bool on, Kind k, double value, bool skipZero) {
        if (on && !has(tags, from, {k}) && !(skipZero && value == 0))
            out.push_back("\\" + std::string(outName(k)) + formatG(value));
    };
    add(scale, Kind::Fscx, p.scaleX, true);
    add(scale, Kind::Fscy, p.scaleY, true);
    add(scale && o.border, Kind::Bord, p.outline, true);
    add(scale && o.shadow, Kind::Shad, p.shadow, true);
    add(o.main.rotation, Kind::Frz, p.angle, false);
    if (!o.bakeTransforms || !fades || has(tags, from, {Kind::Alpha}))
        return out;
    if (std::ranges::all_of(p.alpha, [](double a) { return a == 0; })) {
        out.push_back("\\alpha&H00&");
        return out;
    }
    const auto alpha = [&](Kind k, double value) { out.push_back(formatValue(k, Value{value})); };
    if (!has(tags, from, {Kind::A1}))
        alpha(Kind::A1, p.alpha[0]);
    if (!has(tags, from, {Kind::A2}) && has(tags, from, {Kind::Karaoke}))
        alpha(Kind::A2, p.alpha[1]);
    if (!has(tags, from, {Kind::A3}) && (has(tags, from, {Kind::Bord, Kind::XBord, Kind::YBord}) || p.outline > 0))
        alpha(Kind::A3, p.alpha[2]);
    if (!has(tags, from, {Kind::A4}) && (has(tags, from, {Kind::Shad, Kind::XShad, Kind::YShad}) || p.shadow > 0))
        alpha(Kind::A4, p.alpha[3]);
    return out;
}

template <class F>
std::string mapPairs(std::string_view text, char separator, F &&f)
{
    std::string out;
    std::size_t i = 0;
    const auto run = [&](std::size_t from) {
        std::size_t j = from;
        while (j < text.size() && numberChar(text[j]))
            ++j;
        return j;
    };
    while (i < text.size()) {
        const std::size_t a = run(i);
        if (a > i && a < text.size() && text[a] == separator) {
            const std::size_t b = run(a + 1);
            const auto x = number(text.substr(i, a - i)), y = number(text.substr(a + 1, b - a - 1));
            if (b > a + 1 && x && y) {
                const auto [nx, ny] = f(*x, *y);
                out += formatG(round2(nx)) + separator + formatG(round2(ny));
                i = b;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

std::string clipToFloat(std::string_view tag)
{
    const TagView v = view(tag);
    const std::string_view content = inner(v.value);
    const std::size_t comma = content.find(',');
    if (comma == std::string_view::npos || v.kind == Kind::RectClip || v.kind == Kind::RectIClip)
        return std::string(tag);
    const auto scale = number(content.substr(0, comma));
    if (!scale || content.substr(0, comma).find_first_not_of("0123456789") != std::string_view::npos)
        return std::string(tag);
    const double divisor = std::pow(2.0, *scale - 1);
    const std::string points = mapPairs(content.substr(comma + 1), ' ', [&](double x, double y) {
        return std::pair{x / divisor, y / divisor};
    });
    return "\\" + std::string(v.name) + "(" + points + ")";
}

std::string rectToVector(std::string_view tag)
{
    const TagView v = view(tag);
    const auto args = numbers(inner(v.value));
    if (!args || args->size() != 4)
        return std::string(tag);
    const auto &a = *args;
    return "\\" + std::string(v.name) + "(m " + formatG(a[0]) + " " + formatG(a[1]) + " l " + formatG(a[2]) + " " +
           formatG(a[1]) + " " + formatG(a[2]) + " " + formatG(a[3]) + " " + formatG(a[0]) + " " + formatG(a[3]) + ")";
}

struct Frames {
    Track::Frame main, rect, vector;
    std::size_t index = 0;
    double dx = 0, dy = 0;
};

Frames framesAt(const Setup &setup, std::size_t index)
{
    Frames f;
    f.index = index;
    f.main = setup.main->at(index);
    if (setup.rect) {
        f.rect = setup.rect->at(index);
        f.rect.turn = 0;
    }
    if (setup.vector)
        f.vector = setup.vector->at(index);
    return f;
}

std::string posTag(double x, double y)
{
    return "\\pos(" + formatG(round2(x)) + "," + formatG(round2(y)) + ")";
}

std::string followTag(std::string_view tag, const Setup &setup, const Frames &f)
{
    const TagView v = view(tag);
    const auto &o = setup.options;
    const bool main = !o.clipsOnly;
    const bool scale = main && o.main.scale;
    const auto times = [&](double factor) {
        const auto n = leadingNumber(v.value);
        if (!n)
            return std::string(tag);
        return "\\" + std::string(v.name) + formatLua(round2(*n * factor));
    };
    switch (v.kind) {
    case Kind::Pos:
    case Kind::Org: {
        if (!main || (v.kind == Kind::Pos ? !setup.followsMain() : !o.origin))
            return std::string(tag);
        const auto args = numbers(inner(v.value));
        if (!args || args->size() != 2)
            return std::string(tag);
        if (v.kind == Kind::Pos && o.absolute)
            return posTag(setup.main->x[f.index], setup.main->y[f.index]);
        const auto [x, y] = setup.main->follow((*args)[0], (*args)[1], f.main);
        return "\\" + std::string(v.name) + "(" + formatG(round2(x)) + "," + formatG(round2(y)) + ")";
    }
    case Kind::Fscx: case Kind::Fscy:
        return scale ? times(f.main.ratio) : std::string(tag);
    case Kind::Bord: case Kind::XBord: case Kind::YBord:
        return scale && o.border ? times(f.main.ratio) : std::string(tag);
    case Kind::Shad: case Kind::XShad: case Kind::YShad:
        return scale && o.shadow ? times(f.main.ratio) : std::string(tag);
    case Kind::Blur:
        return scale && o.blur ? times(1 - (1 - f.main.ratio) * o.blurScale) : std::string(tag);
    case Kind::Frz: {
        if (!main || !o.main.rotation)
            return std::string(tag);
        const auto n = leadingNumber(v.value);
        return n ? "\\" + std::string(v.name) + formatLua(round2(*n + f.main.turn)) : std::string(tag);
    }
    case Kind::RectClip: case Kind::RectIClip:
        if (!setup.rect)
            return std::string(tag);
        return "\\" + std::string(v.name) + mapPairs(v.value, ',', [&](double x, double y) {
                   return setup.rect->follow(x + f.dx, y + f.dy, f.rect);
               });
    case Kind::VectClip: case Kind::VectIClip:
        if (!setup.vector)
            return std::string(tag);
        return "\\" + std::string(v.name) + mapPairs(v.value, ' ', [&](double x, double y) {
                   return setup.vector->follow(x + f.dx, y + f.dy, f.vector);
               });
    default:
        return std::string(tag);
    }
}

bool follows(std::string_view tag, const Setup &setup)
{
    switch (kindOf(tag)) {
    case Kind::Pos: case Kind::Org: case Kind::Fscx: case Kind::Fscy: case Kind::Bord: case Kind::XBord:
    case Kind::YBord: case Kind::Shad: case Kind::XShad: case Kind::YShad: case Kind::Blur: case Kind::Frz:
    case Kind::RectClip: case Kind::RectIClip: case Kind::VectClip: case Kind::VectIClip:
        return true;
    default:
        return false;
    }
}

std::string followTransform(std::string_view tag, const Setup &setup, const Frames &f)
{
    const TagView v = view(tag);
    const std::string_view content = inner(v.value);
    const std::size_t slash = content.find('\\');
    if (slash == std::string_view::npos)
        return std::string(tag);
    std::string out = "\\t(" + std::string(content.substr(0, slash));
    for (const auto &t : splitTags(content.substr(slash)))
        out += followTag(t, setup, f);
    return out + ")";
}

void applyDelta(const Setup &setup, std::vector<Piece> &pieces, Frames &f)
{
    if (!setup.options.absolute)
        return;
    forEachTag(pieces, [&](Position, const std::string &tag) {
        const TagView v = view(tag);
        if (v.kind != Kind::Pos || f.dx != 0 || f.dy != 0)
            return;
        const auto args = numbers(inner(v.value));
        if (args && args->size() == 2) {
            f.dx = setup.main->x[f.index] - (*args)[0];
            f.dy = setup.main->y[f.index] - (*args)[1];
        }
    });
}

std::map<Kind, Value> stateOf(const Props &p, const Setup &setup)
{
    std::map<Kind, Value> out;
    for (int k = 0; k <= static_cast<int>(Kind::Drawing); ++k)
        if (const auto v = styleValue(static_cast<Kind>(k), p, setup.resX, setup.resY))
            out[static_cast<Kind>(k)] = *v;
    return out;
}

void remember(std::map<Kind, Value> &state, Kind k, const Value &v)
{
    state[k] = v;
    if (k == Kind::Alpha)
        for (const Kind a : {Kind::A1, Kind::A2, Kind::A3, Kind::A4})
            state[a] = v;
}

void bake(std::vector<Piece> &pieces, const PreparedLine &line, const Setup &setup, const FrameTime &time)
{
    auto state = stateOf(line.props, setup);
    for (auto &piece : pieces) {
        if (!piece.block)
            continue;
        for (std::size_t i = 0; i < piece.tags.size(); ++i) {
            const TagView v = view(piece.tags[i]);
            if (v.kind == Kind::Reset) {
                const auto style = setup.styles.find(std::string(v.value));
                state = stateOf(style == setup.styles.end() ? line.props : style->second, setup);
                continue;
            }
            if (v.kind != Kind::T) {
                if (transformable(v.kind))
                    if (const auto value = valueOf(v))
                        remember(state, v.kind, *value);
                continue;
            }
            const Transform t = transformOf(v.value, line.duration);
            const double delta = static_cast<double>(time.delta);
            const double start = t.start - delta, end = t.end - delta;
            const double now = static_cast<double>(time.evaluate);
            const double linear = end != start ? (now - start) / (end - start)
                                 : now >= start ? std::numeric_limits<double>::infinity()
                                                : -std::numeric_limits<double>::infinity();
            const double progress = std::pow(linear, t.accel);
            std::vector<std::pair<Kind, std::vector<Value>>> targets;
            for (const auto &tag : splitTags(t.effect)) {
                const TagView e = view(tag);
                const auto value = valueOf(e);
                if (!transformable(e.kind) || !value)
                    continue;
                auto found = std::ranges::find(targets, e.kind, &std::pair<Kind, std::vector<Value>>::first);
                if (found == targets.end())
                    targets.push_back({e.kind, {*value}});
                else
                    found->second.push_back(*value);
            }
            std::vector<std::string> baked;
            for (const auto &[k, ends] : targets) {
                Value value = state[k];
                if (linear >= 1) {
                    value = ends.back();
                } else if (!(linear <= 0)) {
                    for (const Value &e : ends)
                        for (std::size_t c = 0; c < 4; ++c)
                            value[c] = (1 - progress) * value[c] + progress * e[c];
                }
                baked.push_back(formatValue(k, value));
                remember(state, k, value);
            }
            piece.tags.erase(piece.tags.begin() + static_cast<std::ptrdiff_t>(i));
            piece.tags.insert(piece.tags.begin() + static_cast<std::ptrdiff_t>(i), baked.begin(), baked.end());
            i += baked.size();
            --i;
        }
    }
}

void scaleFades(std::vector<Piece> &pieces, std::int64_t delta)
{
    std::optional<std::vector<double>> fade;
    for (auto &piece : pieces)
        if (piece.block)
            std::erase_if(piece.tags, [&](const std::string &tag) {
                const TagView v = view(tag);
                if (v.kind != Kind::Fade)
                    return false;
                fade = numbers(inner(v.value));
                return true;
            });
    if (!fade)
        return;
    const auto &f = *fade;
    const double a1 = f[0], a2 = f[1], a3 = f[2], t1 = f[3], t2 = f[4], t3 = f[5], t4 = f[6];
    const double d = static_cast<double>(delta);
    const double alpha = d < t1   ? a1
                         : d < t2 ? a1 + (a2 - a1) * (d - t1) / (t2 - t1)
                         : d < t3 ? a2
                         : d < t4 ? a2 + (a3 - a2) * (d - t3) / (t4 - t3)
                                  : a3;
    const double factor = (255 - alpha) / 255;
    for (auto &piece : pieces)
        if (piece.block)
            for (auto &tag : piece.tags) {
                const TagView v = view(tag);
                if (!isAlpha(v.kind))
                    continue;
                if (const auto value = valueOf(v))
                    tag = formatValue(v.kind, Value{std::floor(255 - factor * (255 - (*value)[0]) + 0.5)});
            }
}

void shiftAnimations(std::vector<Piece> &pieces, const PreparedLine &line, std::int64_t delta)
{
    const double d = static_cast<double>(delta), duration = static_cast<double>(line.duration);
    for (auto &piece : pieces) {
        if (!piece.block)
            continue;
        std::vector<std::string> out;
        for (const auto &tag : piece.tags) {
            const TagView v = view(tag);
            if (v.kind == Kind::Fade) {
                const auto parts = splitArgs(inner(v.value));
                std::string text = "\\fade(";
                for (std::size_t i = 0; i < parts.size(); ++i)
                    text += (i ? "," : "") + (i < 3 ? std::string(parts[i]) : formatLua(number(parts[i]).value_or(0) - d));
                out.push_back(text + ")");
                continue;
            }
            if (v.kind != Kind::T) {
                out.push_back(tag);
                continue;
            }
            const Transform t = transformOf(v.value, line.duration);
            const double start = t.start - d, end = t.end - d;
            if (t.effect.empty() || (end > 0 && (start > duration || end < start)))
                continue;
            if (end <= 0) {
                for (auto &e : splitTags(t.effect))
                    out.push_back(std::move(e));
                continue;
            }
            std::string text = "\\t(" + formatLua(start) + "," + formatLua(end) + ",";
            if (t.accel != 1)
                text += formatLua(t.accel) + ",";
            out.push_back(text + std::string(t.effect) + ")");
        }
        piece.tags = std::move(out);
    }
}

void replaceMove(std::vector<Piece> &pieces, const Move &move, double time)
{
    forEachTag(pieces, [&](Position, std::string &tag) {
        if (kindOf(tag) != Kind::Move)
            return;
        const auto [x, y] = move.at(time);
        tag = "\\pos(" + formatLua(x) + "," + formatLua(y) + ")";
    });
}

std::optional<Move> moveOf(const TagView &v, std::int64_t duration)
{
    const auto args = numbers(inner(v.value));
    if (!args || (args->size() != 4 && args->size() != 6))
        return std::nullopt;
    const auto &a = *args;
    Move m{a[0], a[1], a[2], a[3], 0, static_cast<double>(duration)};
    if (a.size() == 6) {
        m.t1 = a[4];
        m.t2 = a[5];
    }
    return m;
}

}

double round2(double value)
{
    return std::floor(value * 100 + 0.5) / 100;
}

std::string formatG(double value)
{
    return printf1("%g", value);
}

std::string formatLua(double value)
{
    return printf1("%.14g", value);
}

Props propsOf(const core::StyleValues &style)
{
    const auto num = [](const std::u8string &text) {
        return number(std::string_view(reinterpret_cast<const char *>(text.data()), text.size())).value_or(0);
    };
    Props p;
    p.fontname.assign(style.fontname.begin(), style.fontname.end());
    p.fontsize = num(style.fontsize);
    p.spacing = num(style.spacing);
    p.scaleX = num(style.scaleX);
    p.scaleY = num(style.scaleY);
    p.angle = num(style.angle);
    p.outline = num(style.outlineWidth);
    p.shadow = num(style.shadow);
    p.align = static_cast<int>(num(style.alignment));
    p.marginL = static_cast<int>(num(style.marginLeft));
    p.marginR = static_cast<int>(num(style.marginRight));
    p.marginV = static_cast<int>(num(style.marginVertical));
    const core::Colour colours[] = {style.primary, style.secondary, style.outline, style.back};
    for (std::size_t i = 0; i < 4; ++i) {
        p.bgr[i] = {static_cast<double>(colours[i].b), static_cast<double>(colours[i].g),
                       static_cast<double>(colours[i].r)};
        p.alpha[i] = static_cast<double>(colours[i].a);
    }
    return p;
}

std::vector<Piece> parseText(std::string_view text)
{
    std::vector<Piece> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find('{', i);
        const std::size_t close = open == std::string_view::npos ? open : text.find('}', open);
        if (open != i || close == std::string_view::npos) {
            const std::size_t end = close == std::string_view::npos ? text.size() : open;
            if (!out.empty() && !out.back().block)
                out.back().text += text.substr(i, end - i);
            else
                out.push_back({false, std::string(text.substr(i, end - i)), {}});
            i = end;
            continue;
        }
        out.push_back({true, {}, splitTags(text.substr(open + 1, close - open - 1))});
        i = close + 1;
    }
    return out;
}

std::string renderText(const std::vector<Piece> &pieces)
{
    std::string out;
    for (const auto &p : pieces) {
        if (!p.block) {
            out += p.text;
            continue;
        }
        if (p.tags.empty())
            continue;
        out += '{';
        for (const auto &t : p.tags)
            out += t;
        out += '}';
    }
    return out;
}

Track::Frame Track::at(std::size_t index) const
{
    return {x[index], y[index], scale[index] / scale[ref], turn[index] - turn[ref]};
}

std::array<double, 2> Track::follow(double px, double py, const Frame &frame) const
{
    const double dx = (px - x[ref]) * frame.ratio, dy = (py - y[ref]) * frame.ratio;
    const double radius = std::sqrt(dx * dx + dy * dy);
    const double angle = std::atan2(dy, dx) * kDeg;
    return {frame.x + radius * std::cos((angle - frame.turn) * kRad),
            frame.y + radius * std::sin((angle - frame.turn) * kRad)};
}

Track resolveTrack(const MotionTrack &data, const MotionFollow &follow, double resX, double resY)
{
    Track t;
    const double sx = resX / (data.width > 0 ? data.width : resX), sy = resY / (data.height > 0 ? data.height : resY);
    const std::size_t n = data.x.size();
    t.ref = static_cast<std::size_t>(std::clamp(follow.relativeFrame, 1, std::max(1, data.frames())) - 1);
    for (std::size_t i = 0; i < n; ++i) {
        t.x.push_back(sx * data.x[i]);
        t.y.push_back(sy * data.y[i]);
        t.scale.push_back(data.scaleX[i]);
        t.turn.push_back(-data.rotation[i]);
    }
    const auto hold = [&](std::vector<double> &v, bool on) {
        if (!on && !v.empty())
            std::ranges::fill(v, v[t.ref]);
    };
    hold(t.x, follow.x);
    hold(t.y, follow.y);
    hold(t.scale, follow.scale);
    hold(t.turn, follow.rotation);
    return t;
}

bool Setup::followsMain() const
{
    const auto &m = options.main;
    return m.x || m.y || m.scale || m.rotation;
}

bool Setup::touchesClips() const
{
    const auto &m = options.main;
    const auto &c = options.clip;
    return m.rectClips || m.vectorClips || m.rectToVector ||
           (options.clipTrack && (c.rectClips || c.vectorClips || c.rectToVector));
}

bool Setup::toVector() const
{
    return options.main.rectToVector || (options.clipTrack && options.clip.rectToVector);
}

std::array<double, 2> Move::at(double time) const
{
    const double progress = t2 > t1 ? std::clamp((time - t1) / (t2 - t1), 0.0, 1.0) : time >= t1 ? 1.0 : 0.0;
    return {(1 - progress) * x1 + progress * x2, (1 - progress) * y1 + progress * y2};
}

bool PreparedLine::linear(const Setup &setup) const
{
    return setup.options.linear && !(setup.options.origin && hasOrg) && !((setup.rect || setup.vector) && hasClip);
}

PreparedLine prepareLine(std::string_view text, std::int64_t duration, const Props &props, const Setup &setup,
                         int lineMarginL, int lineMarginR, int lineMarginV)
{
    PreparedLine line;
    line.props = props;
    line.duration = duration;
    line.pieces = parseText(text);
    auto &pieces = line.pieces;
    forEachTag(pieces, [&](Position, std::string &tag) {
        const TagView v = view(tag);
        if (v.kind != Kind::Fad)
            return;
        const auto args = numbers(inner(v.value));
        const double in = (*args)[0], out = (*args)[1];
        if (in != std::floor(in) || out != std::floor(out) || in < 0 || out < 0)
            return;
        tag = "\\fade(255,0,255,0," + formatInt(in) + "," + std::to_string(duration - static_cast<std::int64_t>(out)) +
              "," + std::to_string(duration) + ")";
    });
    dedupe(pieces);
    bool fades = false;
    forEachTag(pieces, [&](Position, const std::string &tag) { fades = fades || kindOf(tag) == Kind::Fade; });

    std::optional<int> align;
    bool positioned = false;
    forEachTag(pieces, [&](Position, const std::string &tag) {
        const TagView v = view(tag);
        if (v.kind == Kind::An && !align)
            align = static_cast<int>(leadingNumber(v.value).value_or(0));
        if (positioned || (v.kind != Kind::Pos && v.kind != Kind::Move))
            return;
        positioned = v.kind == Kind::Pos ? numbers(inner(v.value)).has_value() : (line.move = moveOf(v, duration)).has_value();
    });
    if (!positioned) {
        const int an = align.value_or(props.align);
        const double mv = lineMarginV != 0 ? lineMarginV : props.marginV;
        const double ml = lineMarginL != 0 ? lineMarginL : props.marginL;
        const double mr = lineMarginR != 0 ? lineMarginR : props.marginR;
        const double x = an % 3 == 0 ? setup.resX - mr : an % 3 == 1 ? ml : 0.5 * setup.resX;
        const int row = (an + 2) / 3;
        const double y = row == 1 ? setup.resY - mv : row == 2 ? 0.5 * setup.resY : mv;
        if (pieces.empty() || !pieces.front().block)
            pieces.insert(pieces.begin(), Piece{true, {}, {}});
        pieces.front().tags.insert(pieces.front().tags.begin(), "\\pos(" + formatG(x) + "," + formatG(y) + ")");
    }
    if (!pieces.empty() && pieces.front().block) {
        auto &tags = pieces.front().tags;
        const auto missing = missingTags(tags, 0, props, setup, fades);
        tags.insert(tags.begin(), missing.begin(), missing.end());
    }
    for (auto &piece : pieces) {
        if (!piece.block)
            continue;
        auto &tags = piece.tags;
        line.hasOrg = line.hasOrg || has(tags, 0, {Kind::Org});
        const auto reset = std::ranges::find_if(tags, [](const std::string &t) { return kindOf(t) == Kind::Reset; });
        if (reset != tags.end()) {
            const auto at = static_cast<std::size_t>(reset - tags.begin());
            const auto style = setup.styles.find(std::string(view(*reset).value));
            const auto missing = missingTags(tags, at + 1, style == setup.styles.end() ? props : style->second, setup, fades);
            tags.insert(tags.begin() + static_cast<std::ptrdiff_t>(at + 1), missing.begin(), missing.end());
        }
        if (!setup.touchesClips())
            continue;
        for (auto &tag : tags) {
            if (!isClip(kindOf(tag)))
                continue;
            line.hasClip = true;
            tag = clipToFloat(tag);
            if (setup.toVector())
                tag = rectToVector(tag);
        }
    }
    return line;
}

std::string frameText(const PreparedLine &line, const Setup &setup, const FrameTime &time)
{
    std::vector<Piece> pieces = line.pieces;
    if (setup.options.bakeTransforms) {
        bake(pieces, line, setup, time);
        scaleFades(pieces, time.delta);
    } else {
        shiftAnimations(pieces, line, time.delta);
    }
    if (line.move)
        replaceMove(pieces, *line.move, static_cast<double>(time.delta));
    Frames f = framesAt(setup, time.index);
    applyDelta(setup, pieces, f);
    for (auto &piece : pieces)
        if (piece.block)
            for (auto &tag : piece.tags)
                tag = kindOf(tag) == Kind::T ? followTransform(tag, setup, f) : followTag(tag, setup, f);
    return renderText(pieces);
}

std::string linearText(const PreparedLine &line, const Setup &setup, const LinearSpan &span)
{
    Frames first = framesAt(setup, span.first), last = framesAt(setup, span.last);
    std::vector<Piece> atFirst = line.pieces, atLast = line.pieces;
    if (line.move && setup.followsMain() && !setup.options.clipsOnly) {
        replaceMove(atFirst, *line.move, static_cast<double>(span.begin));
        replaceMove(atLast, *line.move, static_cast<double>(span.end));
    }
    applyDelta(setup, atFirst, first);
    applyDelta(setup, atLast, last);
    const std::string times = std::to_string(span.begin) + "," + std::to_string(span.end);
    std::vector<Piece> out = atFirst;
    for (std::size_t p = 0; p < out.size(); ++p) {
        if (!out[p].block)
            continue;
        std::vector<std::string> tags;
        for (std::size_t t = 0; t < out[p].tags.size(); ++t) {
            const std::string &tag = out[p].tags[t];
            if (!follows(tag, setup)) {
                tags.push_back(tag);
                continue;
            }
            const std::string a = followTag(tag, setup, first), b = followTag(atLast[p].tags[t], setup, last);
            if (a == tag && b == atLast[p].tags[t]) {
                tags.push_back(tag);
                continue;
            }
            const TagView va = view(a), vb = view(b);
            if (va.kind == Kind::Pos && vb.kind == Kind::Pos) {
                tags.push_back("\\move(" + std::string(inner(va.value)) + "," + std::string(inner(vb.value)) + "," + times + ")");
                continue;
            }
            tags.push_back(a);
            tags.push_back("\\t(" + times + "," + b + ")");
        }
        out[p].tags = std::move(tags);
    }
    return renderText(out);
}

std::string finishFrame(std::string_view text, double karaokeShift)
{
    std::vector<Piece> pieces = parseText(text);
    dedupe(pieces);
    double shift = karaokeShift;
    for (auto &piece : pieces) {
        if (!piece.block)
            continue;
        std::vector<std::string> tags;
        for (const auto &tag : piece.tags) {
            const TagView v = view(tag);
            const auto time = v.kind == Kind::Karaoke ? leadingNumber(v.value) : std::nullopt;
            if (!time || !(shift > 0)) {
                tags.push_back(tag);
                continue;
            }
            const double old = -shift, left = *time - shift;
            shift -= *time;
            if (!(left > 0))
                continue;
            const std::string name = "\\" + std::string(v.name);
            if (v.name == "kf")
                tags.push_back(name + formatInt(old) + name + formatInt(*time));
            else
                tags.push_back(name + formatInt(left));
        }
        piece.tags = std::move(tags);
    }
    return renderText(pieces);
}

std::string finishLinear(std::string_view text)
{
    return renderText(parseText(text));
}

}
