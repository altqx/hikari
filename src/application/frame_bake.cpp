#include "hikari/application/frame_bake.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>

namespace hikari::application {
namespace {

using u8s = std::u8string;
using u8v = std::u8string_view;
using Values = std::vector<double>;

struct Tag {
    u8s name;
    u8s value;
};

struct Item {
    std::optional<Tag> tag;
    u8s raw;
};

struct Piece {
    bool block = false;
    bool emptied = false;
    u8s text;
    std::vector<Item> items;
};

constexpr std::array<u8v, 51> kNames{
    u8"iclip", u8"alpha", u8"xbord", u8"ybord", u8"xshad", u8"yshad", u8"clip", u8"bord", u8"shad", u8"blur",
    u8"fscx",  u8"fscy",  u8"fade",  u8"move",  u8"fsp",   u8"frx",   u8"fry",  u8"frz",  u8"fax",  u8"fay",
    u8"fad",   u8"pos",   u8"org",   u8"pbo",   u8"fs",    u8"fr",    u8"fn",   u8"fe",   u8"an",   u8"be",
    u8"kf",    u8"ko",    u8"1c",    u8"2c",    u8"3c",    u8"4c",    u8"1a",   u8"2a",   u8"3a",   u8"4a",
    u8"b",     u8"i",     u8"u",     u8"s",     u8"a",     u8"q",     u8"p",    u8"r",    u8"t",    u8"k",
    u8"c"};

std::string narrow(u8v v)
{
    return std::string(reinterpret_cast<const char *>(v.data()), v.size());
}

u8s wide(const std::string &s)
{
    return u8s(reinterpret_cast<const char8_t *>(s.data()), s.size());
}

double number(u8v v)
{
    return std::strtod(narrow(v).c_str(), nullptr);
}

std::vector<Item> parseTags(u8v content)
{
    std::vector<Item> items;
    std::size_t i = 0;
    const std::size_t first = content.find(u8'\\');
    if (first != 0) {
        items.push_back(Item{std::nullopt, u8s(content.substr(0, first))});
        if (first == u8v::npos)
            return items;
        i = first;
    }
    while (i < content.size()) {
        const u8v rest = content.substr(i + 1);
        u8s name;
        for (const u8v known : kNames)
            if (rest.starts_with(known)) {
                name = known;
                break;
            }
        if (name.empty()) {
            std::size_t n = 0;
            while (n < rest.size() && rest[n] != u8'\\' && rest[n] != u8'(' &&
                   ((rest[n] >= u8'a' && rest[n] <= u8'z') || (rest[n] >= u8'A' && rest[n] <= u8'Z')))
                ++n;
            name = rest.substr(0, n);
        }
        std::size_t end = i + 1 + name.size();
        int depth = 0;
        while (end < content.size()) {
            const char8_t c = content[end];
            if (c == u8'(')
                ++depth;
            else if (c == u8')' && depth > 0)
                --depth;
            else if (c == u8'\\' && depth == 0)
                break;
            ++end;
        }
        items.push_back(Item{Tag{name, u8s(content.substr(i + 1 + name.size(), end - i - 1 - name.size()))}, {}});
        i = end;
    }
    return items;
}

std::vector<Piece> parse(u8v text)
{
    std::vector<Piece> pieces;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find(u8'{', i);
        const std::size_t close = open == u8v::npos ? u8v::npos : text.find(u8'}', open);
        if (open == u8v::npos || close == u8v::npos) {
            pieces.push_back(Piece{false, false, u8s(text.substr(i)), {}});
            break;
        }
        if (open > i)
            pieces.push_back(Piece{false, false, u8s(text.substr(i, open - i)), {}});
        pieces.push_back(Piece{true, false, {}, parseTags(text.substr(open + 1, close - open - 1))});
        i = close + 1;
    }
    return pieces;
}

u8s serialize(const std::vector<Piece> &pieces)
{
    u8s out;
    for (const auto &p : pieces) {
        if (!p.block) {
            out += p.text;
            continue;
        }
        if (p.emptied && p.items.empty())
            continue;
        out += u8'{';
        for (const auto &item : p.items)
            out += item.tag ? u8"\\" + item.tag->name + item.tag->value : item.raw;
        out += u8'}';
    }
    return out;
}

enum class Kind { Number, Colour, Alpha, Rect, Other };

u8s canonical(u8v name)
{
    if (name == u8"c")
        return u8"1c";
    if (name == u8"fr")
        return u8"frz";
    return u8s(name);
}

Kind kindOf(u8v canonicalName)
{
    static const std::array<u8v, 17> numbers{u8"fs",   u8"fsp",   u8"fscx",  u8"fscy",  u8"frx", u8"fry",
                                             u8"frz",  u8"fax",   u8"fay",   u8"bord",  u8"xbord", u8"ybord",
                                             u8"shad", u8"xshad", u8"yshad", u8"blur",  u8"be"};
    if (std::ranges::find(numbers, canonicalName) != numbers.end())
        return Kind::Number;
    if (canonicalName.size() == 2 && canonicalName[0] >= u8'1' && canonicalName[0] <= u8'4')
        return canonicalName[1] == u8'c' ? Kind::Colour : Kind::Alpha;
    if (canonicalName == u8"alpha")
        return Kind::Alpha;
    if (canonicalName == u8"clip" || canonicalName == u8"iclip")
        return Kind::Rect;
    return Kind::Other;
}

std::vector<u8s> arguments(u8v value)
{
    std::vector<u8s> out;
    const std::size_t open = value.find(u8'(');
    if (open == u8v::npos)
        return out;
    const std::size_t close = value.rfind(u8')');
    const u8v inside = value.substr(open + 1, close == u8v::npos || close < open ? u8v::npos : close - open - 1);
    std::size_t p = 0;
    while (true) {
        const std::size_t comma = inside.find(u8',', p);
        out.emplace_back(inside.substr(p, comma == u8v::npos ? u8v::npos : comma - p));
        if (comma == u8v::npos)
            break;
        p = comma + 1;
    }
    return out;
}

int hexValue(u8v value)
{
    std::string s;
    for (const char8_t c : value)
        if (std::isxdigit(static_cast<unsigned char>(c)) && !(s.empty() && (c == u8'H' || c == u8'h')))
            s += static_cast<char>(c);
    return static_cast<int>(std::strtol(s.c_str(), nullptr, 16));
}

std::optional<Values> parseValue(Kind kind, u8v value)
{
    switch (kind) {
    case Kind::Number: return Values{number(value)};
    case Kind::Colour: {
        const auto c = core::legacy::colour(value);
        return Values{double(c.r), double(c.g), double(c.b)};
    }
    case Kind::Alpha: return Values{double(hexValue(value) & 0xFF)};
    case Kind::Rect: {
        const auto args = arguments(value);
        if (args.size() != 4)
            return std::nullopt;
        Values v;
        for (const auto &a : args)
            v.push_back(number(a));
        return v;
    }
    case Kind::Other: return std::nullopt;
    }
    return std::nullopt;
}

std::string fixed(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s(buf);
    while (s.back() == '0')
        s.pop_back();
    if (s.back() == '.')
        s.pop_back();
    return s == "-0" ? "0" : s;
}

int channel(double v)
{
    return std::clamp(static_cast<int>(std::lround(v)), 0, 255);
}

u8s format(Kind kind, const Values &v)
{
    char buf[32];
    switch (kind) {
    case Kind::Colour:
        std::snprintf(buf, sizeof buf, "&H%02X%02X%02X&", channel(v[2]), channel(v[1]), channel(v[0]));
        return wide(buf);
    case Kind::Alpha: std::snprintf(buf, sizeof buf, "&H%02X&", channel(v[0])); return wide(buf);
    case Kind::Rect: {
        std::string s = "(";
        for (std::size_t i = 0; i < v.size(); ++i)
            s += (i ? "," : "") + std::to_string(std::lround(v[i]));
        return wide(s + ")");
    }
    default: return wide(fixed(v[0]));
    }
}

using State = std::map<u8s, Values>;

State styleState(const core::StyleValues &s)
{
    State st;
    st[u8"fs"] = {number(s.fontsize)};
    st[u8"fsp"] = {number(s.spacing)};
    st[u8"fscx"] = {number(s.scaleX)};
    st[u8"fscy"] = {number(s.scaleY)};
    st[u8"frz"] = {number(s.angle)};
    for (const u8v zero : {u8"frx", u8"fry", u8"fax", u8"fay", u8"blur", u8"be"})
        st[u8s(zero)] = {0};
    for (const u8v outline : {u8"bord", u8"xbord", u8"ybord"})
        st[u8s(outline)] = {number(s.outlineWidth)};
    for (const u8v shadow : {u8"shad", u8"xshad", u8"yshad"})
        st[u8s(shadow)] = {number(s.shadow)};
    const std::array colours{&s.primary, &s.secondary, &s.outline, &s.back};
    for (int n = 0; n < 4; ++n) {
        const auto &c = *colours[std::size_t(n)];
        const char8_t digit = static_cast<char8_t>(u8'1' + n);
        st[u8s{digit, u8'c'}] = {double(c.r), double(c.g), double(c.b)};
        st[u8s{digit, u8'a'}] = {double(c.a)};
    }
    st[u8"alpha"] = {double(s.primary.a)};
    return st;
}

const core::StyleValues &styleNamed(u8v name, const core::StyleValues &fallback,
                                    const std::vector<core::StyleValues> &styles)
{
    const auto found = std::ranges::find(styles, name, &core::StyleValues::name);
    return name.empty() || found == styles.end() ? fallback : *found;
}

void applyTag(State &state, const Tag &tag, const core::StyleValues &style,
              const std::vector<core::StyleValues> &styles)
{
    if (tag.name == u8"r") {
        state = styleState(styleNamed(tag.value, style, styles));
        return;
    }
    const u8s name = canonical(tag.name);
    const Kind kind = kindOf(name);
    const auto value = parseValue(kind, tag.value);
    if (!value)
        return;
    state[name] = *value;
    if (name == u8"alpha")
        for (const u8v n : {u8"1a", u8"2a", u8"3a", u8"4a"})
            state[u8s(n)] = *value;
}

Item *lastSinceReset(Piece &block, u8v canonicalName)
{
    for (auto it = block.items.rbegin(); it != block.items.rend() && !(it->tag && it->tag->name == u8"r"); ++it)
        if (it->tag && canonical(it->tag->name) == canonicalName)
            return &*it;
    return nullptr;
}

void setTag(Piece &block, const Tag &tag)
{
    if (Item *existing = lastSinceReset(block, canonical(tag.name)))
        existing->tag->value = tag.value;
    else
        block.items.push_back(Item{tag, {}});
}

double progress(double now, double t1, double t2, double accel)
{
    if (now < t1)
        return 0;
    if (now >= t2)
        return 1;
    return std::pow((now - t1) / (t2 - t1), accel);
}

void bakeMove(std::vector<Piece> &pieces, const BakeFrame &frame)
{
    Item *move = nullptr;
    for (auto &p : pieces)
        for (auto &item : p.items)
            if (!move && item.tag && item.tag->name == u8"move")
                move = &item;
    if (!move)
        return;
    const auto args = arguments(move->tag->value);
    if (args.size() < 4)
        return;
    const double x1 = number(args[0]), y1 = number(args[1]), x2 = number(args[2]), y2 = number(args[3]);
    if (x1 == x2 && y1 == y2)
        return;
    double t1 = args.size() >= 6 ? number(args[4]) : 0, t2 = args.size() >= 6 ? number(args[5]) : 0;
    if (t1 > t2)
        std::swap(t1, t2);
    if (t1 <= 0 && t2 <= 0) {
        t1 = 0;
        t2 = frame.duration;
    }
    const double k = frame.now <= t1 ? 0 : frame.now >= t2 ? 1 : (frame.now - t1) / (t2 - t1);
    *move->tag = Tag{u8"pos", wide("(" + fixed(x1 + (x2 - x1) * k) + "," + fixed(y1 + (y2 - y1) * k) + ")")};
    for (auto &p : pieces)
        std::erase_if(p.items, [](const Item &i) { return i.tag && i.tag->name == u8"move"; });
}

void bakeTransforms(std::vector<Piece> &pieces, const core::StyleValues &style,
                    const std::vector<core::StyleValues> &styles, const BakeFrame &frame)
{
    State state = styleState(style);
    for (auto &block : pieces) {
        if (!block.block)
            continue;
        for (const auto &item : block.items)
            if (item.tag && item.tag->name != u8"t")
                applyTag(state, *item.tag, style, styles);
        std::vector<Tag> transforms;
        for (const auto &item : block.items)
            if (item.tag && item.tag->name == u8"t")
                transforms.push_back(*item.tag);
        if (transforms.empty())
            continue;
        std::erase_if(block.items, [](const Item &i) { return i.tag && i.tag->name == u8"t"; });
        State current;
        for (const Tag &t : transforms) {
            const std::size_t open = t.value.find(u8'(');
            const std::size_t close = t.value.rfind(u8')');
            if (open == u8s::npos)
                continue;
            const u8v inside = u8v(t.value).substr(open + 1, close == u8s::npos || close < open ? u8v::npos
                                                                                               : close - open - 1);
            const std::size_t tagsAt = inside.find(u8'\\');
            std::vector<double> numbers;
            for (const auto &a : arguments(u8"(" + u8s(inside.substr(0, tagsAt)) + u8")"))
                if (a.find_first_not_of(u8" ") != u8s::npos)
                    numbers.push_back(number(a));
            double t1 = 0, t2 = 0, accel = 1;
            if (numbers.size() == 1)
                accel = numbers[0];
            else if (numbers.size() >= 2) {
                t1 = numbers[0];
                t2 = numbers[1];
                if (numbers.size() >= 3)
                    accel = numbers[2];
            }
            if (t2 == 0)
                t2 = frame.duration;
            const double k = progress(frame.now, t1, t2, accel);
            if (tagsAt == u8v::npos)
                continue;
            for (const Item &inner : parseTags(inside.substr(tagsAt))) {
                if (!inner.tag)
                    continue;
                const u8s name = canonical(inner.tag->name);
                const Kind kind = kindOf(name);
                const auto target = parseValue(kind, inner.tag->value);
                if (!target) {
                    if (k >= 1)
                        setTag(block, *inner.tag);
                    continue;
                }
                Values from = current.contains(name) ? current[name]
                              : state.contains(name)  ? state[name]
                                                      : Values{0, 0, double(frame.playResX), double(frame.playResY)};
                Values reached = *target;
                for (std::size_t c = 0; c < reached.size() && c < from.size(); ++c)
                    reached[c] = from[c] + ((*target)[c] - from[c]) * k;
                setTag(block, Tag{inner.tag->name, format(kind, reached)});
                current[name] = reached;
            }
        }
        for (auto &[name, reached] : current)
            state[name] = std::move(reached);
        if (block.items.empty())
            block.emptied = true;
    }
}

double fadeLevel(const Tag &fade, const BakeFrame &frame)
{
    const auto args = arguments(fade.value);
    double a1, a2, a3, t1, t2, t3, t4;
    if (fade.name == u8"fad") {
        if (args.size() < 2)
            return 0;
        a1 = 255, a2 = 0, a3 = 255;
        t1 = -1, t4 = -1;
        t2 = number(args[0]), t3 = number(args[1]);
    } else {
        if (args.size() < 7)
            return 0;
        a1 = number(args[0]), a2 = number(args[1]), a3 = number(args[2]);
        t1 = number(args[3]), t2 = number(args[4]), t3 = number(args[5]), t4 = number(args[6]);
    }
    if (t1 == -1 && t4 == -1) {
        t1 = 0;
        t4 = frame.duration;
        t3 = t4 - t3;
    }
    const double now = frame.now;
    if (now < t1)
        return a1;
    if (now < t2)
        return a1 * (1 - (now - t1) / (t2 - t1)) + a2 * ((now - t1) / (t2 - t1));
    if (now < t3)
        return a2;
    if (now < t4)
        return a2 * (1 - (now - t3) / (t4 - t3)) + a3 * ((now - t3) / (t4 - t3));
    return a3;
}

void bakeFade(std::vector<Piece> &pieces, const core::StyleValues &style, const BakeFrame &frame)
{
    std::optional<Tag> fade;
    for (const u8v name : {u8"fad", u8"fade"})
        for (const auto &p : pieces)
            for (const auto &item : p.items)
                if (!fade && item.tag && item.tag->name == name)
                    fade = item.tag;
    if (!fade)
        return;
    for (auto &p : pieces) {
        const bool had = !p.items.empty();
        std::erase_if(p.items, [](const Item &i) { return i.tag && (i.tag->name == u8"fad" || i.tag->name == u8"fade"); });
        if (had && p.items.empty())
            p.emptied = true;
    }
    const double level = fadeLevel(*fade, frame);
    if (level <= 0)
        return;
    const auto faded = [level](double a) {
        return std::clamp(a - (a * level - 0x7F) / 0xFF + level, 0.0, 255.0);
    };
    for (auto &p : pieces)
        for (auto &item : p.items)
            if (item.tag && kindOf(canonical(item.tag->name)) == Kind::Alpha)
                item.tag->value = format(Kind::Alpha, {faded(double(hexValue(item.tag->value) & 0xFF))});
    std::vector<Item> defaults;
    const std::array alphas{style.primary.a, style.secondary.a, style.outline.a, style.back.a};
    if (std::ranges::all_of(alphas, [&](auto a) { return a == alphas[0]; }))
        defaults.push_back(Item{Tag{u8"alpha", format(Kind::Alpha, {faded(double(alphas[0]))})}, {}});
    else
        for (int n = 0; n < 4; ++n)
            defaults.push_back(Item{Tag{u8s{static_cast<char8_t>(u8'1' + n), u8'a'},
                                        format(Kind::Alpha, {faded(double(alphas[std::size_t(n)]))})},
                                    {}});
    if (pieces.empty() || !pieces.front().block)
        pieces.insert(pieces.begin(), Piece{true, false, {}, {}});
    auto &first = pieces.front().items;
    first.insert(first.begin(), defaults.begin(), defaults.end());
    pieces.front().emptied = false;
}

bool animates(const std::vector<Piece> &pieces)
{
    for (const auto &p : pieces)
        for (const auto &item : p.items)
            if (item.tag && (item.tag->name == u8"t" || item.tag->name == u8"fad" || item.tag->name == u8"fade" ||
                             item.tag->name == u8"move"))
                return true;
    return false;
}

}

std::u8string bakeAnimations(std::u8string_view text, const core::StyleValues &style,
                             const std::vector<core::StyleValues> &styles, const BakeFrame &frame)
{
    auto pieces = parse(text);
    if (!animates(pieces))
        return u8s(text);
    bakeMove(pieces, frame);
    bakeTransforms(pieces, style, styles, frame);
    bakeFade(pieces, style, frame);
    return serialize(pieces);
}

}
