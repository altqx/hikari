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

void oneSyllable(karaoke_layout::Line &line)
{
    if (line.syls.size() != 1)
        return;
    Unit whole = line.syls.front();
    whole.start = whole.relStart = 0;
    whole.end = whole.relEnd = line.line.duration;
    whole.duration = line.line.duration;
    whole.kdur = line.line.kdur;
    whole.mid = line.line.duration / 2;
    whole.i = 1;
    whole.blank = whole.spaceStripped.empty();
    std::vector<Unit> chars = line.chars.empty() ? std::vector<Unit>() : line.chars.front();
    for (Unit &c : chars) {
        c.start = c.relStart = whole.start;
        c.end = c.relEnd = whole.end;
        c.duration = whole.duration;
        c.kdur = whole.kdur;
        c.mid = whole.mid;
        c.i = 1;
        c.blank = c.text.find_first_not_of(" \t") == std::string::npos || c.text == "\xE3\x80\x80";
    }
    line.syls.front() = Unit{.style = whole.style};
    line.syls.push_back(std::move(whole));
    if (!line.chars.empty()) {
        line.chars.front().clear();
        line.chars.push_back(std::move(chars));
    }
    karaoke_layout::KaraSyllable syllable =
        line.karaSyllables.empty() ? karaoke_layout::KaraSyllable{} : line.karaSyllables.front();
    syllable.i = 1;
    syllable.start = 0;
    syllable.end = syllable.duration = line.line.duration;
    syllable.kdur = line.line.kdur;
    syllable.highlights = {{0, line.line.duration, line.line.duration}};
    syllable.inlineFx.clear();
    line.karaSyllables.resize(1);
    line.karaSyllables.front() = karaoke_layout::KaraSyllable{};
    line.karaSyllables.push_back(std::move(syllable));
    line.syln = 1;
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
