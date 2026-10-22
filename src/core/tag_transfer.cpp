#include "hikari/core/tag_transfer.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>

namespace hikari::core {

namespace {

// ---- Override blocks -------------------------------------------------------

struct Tag {
    std::size_t order = 0;
    std::u16string name;
    std::u16string args;
    std::u16string text; // "\name args", as written
};

struct Piece {
    bool block = false;
    std::size_t rawStart = 0, rawEnd = 0;
    std::vector<Tag> tags;
};

struct Tokens {
    std::vector<Piece> pieces;
    bool malformed = false;
};

// Longest names first, so "\bord" is not read as "\b".
constexpr std::u16string_view kNames[] = {
    u"alpha", u"iclip", u"xbord", u"ybord", u"xshad", u"yshad", u"blur", u"bord", u"clip", u"fade", u"fscx",
    u"fscy",  u"move",  u"shad",  u"fad",   u"fax",   u"fay",   u"frx",  u"fry",  u"frz",  u"fsc",  u"fsp",  u"org",
    u"pbo",   u"pos",   u"1c",    u"2c",    u"3c",    u"4c",    u"1a",   u"2a",   u"3a",   u"4a",   u"an",
    u"be",    u"fe",    u"fn",    u"fr",    u"fs",    u"kf",    u"ko",   u"kt",   u"a",    u"b",    u"c",
    u"i",     u"k",     u"K",     u"p",     u"q",     u"r",     u"s",    u"t",    u"u",
};

bool isAsciiLetter(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

bool isAsciiAlnum(char16_t c)
{
    return isAsciiLetter(c) || (c >= u'0' && c <= u'9');
}

// Part of a word for a suggestion's whole-word test: ASCII letters and
// digits, and any other character outside the spaces and punctuation, so a
// run never matches inside a longer word in any script (a CJK text, without
// spaces, gets no suggestion).
bool isWordUnit(char16_t c)
{
    if (c < 0x80)
        return isAsciiAlnum(c);
    if (c == 0x00A0 || (c >= 0x00A1 && c <= 0x00BF) || c == 0x00D7 || c == 0x00F7)
        return false; // Latin-1 punctuation and signs
    if ((c >= 0x2000 && c <= 0x206F) || (c >= 0x2E00 && c <= 0x2E7F) || (c >= 0x3000 && c <= 0x303F))
        return false; // general, supplemental and CJK punctuation, the spaces
    if ((c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) ||
        (c >= 0xFF5B && c <= 0xFF65))
        return false; // fullwidth punctuation
    return true;
}

std::u16string tagName(std::u16string_view s)
{
    for (const auto name : kNames)
        if (s.starts_with(name))
            return std::u16string(name);
    std::size_t i = 0;
    while (i < s.size() && isAsciiLetter(s[i]))
        ++i;
    return std::u16string(s.substr(0, i));
}

// The tags of one block's content. Text outside tags is a comment.
std::vector<Tag> parseBlock(std::u16string_view content, std::size_t &order)
{
    std::vector<Tag> tags;
    for (auto i = content.find(u'\\'); i != std::u16string_view::npos;) {
        Tag tag;
        tag.order = order++;
        // libass skips spaces between the backslash and the name.
        std::size_t nameStart = i + 1;
        while (nameStart < content.size() && (content[nameStart] == u' ' || content[nameStart] == u'\t'))
            ++nameStart;
        tag.name = tagName(content.substr(nameStart));
        const std::size_t argsStart = nameStart + tag.name.size();
        std::size_t end = argsStart;
        if (end < content.size() && content[end] == u'(') {
            int depth = 0;
            for (; end < content.size(); ++end) {
                if (content[end] == u'(')
                    ++depth;
                else if (content[end] == u')' && --depth == 0) {
                    ++end;
                    break;
                }
            }
        } else {
            end = std::min(content.find(u'\\', argsStart), content.size());
        }
        tag.args = content.substr(argsStart, end - argsStart);
        tag.text = content.substr(i, end - i);
        tags.push_back(std::move(tag));
        i = content.find(u'\\', end);
    }
    return tags;
}

// Unclosed, nested or unmatched braces make the text malformed, as in the
// projection.
Tokens tokenize(std::u16string_view raw)
{
    Tokens out;
    std::size_t order = 0, textStart = 0;
    auto flushText = [&](std::size_t end) {
        if (end > textStart)
            out.pieces.push_back({false, textStart, end, {}});
    };
    for (std::size_t i = 0; i < raw.size();) {
        // libass shows "\{" and "\}" as braces: the text is read as having
        // no tags rather than lose one (the projection takes them as tags).
        if (raw[i] == u'}' || (raw[i] == u'\\' && i + 1 < raw.size() && (raw[i + 1] == u'{' || raw[i + 1] == u'}'))) {
            out.malformed = true;
            return out;
        }
        if (raw[i] != u'{') {
            ++i;
            continue;
        }
        const auto close = raw.find(u'}', i + 1);
        if (close == std::u16string_view::npos || raw.substr(i + 1, close - i - 1).find(u'{') != std::u16string_view::npos) {
            out.malformed = true;
            return out;
        }
        flushText(i);
        out.pieces.push_back({true, i, close + 1, parseBlock(raw.substr(i + 1, close - i - 1), order)});
        i = textStart = close + 1;
    }
    flushText(raw.size());
    return out;
}

bool isWhitespace(std::u16string_view raw, std::size_t &i)
{
    if (raw[i] == u'\\' && i + 1 < raw.size() && (raw[i + 1] == u'N' || raw[i + 1] == u'n' || raw[i + 1] == u'h')) {
        ++i;
        return true;
    }
    return raw[i] == u' ' || raw[i] == u'\t';
}

bool hasVisible(std::u16string_view raw, const Piece &piece)
{
    const auto text = raw.substr(0, piece.rawEnd);
    for (std::size_t i = piece.rawStart; i < piece.rawEnd; ++i)
        if (!isWhitespace(text, i))
            return true;
    return false;
}

bool isClip(std::u16string_view name)
{
    return name == u"clip" || name == u"iclip";
}

bool isLineWide(std::u16string_view name)
{
    for (const auto n : {u"pos", u"move", u"org", u"fad", u"fade", u"clip", u"iclip", u"an", u"a", u"q"})
        if (name == n)
            return true;
    return false;
}

bool isKaraoke(std::u16string_view name)
{
    return name == u"k" || name == u"K" || name == u"kf" || name == u"ko" || name == u"kt";
}

// ---- Formatting state ------------------------------------------------------

enum class Kind { None, Flag, Bold, Colour, Alpha, Number, Integer, Text, Transform, Reset };

struct Effect {
    Kind kind = Kind::None;
    std::vector<std::u16string> components;
};

Effect effectOf(std::u16string_view name)
{
    const std::u16string n(name);
    if (n == u"i" || n == u"u" || n == u"s")
        return {Kind::Flag, {n}};
    if (n == u"b")
        return {Kind::Bold, {n}};
    if (n == u"c" || n == u"1c")
        return {Kind::Colour, {u"1c"}};
    if (n == u"2c" || n == u"3c" || n == u"4c")
        return {Kind::Colour, {n}};
    if (n == u"alpha")
        return {Kind::Alpha, {u"1a", u"2a", u"3a", u"4a"}};
    if (n == u"1a" || n == u"2a" || n == u"3a" || n == u"4a")
        return {Kind::Alpha, {n}};
    if (n == u"fn")
        return {Kind::Text, {n}};
    if (n == u"fr" || n == u"frz")
        return {Kind::Number, {u"frz"}};
    if (n == u"bord")
        return {Kind::Number, {u"xbord", u"ybord"}};
    if (n == u"shad")
        return {Kind::Number, {u"xshad", u"yshad"}};
    if (n == u"fsc") // both scales back to the Style's, whatever follows
        return {Kind::Number, {u"fscx", u"fscy"}};
    if (n == u"fe") // a whole number (argtoi32)
        return {Kind::Integer, {n}};
    for (const auto number : {u"fs", u"fscx", u"fscy", u"fsp", u"frx", u"fry", u"fax", u"fay", u"be", u"blur",
                              u"xbord", u"ybord", u"xshad", u"yshad"})
        if (n == number)
            return {Kind::Number, {n}};
    if (n == u"t")
        return {Kind::Transform, {n}};
    if (n == u"r")
        return {Kind::Reset, {}};
    return {};
}

std::u16string trimmed(std::u16string_view s)
{
    const auto first = s.find_first_not_of(u" \t");
    if (first == std::u16string_view::npos)
        return {};
    return std::u16string(s.substr(first, s.find_last_not_of(u" \t") - first + 1));
}

std::u16string ascii(const std::string &s)
{
    return {s.begin(), s.end()};
}

bool narrow(std::u16string_view s, std::string &out)
{
    out.clear();
    for (const char16_t c : s) {
        if (c > 0x7F)
            return false;
        out.push_back(static_cast<char>(c));
    }
    return true;
}

std::u16string hex(unsigned long value, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*lX", digits, value);
    return ascii(buf);
}

// Where a number's digits start, past a '+' (strtol and strtod, which libass
// reads numbers with, take one; from_chars does not).
const char *digitsFrom(const char *first, const char *last)
{
    return last - first > 1 && *first == '+' && first[1] != '-' && first[1] != '+' ? first + 1 : first;
}

// Equal values compare equal however they are spelled.
std::u16string normalised(Kind kind, std::u16string_view arg)
{
    const auto value = trimmed(arg);
    std::string s;
    if (kind == Kind::Text || !narrow(value, s))
        return value;
    const char *first = s.data(), *last = s.data() + s.size();
    switch (kind) {
    case Kind::Flag:
    case Kind::Bold: {
        long n = 0;
        if (std::from_chars(digitsFrom(first, last), last, n).ec != std::errc{})
            return value;
        if (kind == Kind::Flag || n == 0 || n == 1)
            return n != 0 ? u"1" : u"0";
        return ascii(std::to_string(n));
    }
    case Kind::Colour:
    case Kind::Alpha: {
        // As libass's parse_color_tag: past '&' and an uppercase 'H' only,
        // then strtoll's signed hexadecimal clamped to 32 bits (-1 is
        // 0xFFFFFFFF); no digits is 0 (black, opaque).
        while (first != last && (*first == '&' || *first == 'H'))
            ++first;
        const bool negative = first != last && *first == '-';
        if (first != last && (*first == '-' || *first == '+'))
            ++first;
        if (last - first > 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X'))
            first += 2;
        unsigned long long magnitude = 0;
        if (std::from_chars(first, last, magnitude, 16).ec == std::errc::result_out_of_range)
            magnitude = std::numeric_limits<unsigned long long>::max();
        const long long limit = negative ? -static_cast<long long>(INT32_MIN) : INT32_MAX;
        const long long signedValue = negative ? -static_cast<long long>(std::min<unsigned long long>(magnitude, limit))
                                               : static_cast<long long>(std::min<unsigned long long>(magnitude, limit));
        const auto n = static_cast<std::uint32_t>(static_cast<std::int32_t>(signedValue));
        return kind == Kind::Colour ? hex(n & 0xFFFFFF, 6) : hex(n & 0xFF, 2);
    }
    case Kind::Integer: {
        long n = 0;
        if (std::from_chars(digitsFrom(first, last), last, n).ec != std::errc{})
            return value;
        return ascii(std::to_string(n));
    }
    case Kind::Number: {
        double n = 0;
        if (std::from_chars(digitsFrom(first, last), last, n).ec != std::errc{})
            return value;
        if (n == 0)
            n = 0; // -0 is 0
        char buf[32];
        const auto r = std::to_chars(buf, buf + sizeof buf, n);
        return ascii(std::string(buf, r.ptr));
    }
    default:
        return value;
    }
}

// Whether an override's value is the active Style's: none at all, or (as
// libass's ass_parse.c reads them) \i, \u and \s other than 0 or 1 and \b
// other than 0, 1 or a weight of 100 and up.
bool isStyleValue(Kind kind, std::u16string_view arg)
{
    const auto value = trimmed(arg);
    if (value.empty())
        return true;
    std::string s;
    long n = 0;
    if ((kind != Kind::Flag && kind != Kind::Bold) || !narrow(value, s)
        || std::from_chars(digitsFrom(s.data(), s.data() + s.size()), s.data() + s.size(), n).ec != std::errc{})
        return false;
    return n != 0 && n != 1 && (kind == Kind::Flag || n < 100);
}

std::u16string valueIn(const TagState &state, const std::u16string &component)
{
    const auto it = state.find(component);
    return it == state.end() ? std::u16string() : it->second;
}

std::optional<double> numberIn(std::u16string_view text)
{
    std::string s;
    if (!narrow(trimmed(text), s))
        return std::nullopt;
    const char *last = s.data() + s.size();
    double n = 0;
    if (std::from_chars(digitsFrom(s.data(), last), last, n).ec != std::errc{})
        return std::nullopt;
    return n;
}

// A number as libass keeps it (ass_parse.c): no border, \shad, scale or
// blur below 0, \blur at most 100, \be a whole number from 0 to 127 (+0.5
// and truncated, as VSFilter rounds it). Other values are kept.
std::u16string clamped(std::u16string_view name, std::u16string value)
{
    const auto n = numberIn(value);
    if (!n)
        return value;
    double v = *n;
    if (name == u"bord" || name == u"xbord" || name == u"ybord" || name == u"shad" || name == u"fscx" ||
        name == u"fscy")
        v = std::max(v, 0.0);
    else if (name == u"blur")
        v = std::clamp(v, 0.0, 100.0);
    else if (name == u"be")
        v = std::clamp(std::trunc(v + 0.5), 0.0, 127.0);
    else
        return value;
    if (v == *n)
        return value;
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return normalised(Kind::Number, ascii(std::string(buf, r.ptr)));
}

// \fs as libass reads it (ass_parse.c TAG_FS): a signed size scales the
// current one by (1 + n / 10), and none, or a size not above 0, is the
// Style's. Without a current size the spelling is kept.
std::optional<std::u16string> fontSizeAfter(const TagState &state, const TagState &defaults, std::u16string_view args)
{
    const auto arg = trimmed(args);
    const auto styleSize = valueIn(defaults, u"fs");
    const auto n = numberIn(arg);
    if (arg.empty() || !n)
        return styleSize.empty() ? std::nullopt : std::optional(styleSize);
    double size = *n;
    if (arg.front() == u'+' || arg.front() == u'-') {
        const auto current = numberIn(valueIn(state, u"fs"));
        if (!current)
            return std::nullopt;
        size = *current * (1 + *n / 10);
    }
    if (size <= 0)
        return styleSize.empty() ? std::nullopt : std::optional(styleSize);
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, size);
    return ascii(std::string(buf, r.ptr));
}

TagState defaultsFor(const StyleValues *style)
{
    TagState d{{u"i", u"0"},    {u"b", u"0"},    {u"u", u"0"},   {u"s", u"0"},   {u"be", u"0"},
               {u"blur", u"0"}, {u"fax", u"0"},  {u"fay", u"0"}, {u"frx", u"0"}, {u"fry", u"0"},
               {u"frz", u"0"},  {u"fsp", u"0"},  {u"fscx", u"100"}, {u"fscy", u"100"}};
    if (!style)
        return d;
    struct FromStyle {
        std::u8string_view tag;
        Kind kind;
        std::vector<std::u16string> components;
    };
    const FromStyle fields[] = {
        {u8"fs", Kind::Number, {u"fs"}},     {u8"bord", Kind::Number, {u"xbord", u"ybord"}},
        {u8"shad", Kind::Number, {u"xshad", u"yshad"}}, {u8"fsp", Kind::Number, {u"fsp"}},
        {u8"fscx", Kind::Number, {u"fscx"}}, {u8"fscy", Kind::Number, {u"fscy"}},
        {u8"frz", Kind::Number, {u"frz"}},   {u8"1c", Kind::Colour, {u"1c"}},
        {u8"2c", Kind::Colour, {u"2c"}},     {u8"3c", Kind::Colour, {u"3c"}},
        {u8"4c", Kind::Colour, {u"4c"}},     {u8"1a", Kind::Alpha, {u"1a"}},
        {u8"2a", Kind::Alpha, {u"2a"}},      {u8"3a", Kind::Alpha, {u"3a"}},
        {u8"4a", Kind::Alpha, {u"4a"}},      {u8"fn", Kind::Text, {u"fn"}},
        {u8"b", Kind::Bold, {u"b"}},         {u8"i", Kind::Flag, {u"i"}},
        {u8"u", Kind::Flag, {u"u"}},         {u8"s", Kind::Flag, {u"s"}},
    };
    for (const auto &field : fields) {
        const auto value = legacy::styleTagValue(*style, field.tag);
        if (!value || value->empty())
            continue;
        for (const auto &component : field.components)
            d[component] = normalised(field.kind, toUtf16(*value));
    }
    if (!style->encoding.empty()) // \fe's base
        d[u"fe"] = normalised(Kind::Integer, toUtf16(style->encoding));
    return d;
}

// One component's value after a tag.
struct Assignment {
    std::size_t order = 0;
    std::u16string value, tag;
};
// Each component's assignments in tag order.
using History = std::map<std::u16string, std::vector<Assignment>>;

std::size_t unitsOf(const TagState &state)
{
    std::size_t units = 0;
    for (const auto &[component, value] : state)
        units += component.size() + value.size();
    return units;
}

// What reading a text may copy, in UTF-16 units: the formatting state each
// text piece keeps, the Style a reset takes (its font name has no bound) and
// the history. A text past it is read as having no runs, however its tags
// stay within kMaxTagTransferTags.
constexpr std::size_t kMaxTagTransferWork = std::size_t(1) << 20;

struct Budget {
    std::size_t left = kMaxTagTransferWork;
    bool spend(std::size_t units)
    {
        if (units > left)
            return false;
        left -= units;
        return true;
    }
};

// A \t with no override tag in it that changes anything (none at all, or
// only ones libass does not know), which libass skips or that does nothing.
bool isEmptyTransform(const Tag &tag)
{
    if (tag.name != u"t")
        return false;
    std::size_t order = 0;
    for (const auto &nested : parseBlock(tag.args, order)) {
        const auto kind = effectOf(nested.name).kind;
        if ((kind != Kind::None && kind != Kind::Reset && kind != Kind::Transform) || isClip(nested.name))
            return false;
    }
    return true;
}

// A \t animating only the clip: libass animates the line's clip wherever
// it stands, a line-wide tag like \clip.
bool isClipTransform(const Tag &tag)
{
    if (tag.name != u"t")
        return false;
    std::size_t order = 0;
    bool clip = false;
    for (const auto &nested : parseBlock(tag.args, order)) {
        const auto kind = effectOf(nested.name).kind;
        if (kind != Kind::None && kind != Kind::Reset && kind != Kind::Transform)
            return false;
        clip = clip || isClip(nested.name);
    }
    return clip;
}

// Whether applying `tag` may change the formatting state.
bool changesState(const Tag &tag)
{
    return effectOf(tag.name).kind != Kind::None && !isEmptyTransform(tag);
}

// Applies `tag`, appending what it set to `history` when given. Returns the
// units it copied.
std::size_t apply(TagState &state, const Tag &tag, const TagState &defaults,
                  const std::map<std::u16string, TagState> &named, History *history)
{
    const auto effect = effectOf(tag.name);
    std::vector<std::u16string> touched = effect.components;
    std::size_t copied = 0;
    switch (effect.kind) {
    case Kind::None:
        return 0;
    case Kind::Reset: {
        for (const auto &[component, value] : state)
            touched.push_back(component);
        const auto style = std::u16string(trimmed(tag.args));
        const auto it = style.empty() ? named.end() : named.find(style);
        state = it != named.end() ? it->second : defaults;
        if (!style.empty())
            state[u"r"] = style;
        copied += unitsOf(state);
        for (const auto &[component, value] : state)
            touched.push_back(component);
        break;
    }
    case Kind::Transform: {
        if (isEmptyTransform(tag))
            return 0;
        // The transforms so far as a count and a hash of their spellings: a
        // fixed size however many there are (each history entry copies it).
        auto &t = state[u"t"];
        const std::string previous(t.begin(), t.end()); // "count|hash", ASCII
        std::uint64_t count = 0, hash = 1469598103934665603ull;
        if (const auto bar = previous.find('|'); bar != std::string::npos) {
            std::from_chars(previous.data(), previous.data() + bar, count);
            std::from_chars(previous.data() + bar + 1, previous.data() + previous.size(), hash);
        }
        for (const char16_t unit : tag.text)
            hash = (hash ^ unit) * 1099511628211ull;
        t = ascii(std::to_string(count + 1) + "|" + std::to_string(hash));
        break;
    }
    default: {
        // The Style a value falls back to is the one in effect, a named
        // reset's when there was one.
        const auto reset = state.find(u"r");
        const auto active = reset != state.end() ? named.find(reset->second) : named.end();
        const TagState &base = active != named.end() ? active->second : defaults;
        if (tag.name == u"fs") {
            if (const auto size = fontSizeAfter(state, base, tag.args))
                state[u"fs"] = *size;
            else
                state[u"fs"] = trimmed(tag.args);
            break;
        }
        // \fsc, and \fn0, are the Style's whatever follows (ass_parse.c).
        const bool styles = tag.name == u"fsc" || (tag.name == u"fn" && trimmed(tag.args) == u"0");
        for (const auto &component : effect.components) {
            if (styles || isStyleValue(effect.kind, tag.args)) {
                if (const auto it = base.find(component); it != base.end())
                    state[component] = it->second;
                else
                    state.erase(component);
            } else {
                state[component] = clamped(tag.name, normalised(effect.kind, tag.args));
            }
        }
    }
    }
    if (history)
        for (const auto &component : touched) {
            auto value = valueIn(state, component);
            copied += value.size() + tag.text.size();
            (*history)[component].push_back({tag.order, std::move(value), tag.text});
        }
    return copied;
}

TagState differences(const TagState &state, const TagState &base)
{
    TagState out;
    for (const auto &[component, value] : state)
        if (valueIn(base, component) != value)
            out[component] = value;
    for (const auto &[component, value] : base)
        if (!state.contains(component))
            out[component] = {};
    return out;
}

// A text piece and how its formatting differs from the base.
struct Segment {
    std::size_t rawStart = 0, rawEnd = 0;
    std::size_t order = 0; // tags before it
    TagState differs;
    bool visible = false;
    // The tags before it went through another state and back ("\i0\i1"):
    // two runs alike, one after the other.
    bool afterBoundary = false;
};

// Consecutive segments that differ from the base in the same way.
struct Group {
    std::size_t first = 0, last = 0;
};

std::vector<Group> groupsOf(const std::vector<Segment> &segments)
{
    std::vector<Group> groups;
    bool open = false;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (segments[i].differs.empty()) {
            open = false;
        } else if (open && !segments[i].afterBoundary && segments[groups.back().last].differs == segments[i].differs) {
            groups.back().last = i;
        } else {
            groups.push_back({i, i});
            open = true;
        }
    }
    return groups;
}

bool isDisplaySpace(char16_t c)
{
    return c == u' ' || c == u'\n' || c == u'\t' || c == u' ';
}

const TagRun *runAt(const OriginalTags &tags, int number)
{
    return number >= 1 && number <= static_cast<int>(tags.runs.size()) ? &tags.runs[number - 1] : nullptr;
}

// The tags that change the formatting from run `from` to run `to` (0 is the
// base): the closing resets, the closings, the opening resets, then the
// openings, each in the order the Original text wrote them; after a reset
// whatever of the target it changed is set again. At the body's end only the
// closings the Original wrote are kept: the others change no glyph.
std::u16string transition(const OriginalTags &tags, int from, int to, bool atEnd = false)
{
    const TagRun *f = runAt(tags, from), *t = runAt(tags, to);
    std::set<std::u16string> components;
    for (const TagRun *run : {f, t})
        if (run)
            for (const auto &[component, value] : run->differs)
                components.insert(component);
    auto valueOf = [&](const TagRun *run, const std::u16string &component) {
        if (run)
            if (const auto it = run->differs.find(component); it != run->differs.end())
                return it->second;
        return valueIn(tags.base, component);
    };
    struct Part {
        int rank; // 0 a closing reset, 1 a closing, 2 an opening reset, 3 an opening
        TagSetter setter;
    };
    std::vector<Part> parts;
    const auto isReset = [](const TagSetter &s) { return s.tag.size() > 1 && tagName(s.tag.substr(1)) == u"r"; };
    // An opening reset goes after the closings: before them they would undo it.
    auto add = [&](const TagSetter &s, bool opening) {
        if (!s.tag.empty())
            parts.push_back({(opening ? 2 : 0) + (isReset(s) ? 0 : 1), s});
    };
    for (const auto &component : components) {
        if (valueOf(f, component) == valueOf(t, component))
            continue;
        const bool opening = t && t->differs.contains(component);
        const auto &setters = opening ? t->opens : f->closes;
        if (const auto it = setters.find(component); it != setters.end() && (it->second.written || !atEnd))
            add(it->second, opening);
    }
    // Two runs alike, one after the other: closed and opened again, or they
    // would read back as one.
    if (f && t && from != to && parts.empty()) {
        for (const auto &[component, close] : f->closes)
            add(close, false);
        for (const auto &[component, open] : t->opens)
            add(open, true);
    }
    // A reset undoes every value: the target's values it undoes too are set
    // again after it.
    const bool reset = std::ranges::any_of(parts, [](const Part &p) { return p.rank % 2 == 0; });
    if (t && reset)
        for (const auto &[component, open] : t->opens)
            if (valueOf(f, component) == valueOf(t, component))
                add(open, true);
    std::ranges::stable_sort(parts, [](const Part &a, const Part &b) {
        return std::tie(a.rank, a.setter.order) < std::tie(b.rank, b.setter.order);
    });
    std::u16string out;
    std::set<std::u16string> written;
    for (const auto &part : parts)
        if (written.insert(part.setter.tag).second)
            out += part.setter.tag;
    if (!reset || (atEnd && !t))
        return out;
    // What else the reset changed (a base value the target keeps: the bold
    // of a carried leading \b1): the target run's own opening, or the value
    // spelled out.
    TagState state = tags.base, target = tags.base;
    if (f)
        for (const auto &[component, value] : f->differs)
            state[component] = value;
    if (t)
        for (const auto &[component, value] : t->differs)
            target[component] = value;
    std::size_t order = 0;
    for (const auto &tag : parseBlock(out, order))
        apply(state, tag, tags.defaults, tags.named, nullptr);
    std::set<std::u16string> all;
    for (const auto *in : {&state, &target})
        for (const auto &[component, value] : *in)
            all.insert(component);
    for (const auto &component : all) {
        const auto value = valueIn(target, component);
        if (component == u"t" || component == u"r" || valueIn(state, component) == value)
            continue;
        std::u16string setter;
        if (t)
            if (const auto it = t->opens.find(component); it != t->opens.end())
                setter = it->second.tag;
        if (setter.empty() && !value.empty()) {
            switch (effectOf(component).kind) {
            case Kind::Colour:
            case Kind::Alpha:
                setter = u"\\" + component + u"&H" + value + u"&";
                break;
            case Kind::Flag:
            case Kind::Bold:
            case Kind::Number:
            case Kind::Integer:
            case Kind::Text:
                setter = u"\\" + component + value;
                break;
            default:
                break;
            }
        }
        if (!setter.empty() && written.insert(setter).second)
            out += setter;
    }
    return out;
}

std::u16string braced(const std::u16string &tags)
{
    return tags.empty() ? std::u16string() : u"{" + tags + u"}";
}

std::vector<RunPlacement> validPlacements(const OriginalTags &tags, const TranslationDraft &draft)
{
    std::vector<RunPlacement> sorted = draft.placements;
    std::ranges::sort(sorted, {}, &RunPlacement::start);
    std::vector<RunPlacement> out;
    for (const auto &p : sorted) {
        if (!runAt(tags, p.run) || p.start >= p.end || p.end > draft.body.size())
            continue;
        if (!out.empty() && p.start < out.back().end)
            continue;
        out.push_back(p);
    }
    return out;
}

// The last tag before the `before`th to give `component` the value.
TagSetter lastSetter(const History &history, const std::u16string &component, std::size_t before,
                     const std::u16string &value, std::size_t &work)
{
    const auto it = history.find(component);
    if (it == history.end())
        return {};
    const auto &assignments = it->second;
    auto end = std::ranges::lower_bound(assignments, before, {}, &Assignment::order);
    for (auto a = end; a != assignments.begin();) {
        work += 1 + value.size();
        if ((--a)->value == value)
            return {a->order, a->tag};
    }
    return {};
}

// What a run's \t opening replays at most, in UTF-16 units; a run that
// needs more is not offered. With at most kMaxTagTransferTags runs this
// keeps every opening within kMaxTagTransferWork.
constexpr std::size_t kMaxTransformOpening = 1024;

// The \t tags from the `from`th to `last`, since the last reset: what puts
// back the transforms running after `last`.
TagSetter transformsTo(const History &history, const TagSetter &last, std::size_t from, std::size_t &work)
{
    const auto it = history.find(u"t");
    if (last.tag.empty() || it == history.end())
        return last;
    const auto &assignments = it->second;
    const auto end = std::ranges::upper_bound(assignments, last.order, {}, &Assignment::order);
    auto begin = end;
    while (begin != assignments.begin() && std::prev(begin)->order >= from && std::prev(begin)->tag.starts_with(u"\\t")) {
        ++work;
        --begin;
    }
    std::u16string tags;
    for (auto a = begin; a != end && tags.size() <= kMaxTransformOpening; ++a)
        tags += a->tag;
    return {last.order, std::move(tags)};
}

// The first tag from the `from`th on to give `component` the value.
TagSetter firstSetter(const History &history, const std::u16string &component, std::size_t from,
                      const std::u16string &value, std::size_t &work)
{
    const auto it = history.find(component);
    if (it == history.end())
        return {};
    const auto &assignments = it->second;
    for (auto a = std::ranges::lower_bound(assignments, from, {}, &Assignment::order); a != assignments.end(); ++a) {
        work += 1 + value.size();
        if (a->value == value)
            return {a->order, a->tag};
    }
    return {};
}

} // namespace

// `setter`, or the tag for `component` alone where `setter` sets more than
// it (\bord, \shad, \alpha, \fsc) and would give another of them a value
// other than `expected(other)`: "\ybord5" for "\bord5" on a run whose
// \xbord is the base's.
TagSetter alone(TagSetter setter, const std::u16string &component, const std::u16string &value,
                const std::function<std::u16string(const std::u16string &)> &expected)
{
    const std::u16string_view tag = setter.tag;
    const auto nameAt = tag.find_first_not_of(u" \t", 1);
    if (nameAt == std::u16string_view::npos)
        return setter;
    const auto name = tagName(tag.substr(nameAt));
    const auto effect = effectOf(name);
    if (effect.components.size() < 2)
        return setter;
    // \fsc gives the Style's scales, \bord and the rest one value to each.
    const bool one = name != u"fsc";
    for (const auto &other : effect.components) {
        if (other != component && (!one || expected(other) != value)) {
            // (No value is the Style's: "\1a" alone.)
            const auto spelled = effect.kind == Kind::Alpha && !value.empty() ? u"&H" + value + u"&" : value;
            setter.tag = u"\\" + component + spelled;
            return setter;
        }
    }
    return setter;
}

OriginalTags readOriginalTags(std::u16string_view original, const StyleValues *style,
                              std::span<const StyleValues> styles)
{
    OriginalTags out;
    out.defaults = defaultsFor(style);
    // A Style's values past the text's bound (a font name) would be copied
    // with every state read against it.
    if (original.size() > kMaxTagTransferUnits || unitsOf(out.defaults) > kMaxTagTransferUnits) {
        out.malformed = true;
        return out;
    }
    const auto tokens = tokenize(original);
    std::size_t tagCount = 0;
    for (const auto &piece : tokens.pieces)
        tagCount += piece.tags.size();
    if (tokens.malformed || tagCount > kMaxTagTransferTags) {
        out.malformed = true;
        return out;
    }
    Budget budget;
    const auto tooMuch = [&] {
        out = {};
        out.defaults = defaultsFor(style);
        out.malformed = true;
        return out;
    };
    bool drawingNow = false; // \p as the renderer is at each piece
    std::set<std::u16string> looked; // each name looked up once, found or not
    for (const auto &piece : tokens.pieces) {
        if (!piece.block && drawingNow) {
            out.drawing = true;
            return out;
        }
        for (const auto &tag : piece.tags) {
            if (effectOf(tag.name).kind == Kind::Reset) {
                auto name = std::u16string(trimmed(tag.args));
                if (!name.empty() && !looked.contains(name)) {
                    // The last Style of the name, as libass's lookup_style:
                    // charged, the names the text gives may be many.
                    if (!budget.spend(styles.size() + 1))
                        return tooMuch();
                    const auto u8name = toUtf8(name);
                    const auto named = std::ranges::find_if(std::views::reverse(styles),
                                                            [&](const StyleValues &s) { return s.name == u8name; });
                    if (named != std::views::reverse(styles).end()) {
                        out.named[name] = defaultsFor(&*named);
                        if (!budget.spend(unitsOf(out.named[name])))
                            return tooMuch();
                    }
                    looked.insert(std::move(name));
                }
            }
            // libass reads \p as a whole number and clamps it to 0 and up;
            // a later \p0 is text again.
            if (tag.name == u"p") {
                const auto scale = numberIn(tag.args);
                drawingNow = scale && *scale >= 1;
            }
            out.karaoke = out.karaoke || isKaraoke(tag.name);
        }
    }

    std::size_t first = tokens.pieces.size(), last = 0;
    for (std::size_t i = 0; i < tokens.pieces.size(); ++i) {
        if (!tokens.pieces[i].block && hasVisible(original, tokens.pieces[i])) {
            first = std::min(first, i);
            last = i;
        }
    }

    TagState state = out.defaults;
    bool baseSet = false;
    History history;
    std::map<std::u16string, TagSetter> leadingSetters;
    std::u16string leading, hoisted, trailing, trailingDone, leadingDone, leadingPending;
    // Spaces before a leading tag stay before it (a \h at the base size).
    auto lead = [&](std::u16string_view tag) {
        if (!leadingPending.empty()) {
            leadingDone += braced(leading);
            leadingDone += leadingPending;
            out.leadingSpaces += leadingPending.size();
            leading.clear();
            leadingPending.clear();
        }
        leading += tag;
    };
    std::vector<Segment> segments;
    std::size_t tagsSeen = 0;
    bool deviated = false; // since the last segment
    for (std::size_t i = 0; i < tokens.pieces.size(); ++i) {
        const auto &piece = tokens.pieces[i];
        const bool isLeading = i < first, isTrailing = first < tokens.pieces.size() && i > last;
        if (!isLeading && !baseSet) {
            out.base = state;
            baseSet = true;
        }
        if (!piece.block) {
            // Spaces after a trailing tag stay after it (a \h at the base
            // size, a \N's row after a reset).
            if (isLeading)
                leadingPending += original.substr(piece.rawStart, piece.rawEnd - piece.rawStart);
            if (isTrailing && (!trailing.empty() || !trailingDone.empty())) {
                const auto spaces = original.substr(piece.rawStart, piece.rawEnd - piece.rawStart);
                trailingDone += braced(trailing);
                trailingDone += spaces;
                out.trailingSpaces += spaces.size();
                trailing.clear();
            }
            if (!isLeading && !isTrailing && !out.karaoke) {
                if (!budget.spend(unitsOf(state)))
                    return tooMuch();
                segments.push_back({piece.rawStart, piece.rawEnd, tagsSeen, state, hasVisible(original, piece), deviated});
                deviated = false;
            }
            continue;
        }
        tagsSeen += piece.tags.size();
        for (const auto &tag : piece.tags) {
            if (isKaraoke(tag.name) || isEmptyTransform(tag))
                continue;
            if (isLineWide(tag.name) || isClipTransform(tag)) {
                if (isLeading)
                    lead(tag.text);
                else
                    hoisted += tag.text;
                continue;
            }
            const auto effect = effectOf(tag.name);
            if (isLeading) {
                lead(tag.text);
                for (const auto &component : effect.components)
                    leadingSetters[component] = {tag.order, tag.text};
            } else if (isTrailing) {
                // A tag that undoes a leading one; a reset undoes them all.
                if (std::ranges::any_of(effect.components, [&](const auto &c) { return leadingSetters.contains(c); })
                    || (effect.kind == Kind::Reset && !leadingSetters.empty()))
                    trailing += tag.text;
            }
            if (!budget.spend(apply(state, tag, out.defaults, out.named, &history)))
                return tooMuch();
            // (The segments hold their whole state until read.) Charged as
            // the copy it is in cost, a long font name's too.
            if (!deviated && !segments.empty() && changesState(tag)) {
                if (!budget.spend(unitsOf(state)))
                    return tooMuch();
                deviated = state != segments.back().differs;
            }
            // A leading reset sets every value of the state it gives (a
            // named one another Style's): it is what restores them after a
            // run, and what a trailing reset undoes.
            if (isLeading && effect.kind == Kind::Reset) {
                if (!budget.spend(unitsOf(state)))
                    return tooMuch();
                leadingSetters.clear(); // what it undid restores nothing (a \t)
                for (const auto &component : state | std::views::keys)
                    leadingSetters[component] = {tag.order, tag.text};
            }
        }
    }
    if (!baseSet)
        out.base = state;
    out.leading = leadingDone + braced(leading + hoisted);
    out.trailing = trailingDone + braced(trailing);

    for (auto &segment : segments)
        segment.differs = differences(segment.differs, out.base);

    const auto projection = project(original);
    for (const auto &group : groupsOf(segments)) {
        const auto &firstSegment = segments[group.first];
        const auto &lastSegment = segments[group.last];
        if (!std::any_of(segments.begin() + group.first, segments.begin() + group.last + 1,
                         [](const Segment &s) { return s.visible; }))
            continue;
        TagRun run;
        run.differs = firstSegment.differs;
        auto start = displayOffset(projection, firstSegment.rawStart);
        auto end = displayOffset(projection, lastSegment.rawEnd);
        while (start < end && isDisplaySpace(projection.text[start]))
            ++start;
        while (end > start && isDisplaySpace(projection.text[end - 1]))
            --end;
        if (start == end) // only spaces, a non-breaking one among them
            continue;
        run.displayStart = start;
        run.displayEnd = end;
        run.rawStart = firstSegment.rawStart;
        run.rawEnd = lastSegment.rawEnd;
        run.text = projection.text.substr(start, end - start);
        bool closable = true;
        // The lookups and what the run keeps (its setters spell the
        // Original's tags, a long one per component) charged.
        std::size_t work = unitsOf(run.differs) + run.text.size();
        for (const auto &[component, value] : run.differs) {
            auto open = lastSetter(history, component, firstSegment.order, value, work);
            if (component == u"t") {
                // The transforms are all those still running, after any the
                // leading block carries: the last alone is another state.
                const auto leadingT = leadingSetters.find(component);
                open = transformsTo(history, open, leadingT != leadingSetters.end() ? leadingT->second.order + 1 : 0,
                                    work);
                closable = closable && open.tag.size() <= kMaxTransformOpening;
            } else if (component == u"fs" && open.tag.starts_with(u"\\fs")) {
                // A relative size scales the current one: from the base it
                // would be another.
                const auto args = trimmed(std::u16string_view(open.tag).substr(3));
                if (!args.empty() && (args.front() == u'+' || args.front() == u'-'))
                    open.tag = u"\\fs" + value;
            }
            run.opens[component] = alone(std::move(open), component, value, [&](const std::u16string &other) {
                const auto it = run.differs.find(other);
                return it != run.differs.end() ? it->second : valueIn(out.base, other);
            });
            const auto baseValue = valueIn(out.base, component);
            TagSetter close = firstSetter(history, component, lastSegment.order, baseValue, work);
            if (close.tag.empty()) {
                if (const auto it = leadingSetters.find(component); it != leadingSetters.end())
                    close = {it->second.order, it->second.tag, false};
                else if (component != u"t" && component != u"r" && baseValue == valueIn(out.defaults, component))
                    close = {lastSegment.order, u"\\" + component, false};
            }
            closable = closable && !close.tag.empty();
            run.closes[component] = alone(std::move(close), component, baseValue,
                                          [&](const std::u16string &other) { return valueIn(out.base, other); });
            work += run.opens[component].tag.size() + run.closes[component].tag.size();
            if (!budget.spend(work))
                return tooMuch();
            work = 0;
        }
        if (!budget.spend(work))
            return tooMuch();
        // A run nothing can end (a \t the Original never resets) would go on
        // past the text it is placed on: it is not offered.
        if (!closable)
            continue;
        run.number = static_cast<int>(out.runs.size()) + 1;
        out.runs.push_back(std::move(run));
    }
    return out;
}

TranslationDraft draftOfOriginal(std::u16string_view original, const OriginalTags &tags)
{
    TranslationDraft draft;
    const auto tokens = tokenize(original);
    if (tokens.malformed || tags.drawing) {
        draft.body = original;
        return draft;
    }
    std::map<std::size_t, std::size_t> starts, ends; // raw text piece bounds -> body offsets
    std::size_t carriedLead = tags.leadingSpaces;
    for (const auto &piece : tokens.pieces) {
        if (piece.block)
            continue;
        if (carriedLead >= piece.rawEnd - piece.rawStart) { // carried
            carriedLead -= piece.rawEnd - piece.rawStart;
            continue;
        }
        starts[piece.rawStart] = draft.body.size();
        draft.body += original.substr(piece.rawStart, piece.rawEnd - piece.rawStart);
        ends[piece.rawEnd] = draft.body.size();
    }
    draft.body.resize(draft.body.size() - std::min(tags.trailingSpaces, draft.body.size())); // carried
    for (const auto &run : tags.runs)
        if (starts.contains(run.rawStart) && ends.contains(run.rawEnd))
            draft.placements.push_back({starts[run.rawStart], ends[run.rawEnd], run.number});
    return draft;
}

std::u16string composeTranslation(const OriginalTags &tags, const TranslationDraft &draft,
                                  std::vector<RunPlacement> *placed)
{
    if (placed)
        placed->clear();
    if (draft.body.empty())
        return {};
    std::u16string out = draft.carried ? tags.leading : std::u16string();
    int current = 0;
    std::size_t pos = 0, currentEnd = 0;
    auto moveTo = [&](std::size_t at, int run) {
        out.append(draft.body, pos, at - pos);
        pos = at;
        if (placed && current != 0)
            placed->back().end = out.size();
        out += braced(transition(tags, current, run, at == draft.body.size()));
        if (placed && run != 0)
            placed->push_back({out.size(), out.size(), run});
        current = run;
    };
    // Only the translator's own blocks from `from` to `to`: no text between.
    const auto onlyBlocks = [&](std::size_t from, std::size_t to) {
        while (from < to && draft.body[from] == u'{') {
            const auto close = draft.body.find(u'}', from);
            if (close == std::u16string::npos || close >= to)
                return false;
            from = close + 1;
        }
        return from == to;
    };
    for (const auto &p : validPlacements(tags, draft)) {
        // Runs side by side across the translator's tags change in one
        // block before them, as the reader reads a transition.
        if (current != 0 && currentEnd < p.start && onlyBlocks(currentEnd, p.start)) {
            moveTo(currentEnd, p.run);
            currentEnd = p.end;
            continue;
        }
        if (current != 0 && currentEnd < p.start)
            moveTo(currentEnd, 0);
        moveTo(p.start, p.run);
        currentEnd = p.end;
    }
    if (current != 0)
        moveTo(currentEnd, 0);
    out.append(draft.body, pos);
    if (draft.carried)
        out += tags.trailing;
    return out;
}

TranslationDraft readTranslation(const OriginalTags &tags, std::u16string_view translation,
                                 std::span<const int> order)
{
    TranslationDraft plain;
    if (translation.empty())
        return plain;
    auto inner = translation;
    if (!tags.leading.empty() || !tags.trailing.empty()) {
        if (translation.size() < tags.leading.size() + tags.trailing.size() || !translation.starts_with(tags.leading)
            || !translation.ends_with(tags.trailing))
            return {std::u16string(translation), {}, false};
        inner = translation.substr(tags.leading.size(), translation.size() - tags.leading.size() - tags.trailing.size());
    }
    plain.body = inner;
    if (tags.runs.empty() || inner.size() > kMaxTagTransferUnits)
        return plain;
    const auto tokens = tokenize(inner);
    std::size_t tagCount = 0;
    for (const auto &piece : tokens.pieces)
        tagCount += piece.tags.size();
    if (tokens.malformed || tagCount > kMaxTagTransferTags)
        return plain;

    // The translation's own formatting, matched to the runs.
    std::vector<Segment> segments;
    TagState state = tags.base, atText = state;
    bool deviated = false; // since the last segment, as the Original's are read
    Budget budget;
    for (const auto &piece : tokens.pieces) {
        if (piece.block) {
            for (const auto &tag : piece.tags) {
                if (!budget.spend(apply(state, tag, tags.defaults, tags.named, nullptr)))
                    return plain;
                if (!deviated && !segments.empty() && changesState(tag)) {
                    if (!budget.spend(unitsOf(state)))
                        return plain;
                    deviated = state != atText;
                }
            }
        } else {
            segments.push_back({piece.rawStart, piece.rawEnd, 0, differences(state, tags.base), true, deviated});
            deviated = false;
            atText = state;
            if (!budget.spend(unitsOf(segments.back().differs) + unitsOf(state)))
                return plain;
        }
    }
    std::vector<int> labels(segments.size(), 0); // a run, 0 for the base, -1 for the translator's own
    std::vector<bool> used(tags.runs.size(), false);
    const auto groups = groupsOf(segments);
    // Each group set against a run is charged: many groups against many
    // runs alike in all but their last value.
    const auto compared = [&](const TagRun &run) { return budget.spend(unitsOf(run.differs) + 1); };
    for (const auto &group : groups) {
        int label = -1;
        for (std::size_t r = 0; r < tags.runs.size(); ++r) {
            if (used[r])
                continue;
            if (!compared(tags.runs[r]))
                return plain;
            if (tags.runs[r].differs == segments[group.first].differs) {
                used[r] = true;
                label = tags.runs[r].number;
                break;
            }
        }
        std::fill(labels.begin() + group.first, labels.begin() + group.last + 1, label);
    }
    // Then a run under the translator's own formatting (Ctrl+B over a placed
    // italic run): its values among more, the rest staying the body's. Read
    // without these if they don't come back as the translation.
    auto under = labels;
    for (const auto &group : groups) {
        if (under[group.first] != -1 || segments[group.first].differs.empty())
            continue;
        const auto &differs = segments[group.first].differs;
        for (std::size_t r = 0; r < tags.runs.size(); ++r) {
            if (used[r])
                continue;
            if (!compared(tags.runs[r]))
                return plain;
            const bool within = std::ranges::all_of(tags.runs[r].differs, [&](const auto &value) {
                const auto it = differs.find(value.first);
                return it != differs.end() && it->second == value.second;
            });
            if (within) {
                used[r] = true;
                std::fill(under.begin() + group.first, under.begin() + group.last + 1, tags.runs[r].number);
                break;
            }
        }
    }

    // Drop each run's tags where the composer would write them, noting where.
    auto read = [&](const std::vector<int> &runOf) -> std::optional<TranslationDraft> {
        TranslationDraft draft;
        std::vector<std::size_t> boundary; // before each segment, and at the end
        std::vector<const Piece *> pending;
        auto settle = [&](int before, int after, bool atEnd) {
            before = std::max(before, 0);
            after = std::max(after, 0);
            auto expected = before != after ? transition(tags, before, after, atEnd) : std::u16string();
            // The composer ends a run at the body's end only past the body's own
            // last tags: before one of them (\b0 after Ctrl+B) it ends as inside.
            if (atEnd && before != after) {
                const auto inside = transition(tags, before, after, false);
                for (std::size_t i = 0; i + 1 < pending.size(); ++i) {
                    const auto raw = inner.substr(pending[i]->rawStart, pending[i]->rawEnd - pending[i]->rawStart);
                    if (raw.substr(1, raw.size() - 2) == inside) {
                        expected = inside;
                        break;
                    }
                }
            }
            bool found = expected.empty();
            std::size_t at = draft.body.size();
            for (const Piece *block : pending) {
                const auto raw = inner.substr(block->rawStart, block->rawEnd - block->rawStart);
                if (!found && raw.substr(1, raw.size() - 2) == expected) {
                    found = true;
                    at = draft.body.size();
                    continue;
                }
                draft.body += raw;
            }
            boundary.push_back(at);
            pending.clear();
            return found;
        };
        int previous = 0;
        std::size_t s = 0;
        for (const auto &piece : tokens.pieces) {
            if (piece.block) {
                pending.push_back(&piece);
                continue;
            }
            if (!settle(previous, runOf[s], false))
                return std::nullopt;
            draft.body += inner.substr(piece.rawStart, piece.rawEnd - piece.rawStart);
            previous = runOf[s++];
        }
        if (!settle(previous, 0, true))
            return std::nullopt;
        for (const auto &group : groups)
            if (runOf[group.first] > 0)
                draft.placements.push_back({boundary[group.first], boundary[group.last + 1], runOf[group.first]});
        if (composeTranslation(tags, draft) != translation)
            return std::nullopt;
        return draft;
    };
    // The runs as last placed, by position, first: each group with
    // formatting is the next of them, exactly or under more, or the
    // translator's own.
    if (!order.empty()) {
        std::vector<int> hinted(segments.size(), 0);
        std::vector<bool> taken(tags.runs.size(), false);
        std::size_t next = 0;
        for (const auto &group : groups) {
            const auto &differs = segments[group.first].differs;
            if (differs.empty())
                continue;
            int label = -1;
            if (next < order.size()) {
                if (const TagRun *run = runAt(tags, order[next]); run && !taken[run->number - 1]) {
                    if (!compared(*run))
                        return plain;
                    const bool within = std::ranges::all_of(run->differs, [&](const auto &value) {
                        const auto it = differs.find(value.first);
                        return it != differs.end() && it->second == value.second;
                    });
                    if (within) {
                        taken[run->number - 1] = true;
                        label = run->number;
                        ++next;
                    }
                }
            }
            std::fill(hinted.begin() + group.first, hinted.begin() + group.last + 1, label);
        }
        if (next == order.size())
            if (auto draft = read(hinted))
                return *draft;
    }
    if (under != labels)
        if (auto draft = read(under))
            return *draft;
    return read(labels).value_or(plain);
}

void shiftPlacements(std::vector<RunPlacement> &placements, std::size_t start, std::size_t end,
                     std::size_t inserted)
{
    auto moved = [&](std::size_t x) {
        if (x <= start)
            return x;
        if (x >= end)
            return x - (end - start) + inserted;
        return start + inserted;
    };
    for (auto &p : placements) {
        p.start = moved(p.start);
        p.end = moved(p.end);
    }
    std::erase_if(placements, [](const RunPlacement &p) { return p.start >= p.end; });
}

bool placeRun(std::vector<RunPlacement> &placements, std::size_t start, std::size_t end, int run)
{
    if (start >= end)
        return false;
    const RunPlacement placed{start, end, run};
    if (std::erase(placements, placed) > 0)
        return true;
    std::erase_if(placements, [&](const RunPlacement &p) {
        return p.run == run || (p.start >= start && p.end <= end);
    });
    for (auto &p : placements) {
        if (p.end <= start || p.start >= end)
            continue;
        if (p.start < start && p.end > end) {
            if (start - p.start >= p.end - end)
                p.end = start;
            else
                p.start = end;
        } else if (p.start < start) {
            p.end = start;
        } else {
            p.start = end;
        }
    }
    placements.push_back(placed);
    std::ranges::sort(placements, {}, &RunPlacement::start);
    return true;
}

namespace {

bool isPlaced(std::span<const RunPlacement> placements, int run)
{
    return std::ranges::any_of(placements, [&](const RunPlacement &p) { return p.run == run; });
}

} // namespace

int nextUnplacedRun(const OriginalTags &tags, std::span<const RunPlacement> placements)
{
    for (const auto &run : tags.runs)
        if (!isPlaced(placements, run.number))
            return run.number;
    return 0;
}

int runWithProperty(const OriginalTags &tags, std::span<const RunPlacement> placements,
                    std::u16string_view property)
{
    int placed = 0;
    for (const auto &run : tags.runs) {
        if (!run.differs.contains(std::u16string(property)))
            continue;
        if (!isPlaced(placements, run.number))
            return run.number;
        if (placed == 0)
            placed = run.number;
    }
    return placed;
}

std::vector<RunPlacement> suggestPlacements(const OriginalTags &tags, const TranslationDraft &draft)
{
    std::vector<RunPlacement> out;
    // As readTranslation: no runs past the bound, or in a text whose braces
    // don't close (a placement there would not read back).
    if (draft.body.size() > kMaxTagTransferUnits || tokenize(draft.body).malformed)
        return out;
    const auto projection = project(draft.body);
    const std::u16string_view text = projection.text;
    auto isWord = [&](std::size_t at, std::size_t length) {
        const auto &needle = text.substr(at, length);
        if (at > 0 && isWordUnit(text[at - 1]) && isWordUnit(needle.front()))
            return false;
        const auto after = at + length;
        return !(after < text.size() && isWordUnit(text[after]) && isWordUnit(needle.back()));
    };
    auto overlaps = [](std::span<const RunPlacement> list, const RunPlacement &p) {
        return std::ranges::any_of(list, [&](const RunPlacement &q) {
            return p.run != q.run && p.start < q.end && q.start < p.end;
        });
    };
    for (const auto &run : tags.runs) {
        if (run.text.empty() || isPlaced(draft.placements, run.number))
            continue;
        std::size_t found = std::u16string_view::npos, count = 0;
        for (auto at = text.find(run.text); at != std::u16string_view::npos; at = text.find(run.text, at + 1)) {
            if (isWord(at, run.text.size())) {
                found = at;
                ++count;
            }
        }
        if (count != 1)
            continue;
        const RunPlacement p{rawOffset(projection, found, true), rawOffset(projection, found + run.text.size(), false),
                             run.number};
        // Text across the translator's own tags is in more than one state,
        // which no one run can be read back over.
        const bool crossesTags = p.start < p.end && draft.body.find(u'{', p.start) < p.end;
        if (p.start < p.end && !crossesTags && !overlaps(draft.placements, p))
            out.push_back(p);
    }
    // Text two runs would both go on (the same words in two formats) says
    // which of them it is no more than for one: neither is suggested.
    const auto candidates = out;
    std::erase_if(out, [&](const RunPlacement &p) { return overlaps(candidates, p); });
    std::ranges::sort(out, {}, &RunPlacement::start);
    return out;
}

std::vector<int> matchRuns(const OriginalTags &before, const OriginalTags &now)
{
    std::vector<int> out(before.runs.size(), 0);
    std::vector<bool> taken(now.runs.size(), false);
    for (std::size_t i = 0; i < before.runs.size(); ++i) {
        const auto &run = before.runs[i];
        for (std::size_t j = 0; j < now.runs.size(); ++j) {
            if (!taken[j] && now.runs[j].differs == run.differs && now.runs[j].text == run.text) {
                taken[j] = true;
                out[i] = now.runs[j].number;
                break;
            }
        }
    }
    if (before.runs.size() == now.runs.size())
        for (std::size_t i = 0; i < before.runs.size(); ++i)
            if (out[i] == 0 && !taken[i]
                && (now.runs[i].differs == before.runs[i].differs || now.runs[i].text == before.runs[i].text)) {
                taken[i] = true;
                out[i] = now.runs[i].number;
            }
    return out;
}

std::vector<int> runOrder(std::span<const RunPlacement> placements)
{
    std::vector<RunPlacement> sorted(placements.begin(), placements.end());
    std::ranges::sort(sorted, {}, &RunPlacement::start);
    std::vector<int> out;
    for (const auto &p : sorted)
        out.push_back(p.run);
    return out;
}

void relabelRuns(const OriginalTags &tags, TranslationDraft &draft, std::span<const int> order)
{
    auto sorted = draft.placements;
    std::ranges::sort(sorted, {}, &RunPlacement::start);
    if (order.size() != sorted.size())
        return;
    std::set<int> seen;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const TagRun *read = runAt(tags, sorted[i].run), *chosen = runAt(tags, order[i]);
        if (!read || !chosen || read->differs != chosen->differs || !seen.insert(order[i]).second)
            return;
    }
    for (std::size_t i = 0; i < sorted.size(); ++i)
        sorted[i].run = order[i];
    draft.placements = std::move(sorted);
}

bool hasUnplacedRuns(const OriginalTags &tags, std::u16string_view translation)
{
    if (translation.empty() || tags.runs.empty())
        return false;
    const auto draft = readTranslation(tags, translation);
    return draft.carried && nextUnplacedRun(tags, draft.placements) != 0;
}

} // namespace hikari::core
