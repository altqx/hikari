#include "hikari/application/typesetting_line.h"

#include <charconv>
#include <cmath>
#include <vector>

namespace hikari::application {

namespace {

using karaoke_layout::Unit;

std::optional<double> numberIn(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    double v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size() || !std::isfinite(v))
        return std::nullopt;
    return v;
}

std::vector<std::string_view> argumentsOf(std::string_view inside)
{
    std::vector<std::string_view> out;
    for (std::size_t at = 0;;) {
        const std::size_t comma = inside.find(',', at);
        out.push_back(inside.substr(at, comma == std::string_view::npos ? std::string_view::npos : comma - at));
        if (comma == std::string_view::npos)
            return out;
        at = comma + 1;
    }
}

std::optional<std::pair<double, double>> pointOf(std::string_view inside, std::size_t minArgs)
{
    const auto args = argumentsOf(inside);
    if (args.size() < minArgs)
        return std::nullopt;
    const auto x = numberIn(args[0]), y = numberIn(args[1]);
    if (!x || !y)
        return std::nullopt;
    return std::pair{*x, *y};
}

// Takes the tags out of one block's body, and the first \an, \pos and \move it sets.
std::string scanBlock(std::string_view body, LinePlacement &out, bool &posSet, bool &anSet)
{
    std::string kept;
    for (std::size_t at = 0; at < body.size();) {
        const bool tag = body[at] == '\\';
        const auto starts = [&](std::string_view name) { return tag && body.substr(at + 1, name.size()) == name; };
        if (starts("pos(") || starts("move(")) {
            const std::size_t open = body.find('(', at);
            const std::size_t close = body.find(')', open);
            const std::size_t end = close == std::string_view::npos ? body.size() : close + 1;
            if (!posSet) {
                const std::string_view inside = body.substr(open + 1, (close == std::string_view::npos ? body.size() : close) - open - 1);
                if (auto p = pointOf(inside, starts("pos(") ? 2 : 4)) {
                    out.pos = p;
                    posSet = true;
                }
            }
            at = end;
            continue;
        }
        if (!anSet && starts("an") && at + 3 < body.size() && body[at + 3] >= '1' && body[at + 3] <= '9') {
            out.align = body[at + 3] - '0';
            anSet = true;
        }
        kept += body[at];
        ++at;
    }
    return kept;
}

void shift(Unit &u, double dx, double dy, int align)
{
    u.left += dx;
    u.center += dx;
    u.right += dx;
    u.top += dy;
    u.middle += dy;
    u.bottom += dy;
    const int column = (align - 1) % 3;
    u.x = column == 0 ? u.left : column == 1 ? u.center : u.right;
    u.y = align >= 7 ? u.top : align >= 4 ? u.middle : u.bottom;
}

bool hasKaraokeTags(std::string_view text)
{
    for (std::size_t open = text.find('{'); open != std::string_view::npos; open = text.find('{', open + 1)) {
        const std::size_t close = text.find('}', open);
        const std::string_view body = text.substr(open, close == std::string_view::npos ? text.npos : close - open);
        for (std::size_t at = body.find('\\'); at != std::string_view::npos; at = body.find('\\', at + 1)) {
            std::string_view tag = body.substr(at + 1);
            if (tag.starts_with("kf") || tag.starts_with("ko"))
                tag.remove_prefix(2);
            else if (tag.starts_with('k') || tag.starts_with('K'))
                tag.remove_prefix(1);
            else
                continue;
            if (!tag.empty() && tag.front() >= '0' && tag.front() <= '9')
                return true;
        }
    }
    return false;
}

void spanTheLine(Unit &u, const Unit &line)
{
    u.start = u.relStart = 0;
    u.end = u.relEnd = line.duration;
    u.duration = line.duration;
    u.kdur = line.kdur;
    u.mid = line.duration / 2;
    u.i = 1;
    u.blank = u.text.find_first_not_of(" \t") == std::string::npos || u.text == "\xE3\x80\x80";
}

void oneSyllable(karaoke_layout::Line &line)
{
    if (line.syls.size() == 1) {
        line.syls.push_back(line.syls.front());
        line.syls.front() = Unit{.style = line.syls.back().style};
        if (!line.chars.empty()) {
            line.chars.push_back(std::move(line.chars.front()));
            line.chars.front().clear();
        }
        line.karaSyllables.resize(1);
        line.karaSyllables.push_back(line.karaSyllables.front());
        line.karaSyllables.front() = karaoke_layout::KaraSyllable{};
    }
    for (std::size_t s = 1; s < line.syls.size(); ++s) {
        spanTheLine(line.syls[s], line.line);
        line.syls[s].blank = line.syls[s].spaceStripped.empty();
        if (s < line.chars.size())
            for (Unit &c : line.chars[s])
                spanTheLine(c, line.line);
    }
    for (std::size_t s = 1; s < line.karaSyllables.size(); ++s) {
        karaoke_layout::KaraSyllable &k = line.karaSyllables[s];
        k.i = 1;
        k.start = 0;
        k.end = k.duration = line.line.duration;
        k.kdur = line.line.kdur;
        k.highlights = {{0, line.line.duration, line.line.duration}};
        k.inlineFx.clear();
    }
    line.syln = static_cast<int>(line.syls.size()) - 1;
}

}

LinePlacement linePlacement(std::string_view text, int styleAlign)
{
    LinePlacement out;
    out.align = styleAlign;
    bool posSet = false, anSet = false;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t open = text.find('{', at);
        if (open == std::string_view::npos) {
            out.rawWithoutPosition += text.substr(at);
            break;
        }
        const std::size_t close = text.find('}', open);
        if (close == std::string_view::npos) {
            out.rawWithoutPosition += text.substr(at);
            break;
        }
        out.rawWithoutPosition += text.substr(at, open - at);
        const std::string_view body = text.substr(open + 1, close - open - 1);
        const std::string kept = scanBlock(body, out, posSet, anSet);
        if (!kept.empty() || body.empty())
            out.rawWithoutPosition += "{" + kept + "}";
        at = close + 1;
    }
    return out;
}

void placeTypesettingLine(karaoke_layout::Line &line)
{
    const int styleAlign = line.style ? line.style->align : 2;
    const LinePlacement placement = linePlacement(line.line.raw, styleAlign);
    line.line.raw = placement.rawWithoutPosition;
    const int column = (placement.align - 1) % 3;
    line.kara.halign = column == 0 ? "left" : column == 1 ? "center" : "right";
    line.kara.valign = placement.align <= 3 ? "bottom" : placement.align <= 6 ? "middle" : "top";
    if (!hasKaraokeTags(line.macro.text))
        oneSyllable(line);
    if (line.syls.size() == 2 && line.syls[1].raw == line.macro.text)
        line.syls[1].raw = placement.rawWithoutPosition;
    if (!placement.pos)
        return;
    const int align = placement.align;
    const double w = line.line.width, h = line.line.height;
    const double left = placement.pos->first - (column == 0 ? 0 : column == 1 ? w / 2 : w);
    const double top = placement.pos->second - (align >= 7 ? 0 : align >= 4 ? h / 2 : h);
    const double dx = left - line.line.left, dy = top - line.line.top;
    shift(line.line, dx, dy, align);
    line.line.x = placement.pos->first;
    line.line.y = placement.pos->second;
    for (Unit &u : line.syls)
        shift(u, dx, dy, align);
    for (auto &chars : line.chars)
        for (Unit &u : chars)
            shift(u, dx, dy, align);
}

}
