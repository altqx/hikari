#include "hikari/application/position_commands.h"

#include "hikari/application/visual_transform.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <set>

namespace hikari::application::position {

namespace {

using u8 = std::u8string;
using u8v = std::u8string_view;

struct Tag {
    std::size_t begin = 0, end = 0;
    std::size_t value = 0, valueEnd = 0;
    bool operator==(const Tag &) const = default;
};

bool isDigit(char8_t c) { return c >= u8'0' && c <= u8'9'; }

bool takesParentheses(u8v name)
{
    return name == u8"pos" || name == u8"move" || name == u8"org" || name == u8"clip" || name == u8"iclip" ||
           name == u8"t";
}

bool startsValue(u8v name, u8v rest)
{
    if (takesParentheses(name))
        return !rest.empty() && rest.front() == u8'(';
    return name == u8"fn" || name == u8"r" || rest.empty() || rest.front() < u8'a' || rest.front() > u8'z';
}

std::size_t closingParenthesis(u8v text, std::size_t open)
{
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == u8'(')
            ++depth;
        else if (text[i] == u8')' && --depth == 0)
            return i;
        else if (text[i] == u8'}')
            return i - 1;
    }
    return text.size() - 1;
}

template <typename Visit>
void eachTag(u8v text, std::size_t from, std::size_t to, bool skipTransforms, Visit &&visit)
{
    to = std::min(to, text.size());
    bool inBlock = false;
    for (std::size_t i = from; i < to; ++i) {
        const char8_t c = text[i];
        if (c == u8'{') {
            inBlock = true;
            continue;
        }
        if (c == u8'}') {
            inBlock = false;
            continue;
        }
        if (!inBlock || c != u8'\\')
            continue;
        std::size_t nameEnd = i + 1;
        while (nameEnd < text.size() && ((text[nameEnd] >= u8'a' && text[nameEnd] <= u8'z') || isDigit(text[nameEnd])))
            ++nameEnd;
        if (skipTransforms && text.substr(i + 1, 2) == u8"t(") {
            i = closingParenthesis(text, i + 2);
            continue;
        }
        if (!visit(i))
            return;
    }
}

std::optional<Tag> findTag(u8v text, u8v name, std::size_t from = 0, std::size_t to = u8v::npos,
                           bool skipTransforms = false)
{
    std::optional<Tag> found;
    eachTag(text, from, to, skipTransforms, [&](std::size_t at) {
        if (text.substr(at + 1, name.size()) != name)
            return true;
        const std::size_t v = at + 1 + name.size();
        if (v > text.size() || !startsValue(name, text.substr(v)))
            return true;
        Tag t{at, 0, v, 0};
        if (takesParentheses(name)) {
            const std::size_t close = closingParenthesis(text, v);
            t.value = v + 1;
            t.valueEnd = close;
            t.end = std::min(close + 1, text.size());
        } else {
            std::size_t e = v;
            while (e < text.size() && text[e] != u8'\\' && text[e] != u8'}')
                ++e;
            t.valueEnd = e;
            t.end = e;
        }
        found = t;
        return false;
    });
    return found;
}

std::vector<Tag> allTags(u8v text, u8v name)
{
    std::vector<Tag> out;
    for (std::size_t from = 0; const auto t = findTag(text, name, from); from = t->end)
        out.push_back(*t);
    return out;
}

std::optional<double> number(u8v text)
{
    while (!text.empty() && text.front() == u8' ')
        text.remove_prefix(1);
    while (!text.empty() && text.back() == u8' ')
        text.remove_suffix(1);
    double v = 0;
    const auto *b = reinterpret_cast<const char *>(text.data());
    const auto r = std::from_chars(b, b + text.size(), v);
    if (r.ec != std::errc{} || r.ptr == b)
        return std::nullopt;
    return v;
}

