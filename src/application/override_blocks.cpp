#include "override_blocks.h"

#include "hikari/core/style.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hikari::application::blocks {

namespace {

constexpr std::array<u8v, 51> kNames{
    u8"iclip", u8"alpha", u8"xbord", u8"ybord", u8"xshad", u8"yshad", u8"clip", u8"bord", u8"shad", u8"blur",
    u8"fscx",  u8"fscy",  u8"fade",  u8"move",  u8"fsp",   u8"frx",   u8"fry",  u8"frz",  u8"fax",  u8"fay",
    u8"fad",   u8"pos",   u8"org",   u8"pbo",   u8"fs",    u8"fr",    u8"fn",   u8"fe",   u8"an",   u8"be",
    u8"kf",    u8"ko",    u8"1c",    u8"2c",    u8"3c",    u8"4c",    u8"1a",   u8"2a",   u8"3a",   u8"4a",
    u8"b",     u8"i",     u8"u",     u8"s",     u8"a",     u8"q",     u8"p",    u8"r",    u8"t",    u8"k",
    u8"c"};

}

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

}