std::vector<u8v> arguments(u8v text)
{
    std::vector<u8v> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
        if (i == text.size() || text[i] == u8',') {
            out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    return out;
}


int styleInt(const std::u8string &value)
{
    const auto v = number(value);
    return v ? static_cast<int>(*v) : 0;
}

std::optional<std::size_t> startBlockEnd(u8v text)
{
    if (text.empty() || text.front() != u8'{')
        return std::nullopt;
    const auto close = text.find(u8'}');
    if (close == u8v::npos)
        return std::nullopt;
    return close;
}

void putStartTag(u8 &text, u8v name, u8v tag)
{
    if (const auto close = startBlockEnd(text)) {
        if (const auto t = findTag(text, name, 0, *close + 1, true))
            text.replace(t->begin, t->end - t->begin, tag);
        else
            text.insert(*close, tag);
        return;
    }
    text.insert(0, u8"{" + u8(tag) + u8"}");
}

const std::u8string &edited(const core::LineRecord &line)
{
    return line.translation.empty() ? line.text : line.translation;
}

std::u8string &edited(core::LineRecord &line)
{
    return line.translation.empty() ? line.text : line.translation;
}

int alignmentOf(const core::LineRecord &line, const Geometry &g)
{
    if (const auto t = findTag(edited(line), u8"an"))
        if (const auto v = number(u8v(edited(line)).substr(t->value, t->valueEnd - t->value)); v && *v >= 1 && *v <= 9)
            return static_cast<int>(*v);
    const int an = styleInt(g.style(line).alignment);
    return an >= 1 && an <= 9 ? an : 2;
}

double xFraction(int an) { return an % 3 == 1 ? 0 : an % 3 == 2 ? 0.5 : 1; }
double yFraction(int an) { return an >= 7 ? 0 : an >= 4 ? 0.5 : 1; }

struct Anchor {
    double x = 0, y = 0;
    std::optional<Tag> tag;
    bool move = false;
};

Anchor anchorOf(const core::LineRecord &line, const Geometry &g)
{
    const u8v text = edited(line);
    const auto pos = findTag(text, u8"pos");
    const auto move = findTag(text, u8"move");
    const auto first = pos && (!move || pos->begin < move->begin) ? pos : move;
    if (first) {
        const auto args = arguments(text.substr(first->value, first->valueEnd - first->value));
        if (args.size() >= 2)
            if (const auto x = number(args[0]), y = number(args[1]); x && y)
                return {*x, *y, first, first == move};
    }
    const auto p = visual::transform::defaultPosition(line, g.style(line), alignmentOf(line, g), g.width, g.height);
    return {p.x, p.y, std::nullopt, false};
}

u8 withArguments(u8v name, std::span<const u8> args)
{
    u8 out = u8"\\" + u8(name) + u8"(";
    for (std::size_t i = 0; i < args.size(); ++i)
        out += (i ? u8"," : u8"") + args[i];
    return out + u8")";
}

u8 movedPosition(u8v text, const Tag &tag, bool move, double dx, double dy, bool start = true, bool end = true)
{
    std::vector<u8> args;
    for (const auto a : arguments(text.substr(tag.value, tag.valueEnd - tag.value)))
        args.emplace_back(a);
    const std::size_t points = move ? 4 : 2;
    for (std::size_t i = 0; i < std::min(points, args.size()); ++i)
        if ((i < 2 ? start : end))
            if (const auto v = number(args[i]))
                args[i] = formatted(*v + (i % 2 ? dy : dx));
    return withArguments(move ? u8"move" : u8"pos", args);
}

bool rotated(const core::LineRecord &line, const Geometry &g)
{
    if (const auto angle = number(g.style(line).angle); angle && *angle != 0)
        return true;
    for (const u8v name : {u8v(u8"frx"), u8v(u8"fry"), u8v(u8"frz"), u8v(u8"fr")})
        for (const auto &t : allTags(edited(line), name))
            if (const auto v = number(u8v(edited(line)).substr(t.value, t.valueEnd - t.value)); v && *v != 0)
                return true;
    return false;
}

u8 movedDrawing(u8v drawing, double dx, double dy, double factor)
{
    u8 out;
    bool isY = false;
    for (std::size_t i = 0; i < drawing.size();) {
        const char8_t c = drawing[i];
        if (isDigit(c) || c == u8'-' || c == u8'.') {
            std::size_t e = i + 1;
            while (e < drawing.size() && (isDigit(drawing[e]) || drawing[e] == u8'.'))
                ++e;
            const auto v = number(drawing.substr(i, e - i));
            out += v ? formatted(*v + (isY ? dy : dx) * factor) : u8(drawing.substr(i, e - i));
            isY = !isY;
            i = e;
            continue;
        }
        if (c >= u8'a' && c <= u8'z')
            isY = false;
        out += c;
        ++i;
    }
    return out;
}

u8 movedClip(u8v args, double dx, double dy)
{
    const auto parts = arguments(args);
    if (parts.size() == 4) {
        u8 joined;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto v = number(parts[i]);
            if (!v)
                return u8(args);
            joined += (i ? u8"," : u8"") + formatted(*v + (i % 2 ? dy : dx));
        }
        return joined;
    }
    if (parts.size() == 2)
        if (const auto scale = number(parts[0]); scale && *scale >= 1)
            return u8(parts[0]) + u8"," + movedDrawing(parts[1], dx, dy, std::pow(2.0, *scale - 1));
    return movedDrawing(args, dx, dy, 1);
}

u8 movedDrawings(u8v text, double dx, double dy)
{
    u8 out;
    int scale = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == u8'{') {
            const auto close = text.find(u8'}', i);
            const std::size_t end = close == u8v::npos ? text.size() : close + 1;
            for (const auto &t : allTags(text.substr(i, end - i), u8"p"))
                if (const auto v = number(text.substr(i + t.value, t.valueEnd - t.value)))
                    scale = static_cast<int>(*v);
            out += text.substr(i, end - i);
            i = end;
            continue;
        }
        const auto next = text.find(u8'{', i);
        const std::size_t end = next == u8v::npos ? text.size() : next;
        out += scale > 0 ? movedDrawing(text.substr(i, end - i), dx, dy, std::pow(2.0, scale - 1)) : u8(text.substr(i, end - i));
        i = end;
    }
    return out;
}

std::vector<const core::LineRecord *> shownSelected(const EditSession &session, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *l : session.document().lines())
        if (session.selection().selected.contains(l->id) && (!visible || visible(l->id)))
            out.push_back(l);
    return out;
}

std::expected<void, CommandRefusal> commit(EditSession &session, std::string name, std::vector<core::LineRecord> changed)
{
    if (changed.empty())
        return {};
    std::set<core::LineId> touched;
    for (const auto &c : changed)
        touched.insert(c.id);
    const auto ran = session.run(application::Command{std::move(name), session.revision(), touched, [&](core::Document &d) {
        for (const auto &c : changed)
            if (!d.editLine(c.id, [&](core::LineRecord &r) {
                    r.text = c.text;
                    r.translation = c.translation;
                    r.marginLeft = c.marginLeft;
                    r.marginRight = c.marginRight;
                    r.marginVertical = c.marginVertical;
                }))
                return false;
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

bool sameLine(const core::LineRecord &a, const core::LineRecord &b)
{
    return a.text == b.text && a.translation == b.translation && a.marginLeft.value == b.marginLeft.value &&
           a.marginRight.value == b.marginRight.value && a.marginVertical.value == b.marginVertical.value;
}

template <typename Edit>
std::expected<void, CommandRefusal> eachSelected(EditSession &session, std::string name, const LineVisible &visible,
                                                 Edit &&edit)
{
    const auto lines = shownSelected(session, visible);
    if (lines.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<core::LineRecord> changed;
    for (const auto *line : lines) {
        core::LineRecord copy = *line;
        edit(copy);
        if (!sameLine(copy, *line))
            changed.push_back(std::move(copy));
    }
    return commit(session, std::move(name), std::move(changed));
}

std::optional<std::pair<Tag, std::pair<double, double>>> plainPosition(u8v text)
{
    for (std::size_t at = text.find(u8"\\pos("); at != u8v::npos; at = text.find(u8"\\pos(", at + 1)) {
        std::size_t i = at + 5;
        const auto plain = [&](std::size_t &k) {
            const std::size_t s = k;
            if (k < text.size() && text[k] == u8'-')
                ++k;
            const std::size_t digits = k;
            while (k < text.size() && (isDigit(text[k]) || text[k] == u8'.'))
                ++k;
            return k > digits ? number(text.substr(s, k - s)) : std::nullopt;
        };
        const auto x = plain(i);
        if (!x || i >= text.size() || text[i] != u8',')
            continue;
        ++i;
        const auto y = plain(i);
        if (!y || i >= text.size() || text[i] != u8')')
            continue;
        return std::pair{Tag{at, i + 1, at + 5, i}, std::pair{*x, *y}};
    }
    return std::nullopt;
}

int petzkuAlignment(const core::LineRecord &line, const Geometry &g)
{
    const u8v text = edited(line);
    if (const auto at = text.find(u8"\\an"); at != u8v::npos && at + 3 < text.size() && isDigit(text[at + 3]))
        return text[at + 3] - u8'0';
    return styleInt(g.style(line).alignment);
}

double luaRound(double v) { return std::floor(v + 0.5); }

}

std::u8string formatted(double v)
{
    if (std::abs(v) < 0.005)
        v = 0;
    std::string s = std::format("{:.2f}", v);
    while (s.back() == '0')
        s.pop_back();
    if (s.back() == '.')
        s.pop_back();
    return u8(s.begin(), s.end());
}

std::u8string startTag(std::u8string_view text, std::u8string_view name)
{
    const auto close = startBlockEnd(text);
    if (!close)
        return {};
    const auto t = findTag(text, name, 0, *close + 1, true);
    return t ? u8(text.substr(t->begin, t->end - t->begin)) : u8();
}

std::u8string startBlock(std::u8string_view text)
{
    const auto close = startBlockEnd(text);
    return close ? u8(text.substr(0, *close + 1)) : u8();
}

std::u8string withStartTag(std::u8string_view text, std::u8string_view name, std::u8string_view tag)
{
    u8 out(text);
    putStartTag(out, name, tag);
    return out;
}

std::u8string withStartBlock(std::u8string_view text, std::u8string_view block)
{
    const auto close = startBlockEnd(text);
    return u8(block) + u8(close ? text.substr(*close + 1) : text);
}

std::optional<Box> box(const core::LineRecord &line, const Geometry &g)
{
    const auto size = g.size(line);
    if (!size)
        return std::nullopt;
    const Anchor a = anchorOf(line, g);
    const int an = alignmentOf(line, g);
    Box b;
    b.left = a.x - xFraction(an) * size->width;
    b.top = a.y - yFraction(an) * size->height;
    b.right = b.left + size->width;
    b.bottom = b.top + size->height;
    return b;
}

std::u8string changeAlignment(const core::LineRecord &line, int an, const Geometry &g)
{
    u8 text = edited(line);
    const int from = alignmentOf(line, g);
    if (an < 1 || an > 9 || an == from)
        return text;
    const auto size = g.size(line).value_or(Size{});
    const double dx = (xFraction(an) - xFraction(from)) * size.width;
    const double dy = (yFraction(an) - yFraction(from)) * size.height;
    const Anchor a = anchorOf(line, g);
    const bool pinOrigin = rotated(line, g) && !findTag(text, u8"org");
    if (a.tag)
        text.replace(a.tag->begin, a.tag->end - a.tag->begin, movedPosition(text, *a.tag, a.move, dx, dy));
    else
        putStartTag(text, u8"pos", u8"\\pos(" + formatted(a.x + dx) + u8"," + formatted(a.y + dy) + u8")");
    if (pinOrigin)
        putStartTag(text, u8"org", u8"\\org(" + formatted(a.x) + u8"," + formatted(a.y) + u8")");
    putStartTag(text, u8"an", u8"\\an" + u8(1, static_cast<char8_t>(u8'0' + an)));
    return text;
}

std::u8string shifted(const core::LineRecord &line, double dx, double dy, const Geometry &g, Parts parts)
{
    u8 text = edited(line);
    if (dx == 0 && dy == 0)
        return text;
    const Anchor a = anchorOf(line, g);
    if (a.tag) {
        if (a.move ? parts.moveStart || parts.moveEnd : parts.position)
            text.replace(a.tag->begin, a.tag->end - a.tag->begin,
                         movedPosition(text, *a.tag, a.move, dx, dy, a.move ? parts.moveStart : true, parts.moveEnd));
    } else if (parts.position) {
        putStartTag(text, u8"pos", u8"\\pos(" + formatted(a.x + dx) + u8"," + formatted(a.y + dy) + u8")");
    }
    for (const u8v name : {u8v(u8"org"), u8v(u8"clip"), u8v(u8"iclip")}) {
        if (name == u8"org" ? !parts.origin : !parts.clip)
            continue;
        const auto tags = allTags(text, name);
        for (auto t = tags.rbegin(); t != tags.rend(); ++t) {
            const u8v args = u8v(text).substr(t->value, t->valueEnd - t->value);
            u8 replacement;
            if (name == u8"org") {
                const auto values = arguments(args);
                const auto x = values.size() == 2 ? number(values[0]) : std::nullopt;
                const auto y = values.size() == 2 ? number(values[1]) : std::nullopt;
                if (!x || !y)
                    continue;
                replacement = formatted(*x + dx) + u8"," + formatted(*y + dy);
            } else {
                replacement = movedClip(args, dx, dy);
            }
            text.replace(t->value, t->valueEnd - t->value, replacement);
        }
    }
    if (parts.drawing)
        text = movedDrawings(text, dx, dy);
    return text;
}

bool positionToMargins(core::LineRecord &line, const Geometry &g)
{
    u8 &text = edited(line);
    if (findTag(text, u8"move"))
        return false;
    const auto found = plainPosition(text);
    if (!found)
        return false;
    const auto [x, y] = found->second;
    const auto style = g.style(line);
    const int an = petzkuAlignment(line, g);
    const int row = (an - 1) / 3;
    if (row == 2)
        line.marginVertical.value = static_cast<std::int64_t>(luaRound(y));
    else if (row == 0)
        line.marginVertical.value = static_cast<std::int64_t>(luaRound(g.height - y));
    const int styleL = styleInt(style.marginLeft), styleR = styleInt(style.marginRight);
    const double left = line.marginLeft.value != 0 ? static_cast<double>(line.marginLeft.value) : styleL;
    const double right = line.marginRight.value != 0 ? static_cast<double>(line.marginRight.value) : styleR;
    const double buffer = (left + right) / 2;
    double ml = 0, mr = 0;
    switch (an % 3) {
    case 2:
        ml = buffer + (x - g.width / 2.0);
        mr = buffer - (x - g.width / 2.0);
        break;
    case 1:
        ml = x;
        mr = 2 * buffer - x;
        break;
    default:
        ml = 2 * buffer + (x - g.width);
        mr = -(x - g.width);
        break;
    }
    if (ml == 0) {
        ml = -1;
        mr -= 1;
    }
    if (mr == 0) {
        mr = -1;
        ml -= 1;
    }
    if (ml == styleL)
        ml = 0;
    if (mr == styleR)
        mr = 0;
    line.marginLeft.value = static_cast<std::int64_t>(luaRound(ml));
    line.marginRight.value = static_cast<std::int64_t>(luaRound(mr));
    text.erase(found->first.begin, found->first.end - found->first.begin);
    if (const auto empty = text.find(u8"{}"); empty != u8::npos)
        text.erase(empty, 2);
    return true;
}

bool marginsToPosition(core::LineRecord &line, const Geometry &g)
{
    u8 &text = edited(line);
    if (findTag(text, u8"pos") || findTag(text, u8"move"))
        return false;
    const auto style = g.style(line);
    const double l = line.marginLeft.value != 0 ? static_cast<double>(line.marginLeft.value) : styleInt(style.marginLeft);
    const double r = line.marginRight.value != 0 ? static_cast<double>(line.marginRight.value) : styleInt(style.marginRight);
    const double v = line.marginVertical.value != 0 ? static_cast<double>(line.marginVertical.value)
                                                    : styleInt(style.marginVertical);
    const int an = petzkuAlignment(line, g);
    const double x = an % 3 == 1 ? l : an % 3 == 2 ? (l + g.width - r) / 2 : g.width - r;
    const int row = (an - 1) / 3;
    const double y = row == 2 ? v : row == 0 ? g.height - v : g.height / 2.0;
    const auto whole = [](double n) { return std::format("{}", static_cast<long long>(std::trunc(n))); };
    const std::string pos = "{\\pos(" + whole(x) + "," + whole(y) + ")}";
    text.insert(0, u8(pos.begin(), pos.end()));
    if (const auto joint = text.find(u8"}{"); joint != u8::npos)
        text.erase(joint, 2);
    return true;
}

std::u16string phantom(std::u16string_view text, Keep keep, std::optional<std::pair<std::size_t, std::size_t>> selection)
{
    static constexpr std::u16string_view hide = u"{\\alpha&HFF&}", show = u"{\\alpha}";
    std::size_t start = 0;
    if (!text.empty() && text.front() == u'{')
        if (const auto close = text.find(u'}'); close != std::u16string_view::npos && close + 1 < text.size())
            start = close + 1;
    const auto apply = [&](std::u16string_view beg, std::u16string_view mid, std::u16string_view end) {
        std::u16string out;
        if (keep == Keep::Start)
            out.append(hide).append(mid).append(show).append(beg).append(mid).append(hide).append(end);
        else
            out.append(hide).append(beg).append(show).append(mid).append(end).append(hide).append(mid);
        return out;
    };
    std::u16string out(text.substr(0, start));
    std::size_t row = start;
    while (true) {
        const auto br = text.find(u"\\N", row);
        const std::size_t rowEnd = br == std::u16string_view::npos ? text.size() : br;
        const auto part = text.substr(row, rowEnd - row);
        if (selection && selection->first != selection->second) {
            const std::size_t from = std::max(selection->first, row), to = std::min(selection->second, rowEnd);
            if (from < to)
                out += apply(text.substr(row, from - row), text.substr(from, to - from), text.substr(to, rowEnd - to));
            else
                out += part;
        } else {
            const auto a = part.find(u"{}");
            const auto b = a == std::u16string_view::npos ? a : part.find(u"{}", a + 2);
            if (b != std::u16string_view::npos)
                out += apply(part.substr(0, a), part.substr(a + 2, b - a - 2), part.substr(b + 2));
            else
                out += part;
        }
        if (br == std::u16string_view::npos)
            break;
        out += u"\\N";
        row = br + 2;
    }
    return out;
}

std::u8string_view cycleTagName(CycleTag tag)
{
    switch (tag) {
    case CycleTag::Blur:
        return u8"blur";
    case CycleTag::Bord:
        return u8"bord";
    case CycleTag::Shad:
        return u8"shad";
    case CycleTag::Alpha:
        return u8"alpha";
    case CycleTag::An:
        return u8"an";
    case CycleTag::Fsp:
        return u8"fsp";
    }
    return u8"blur";
}

std::vector<std::u8string> defaultCycleValues(CycleTag tag)
{
    const auto list = [](std::initializer_list<const char8_t *> values) {
        std::vector<std::u8string> out;
        for (const auto *v : values)
            out.emplace_back(v);
        return out;
    };
    switch (tag) {
    case CycleTag::Blur:
        return list({u8"0.6", u8"0.8", u8"1", u8"1.2", u8"1.5", u8"2", u8"2.5", u8"3", u8"4", u8"5", u8"6", u8"8",
                     u8"10", u8"0.4", u8"0.5"});
    case CycleTag::Bord:
        return list({u8"0", u8"1", u8"1.5", u8"2", u8"2.5", u8"3", u8"4", u8"5", u8"6", u8"7", u8"8", u8"9", u8"10",
                     u8"11", u8"12", u8"15", u8"20"});
    case CycleTag::Shad:
        return list({u8"0", u8"1", u8"1.5", u8"2", u8"2.5", u8"3", u8"4", u8"5", u8"6", u8"7", u8"8", u8"9", u8"10",
                     u8"11", u8"12"});
    case CycleTag::Alpha:
        return list({u8"FF", u8"00", u8"10", u8"30", u8"60", u8"80", u8"A0", u8"C0", u8"E0"});
    case CycleTag::An:
        return list({u8"1", u8"2", u8"3", u8"4", u8"5", u8"6", u8"7", u8"8", u8"9"});
    case CycleTag::Fsp:
        return list({u8"0", u8"1", u8"2", u8"3", u8"4", u8"5", u8"6", u8"7", u8"8", u8"10", u8"12", u8"15", u8"20",
                     u8"30"});
    }
    return {};
}

std::u8string cycle(std::u8string_view source, CycleTag tag, std::span<const std::u8string> values, bool back)
{
    u8 text(source);
    if (values.empty())
        return text;
    const bool alpha = tag == CycleTag::Alpha;
    const auto valueOf = [&](u8v v) -> std::optional<double> {
        if (!alpha)
            return number(v);
        while (!v.empty() && (v.front() == u8'&' || v.front() == u8'H' || v.front() == u8'h'))
            v.remove_prefix(1);
        while (!v.empty() && v.back() == u8'&')
            v.remove_suffix(1);
        unsigned n = 0;
        const auto *b = reinterpret_cast<const char *>(v.data());
        const auto r = std::from_chars(b, b + v.size(), n, 16);
        if (r.ec != std::errc{} || r.ptr == b)
            return std::nullopt;
        return static_cast<double>(n);
    };
    const auto written = [&](const u8 &v) { return u8"\\" + u8(cycleTagName(tag)) + (alpha ? u8"&H" + v + u8"&" : v); };
    const u8v name = cycleTagName(tag);
    const auto close = startBlockEnd(text);
    const auto found = close ? findTag(text, name, 0, *close + 1, true) : std::nullopt;
    if (!found) {
        putStartTag(text, name, written(values.front()));
        return text;
    }
    const auto current = valueOf(u8v(text).substr(found->value, found->valueEnd - found->value));
    const auto equal = [&](const u8 &v) {
        const auto n = valueOf(v);
        return current && n && std::abs(*n - *current) < 1e-9;
    };
    std::size_t next = 0;
    if (const auto on = std::ranges::find_if(values, equal); on != values.end()) {
        const auto at = static_cast<std::size_t>(on - values.begin());
        next = back ? (at + values.size() - 1) % values.size() : (at + 1) % values.size();
    } else {
        std::optional<double> best;
        next = back ? values.size() - 1 : 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto n = valueOf(values[i]);
            if (!current || !n || (back ? *n >= *current : *n <= *current))
                continue;
            if (!best || (back ? *n > *best : *n < *best)) {
                best = n;
                next = i;
            }
        }
    }
    text.replace(found->begin, found->end - found->begin, written(values[next]));
    return text;
}

std::expected<void, CommandRefusal> changeAlignment(EditSession &session, int an, const Geometry &g,
                                                    const LineVisible &visible)
{
    return eachSelected(session, "Change alignment", visible,
                        [&](core::LineRecord &l) { edited(l) = changeAlignment(l, an, g); });
}

std::expected<void, CommandRefusal> align(EditSession &session, Edge edge, RelativeTo to, const Geometry &g,
                                          const LineVisible &visible)
{
    std::optional<Box> reference;
    if (to == RelativeTo::VideoFrame) {
        reference = Box{0, 0, static_cast<double>(g.width), static_cast<double>(g.height)};
    } else if (const auto active = session.selection().active) {
        for (const auto *l : session.document().lines())
            if (l->id == *active)
                reference = box(*l, g);
    }
    if (!reference)
        return std::unexpected(CommandRefusal::Invalid);
    static constexpr const char *names[] = {"Align left edges", "Align centres", "Align right edges",
                                            "Align top edges", "Align middles", "Align bottom edges"};
    return eachSelected(session, names[static_cast<int>(edge)], visible, [&](core::LineRecord &l) {
        const auto b = box(l, g);
        if (!b)
            return;
        double dx = 0, dy = 0;
        switch (edge) {
        case Edge::Left:
            dx = reference->left - b->left;
            break;
        case Edge::Centre:
            dx = (reference->left + reference->right) / 2 - (b->left + b->right) / 2;
            break;
        case Edge::Right:
            dx = reference->right - b->right;
            break;
        case Edge::Top:
            dy = reference->top - b->top;
            break;
        case Edge::Middle:
            dy = (reference->top + reference->bottom) / 2 - (b->top + b->bottom) / 2;
            break;
        case Edge::Bottom:
            dy = reference->bottom - b->bottom;
            break;
        }
        edited(l) = shifted(l, dx, dy, g);
    });
}

std::expected<void, CommandRefusal> distribute(EditSession &session, Axis axis, const Geometry &g,
                                               const LineVisible &visible)
{
    struct Placed {
        const core::LineRecord *line;
        double centre;
    };
    std::vector<Placed> placed;
    for (const auto *l : shownSelected(session, visible))
        if (const auto b = box(*l, g))
            placed.push_back({l, axis == Axis::Horizontal ? (b->left + b->right) / 2 : (b->top + b->bottom) / 2});
    if (placed.size() < 3)
        return std::unexpected(CommandRefusal::Invalid);
    std::ranges::stable_sort(placed, {}, &Placed::centre);
    const double first = placed.front().centre, step = (placed.back().centre - first) / static_cast<double>(placed.size() - 1);
    std::vector<core::LineRecord> changed;
    for (std::size_t i = 1; i + 1 < placed.size(); ++i) {
        const double d = first + step * static_cast<double>(i) - placed[i].centre;
        core::LineRecord copy = *placed[i].line;
        edited(copy) = axis == Axis::Horizontal ? shifted(copy, d, 0, g) : shifted(copy, 0, d, g);
        if (!sameLine(copy, *placed[i].line))
            changed.push_back(std::move(copy));
    }
    return commit(session, axis == Axis::Horizontal ? "Distribute horizontally" : "Distribute vertically",
                  std::move(changed));
}

std::expected<void, CommandRefusal> positionToMargins(EditSession &session, const Geometry &g, const LineVisible &visible)
{
    return eachSelected(session, "Position to margins", visible, [&](core::LineRecord &l) { positionToMargins(l, g); });
}

std::expected<void, CommandRefusal> marginsToPosition(EditSession &session, const Geometry &g, const LineVisible &visible)
{
    return eachSelected(session, "Margins to position", visible, [&](core::LineRecord &l) { marginsToPosition(l, g); });
}

std::expected<void, CommandRefusal> phantom(EditSession &session, Keep keep,
                                            std::optional<std::pair<std::size_t, std::size_t>> selection)
{
    const auto active = session.selection().active;
    if (!active)
        return std::unexpected(CommandRefusal::Invalid);
    for (const auto *l : session.document().lines()) {
        if (l->id != *active)
            continue;
        core::LineRecord copy = *l;
        edited(copy) = core::toUtf8(phantom(core::toUtf16(edited(*l)), keep, selection));
        std::vector<core::LineRecord> changed;
        if (!sameLine(copy, *l))
            changed.push_back(std::move(copy));
        return commit(session, keep == Keep::Start ? "Phantom: keep the start aligned" : "Phantom: keep the end aligned",
                      std::move(changed));
    }
    return std::unexpected(CommandRefusal::Invalid);
}

std::expected<void, CommandRefusal> cycle(EditSession &session, CycleTag tag, std::span<const std::u8string> values,
                                          bool back, const LineVisible &visible)
{
    const auto name = std::string(back ? "Cycle back \\" : "Cycle \\") +
                      std::string(reinterpret_cast<const char *>(cycleTagName(tag).data()), cycleTagName(tag).size());
    return eachSelected(session, name, visible, [&](core::LineRecord &l) { edited(l) = cycle(edited(l), tag, values, back); });
}

}
