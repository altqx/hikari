#include "hikari/application/text_commands.h"

#include "hikari/application/grid_split.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <span>

namespace hikari::application::text_commands {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;
using core::SpanKind;

bool isSurrogateLead(char16_t c) { return c >= 0xD800 && c <= 0xDBFF; }

std::size_t codePointLength(u16v text, std::size_t at)
{
    return at + 1 < text.size() && isSurrogateLead(text[at]) ? 2 : 1;
}

bool isSpace(char16_t c) { return c == u' ' || c == u'\t' || (c >= u'\n' && c <= u'\r'); }

bool isWhite(char16_t c) { return isSpace(c) || c == u'\u3000'; }

const core::ProjectionSpan *spanAt(const core::Projection &p, std::size_t display)
{
    for (const auto &s : p.spans)
        if (s.displayStart <= display && display < s.displayEnd)
            return &s;
    return nullptr;
}

struct Cell {
    u16 text;
    std::optional<std::size_t> span;
    std::size_t run = 0;
};

std::vector<Cell> cellsOf(u16v raw, const core::Projection &p)
{
    std::vector<Cell> cells;
    std::size_t run = 0;
    for (std::size_t i = 0; i < p.spans.size(); ++i) {
        const auto &s = p.spans[i];
        if (s.kind != SpanKind::Text)
            ++run;
        switch (s.kind) {
        case SpanKind::Text:
            for (std::size_t at = s.rawStart; at < s.rawEnd;) {
                const auto n = std::min(codePointLength(raw, at), s.rawEnd - at);
                cells.push_back({u16(raw.substr(at, n)), i, run});
                at += n;
            }
            break;
        case SpanKind::HardBreak:
        case SpanKind::SoftBreak:
        case SpanKind::HardSpace:
            cells.push_back({u" ", std::nullopt, run});
            break;
        case SpanKind::DrawingPayload:
        case SpanKind::Malformed:
            cells.push_back({u"\uFFFC", std::nullopt, run});
            break;
        case SpanKind::HiddenOverride:
            break;
        }
    }
    return cells;
}

u16 rebuild(u16v raw, const core::Projection &p, const std::vector<Cell> &cells)
{
    u16 out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < p.spans.size(); ++i) {
        const auto &s = p.spans[i];
        if (s.kind != SpanKind::Text) {
            out += raw.substr(s.rawStart, s.rawEnd - s.rawStart);
            continue;
        }
        while (next < cells.size() && cells[next].span != i)
            ++next;
        while (next < cells.size() && cells[next].span == i)
            out += cells[next++].text;
    }
    return out;
}

u16 mapTextSpans(u16v raw, const core::Projection &p, const std::function<u16(u16v)> &map)
{
    u16 out;
    for (const auto &s : p.spans) {
        const auto piece = raw.substr(s.rawStart, s.rawEnd - s.rawStart);
        out += s.kind == SpanKind::Text ? map(piece) : u16(piece);
    }
    return out;
}

bool isFluxSeparator(char16_t c)
{
    return isSpace(c) || (c < 0x80 && u16v(u"`~!@#$%^&*()_=+[]{};:\"\\|,./<>-").find(c) != u16v::npos);
}

constexpr std::u16string_view kLeadPunct[] = {u"'", u"‘", u"“", u"«", u"‹", u"„", u"¿", u"¡", u"…"};
constexpr std::u16string_view kTrailPunct[] = {u"'", u"’", u"”", u"»", u"›", u"…"};
constexpr std::u16string_view kSmallWords[] = {u"a",  u"an", u"the", u"at", u"by",  u"for", u"in", u"of", u"on",
                                               u"to", u"up", u"and", u"as", u"but", u"or",  u"nor", u"via", u"vs"};

bool listed(std::span<const std::u16string_view> list, u16v text)
{
    return std::ranges::find(list, text) != list.end();
}

struct Word {
    std::size_t first, last;
    bool afterStop = false;
};

std::vector<Word> wordsOf(const std::vector<Cell> &cells)
{
    std::vector<Word> words;
    bool stop = false;
    for (std::size_t i = 0; i < cells.size();) {
        const auto &c = cells[i];
        if (!c.span || isFluxSeparator(c.text[0])) {
            if (c.span && u16v(u".!?:").find(c.text[0]) != u16v::npos)
                stop = true;
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < cells.size() && cells[end].span && cells[end].run == c.run && !isFluxSeparator(cells[end].text[0]))
            ++end;
        std::size_t first = i, last = end;
        while (first < last && listed(kLeadPunct, cells[first].text))
            ++first;
        while (last > first && listed(kTrailPunct, cells[last - 1].text))
            --last;
        if (first < last) {
            words.push_back({first, last, stop});
            stop = false;
        }
        i = end;
    }
    return words;
}

u16 joined(const std::vector<Cell> &cells, std::size_t first, std::size_t last)
{
    u16 out;
    for (auto i = first; i < last; ++i)
        out += cells[i].text;
    return out;
}

void replaceCells(std::vector<Cell> &cells, std::size_t first, std::size_t last, u16 text)
{
    cells[first].text = std::move(text);
    for (auto i = first + 1; i < last; ++i)
        cells[i].text.clear();
}

bool allCaps(const std::vector<Cell> &cells, const std::vector<Word> &words, const TextServices &s)
{
    return std::ranges::all_of(words, [&](const Word &w) {
        const auto t = joined(cells, w.first, w.last);
        return s.upper(t) == t;
    });
}

void titleCase(std::vector<Cell> &cells, const TextServices &s)
{
    const auto words = wordsOf(cells);
    const bool caps = allCaps(cells, words, s);
    for (std::size_t k = 0; k < words.size(); ++k) {
        const auto &w = words[k];
        const auto word = joined(cells, w.first, w.last);
        const auto lower = s.lower(word);
        if (listed(kSmallWords, lower) && k > 0 && k + 1 < words.size() && !w.afterStop) {
            replaceCells(cells, w.first, w.last, lower);
            continue;
        }
        const auto split = codePointLength(word, 0);
        const auto first = word.substr(0, split);
        u16 rest(word.substr(split));
        if (caps)
            rest = s.lower(rest);
        else if (first == s.lower(first) && rest != s.lower(rest))
            continue;
        replaceCells(cells, w.first, w.last, s.upper(first) + rest);
    }
}

bool isCased(u16v c, const TextServices &s) { return s.upper(c) != s.lower(c); }

void sentenceCase(std::vector<Cell> &cells, const TextServices &s)
{
    if (allCaps(cells, wordsOf(cells), s))
        for (auto &c : cells)
            if (c.span)
                c.text = s.lower(c.text);
    const auto blank = [&](std::size_t i) { return !cells[i].span ? cells[i].text == u" " : isWhite(cells[i].text[0]); };
    const auto endsLoneI = [&](std::size_t i) {
        return i >= cells.size() || blank(i) || u16v(u"'’?!.,").find(cells[i].text[0]) != u16v::npos;
    };
    bool capNext = true, stop = false, boundary = true;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        auto &c = cells[i];
        if (blank(i)) {
            capNext = capNext || stop;
            boundary = true;
            continue;
        }
        const char16_t ch = c.text[0];
        const bool wasBoundary = boundary;
        boundary = false;
        if (!c.span) {
            capNext = stop = false;
            continue;
        }
        if (u16v(u".?!").find(ch) != u16v::npos) {
            stop = true;
            capNext = false;
            continue;
        }
        if (capNext && u16v(u"\"'“‘«").find(ch) != u16v::npos)
            continue;
        if (isCased(c.text, s)) {
            if (capNext)
                c.text = s.upper(c.text);
            else if (c.text == u"i" && wasBoundary && endsLoneI(i + 1))
                c.text = u"I";
        }
        capNext = stop = false;
    }
}

char16_t fullWidthOf(char16_t c)
{
    switch (c) {
    case u'"':
        return u'\u201D';
    case u'\'':
        return u'\u2019';
    case u'`':
        return u'\u2018';
    default:
        return c >= 0x21 && c <= 0x7E ? static_cast<char16_t>(c + 0xFEE0) : c;
    }
}

char16_t halfWidthOf(char16_t c)
{
    if (c == u'\u3000')
        return u' ';
    if (c == u'＼' || c == u'｛' || c == u'｝')
        return c;
    return c >= 0xFF01 && c <= 0xFF5E ? static_cast<char16_t>(c - 0xFEE0) : c;
}

std::u8string replaceAllLua1(std::u8string_view t)
{
    std::u8string out;
    std::size_t i = 0;
    while (i < t.size()) {
        if (t.substr(i, 3) == u8"{*}") {
            const auto open = t.find(u8'{', i + 3);
            if (open != std::u8string_view::npos && t.substr(open, 2) == u8"{*") {
                const auto close = t.find(u8'}', open + 2);
                if (close != std::u8string_view::npos) {
                    const auto a = t.substr(i + 3, open - i - 3);
                    const auto b = t.substr(open + 2, close - open - 2);
                    out += u8"{*}";
                    out += b;
                    out += u8"{*";
                    out += a;
                    out += u8"}";
                    i = close + 1;
                    continue;
                }
            }
        }
        out += t[i++];
    }
    return out;
}

std::u8string replaceAllLua2(std::u8string_view t)
{
    std::u8string out;
    std::size_t i = 0;
    while (i < t.size()) {
        if (t.substr(i, 3) == u8"{**") {
            const auto close = t.find(u8'}', i + 3);
            if (close != std::u8string_view::npos && close > i + 3) {
                out += u8"{*}";
                out += t.substr(i + 3, close - i - 3);
                out += u8"{*}";
                i = close + 1;
                continue;
            }
        }
        out += t[i++];
    }
    return out;
}

std::u8string replaceAllPlain(std::u8string_view t, std::u8string_view from, std::u8string_view to)
{
    std::u8string out;
    for (std::size_t i = 0; i < t.size();) {
        if (t.substr(i, from.size()) == from) {
            out += to;
            i += from.size();
        } else {
            out += t[i++];
        }
    }
    return out;
}

struct BreakPoint {
    std::size_t rawStart, rawEnd;
    std::size_t display;
    char16_t before = 0;
};

std::vector<BreakPoint> breakPoints(u16v raw, const core::Projection &p, const TextServices &s)
{
    std::vector<BreakPoint> out;
    const auto &shown = p.text;
    for (const auto d : s.lineBreaks(shown)) {
        if (d == 0 || d >= shown.size())
            continue;
        const auto *prev = spanAt(p, d - 1);
        const auto *next = spanAt(p, d);
        if (!prev || prev->isProtected() || (next && next->isProtected()) || prev->kind == SpanKind::HardBreak ||
            prev->kind == SpanKind::HardSpace)
            continue;
        auto before = d - 1;
        while (before > 0 && isWhite(shown[before]))
            --before;
        BreakPoint bp{0, 0, d, shown[before]};
        if (prev->kind == SpanKind::SoftBreak) {
            bp.rawStart = prev->rawStart;
            bp.rawEnd = prev->rawEnd;
        } else if (shown[d - 1] == u' ') {
            bp.rawStart = prev->rawStart + (d - 1 - prev->displayStart);
            bp.rawEnd = bp.rawStart + 1;
        } else {
            bp.rawStart = bp.rawEnd = core::rawOffset(p, d, false);
        }
        out.push_back(bp);
    }
    return out;
}

u16 applyBreak(u16v raw, const BreakPoint &bp)
{
    return u16(raw.substr(0, bp.rawStart)) + u"\\N" + u16(raw.substr(bp.rawEnd));
}

std::size_t inkBefore(u16v shown, std::size_t end)
{
    return static_cast<std::size_t>(std::count_if(shown.begin(), shown.begin() + static_cast<std::ptrdiff_t>(end),
                                                  [](char16_t c) { return !isWhite(c); }));
}

std::size_t inkAt(const core::Projection &p, const BreakPoint &bp)
{
    auto end = bp.display;
    while (end > 0 && isWhite(p.text[end - 1]))
        --end;
    return inkBefore(p.text, end);
}

u16 removeHardBreaks(u16v raw, const core::Projection &p, const TextServices &s,
                     const std::function<bool(const core::ProjectionSpan &)> &pick)
{
    u16 out;
    bool skipSpaces = false;
    for (const auto &span : p.spans) {
        auto piece = raw.substr(span.rawStart, span.rawEnd - span.rawStart);
        if (span.kind == SpanKind::HardBreak && pick(span)) {
            const bool spaced = (!out.empty() && out.back() == u' ') ||
                                (span.displayEnd < p.text.size() && isWhite(p.text[span.displayEnd]));
            while (!out.empty() && out.back() == u' ')
                out.pop_back();
            const auto left = p.text.substr(span.displayStart > 20 ? span.displayStart - 20 : 0,
                                            std::min<std::size_t>(span.displayStart, 20));
            const auto right = p.text.substr(span.displayEnd, 20);
            bool needsSpace = !left.empty() && !right.empty();
            if (needsSpace && !spaced) {
                const auto both = u16(left) + u16(right);
                const auto ops = s.lineBreaks(both);
                needsSpace = std::ranges::find(ops, left.size()) == ops.end();
            }
            if (needsSpace)
                out += u' ';
            skipSpaces = true;
            continue;
        }
        if (skipSpaces && span.kind == SpanKind::Text) {
            while (!piece.empty() && piece.front() == u' ')
                piece.remove_prefix(1);
            if (piece.empty())
                continue;
        }
        if (span.kind != SpanKind::HiddenOverride)
            skipSpaces = false;
        out += piece;
    }
    return out;
}

std::optional<std::size_t> startBlockEnd(std::u8string_view text)
{
    if (text.empty() || text.front() != u8'{')
        return std::nullopt;
    const auto close = text.find(u8'}');
    return close == std::u8string_view::npos ? std::nullopt : std::optional(close);
}

std::vector<std::pair<std::size_t, std::size_t>> wrapTags(std::u8string_view text)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    const auto end = startBlockEnd(text);
    if (!end)
        return out;
    for (auto at = text.find(u8"\\q"); at != std::u8string_view::npos && at < *end; at = text.find(u8"\\q", at + 2)) {
        auto digits = at + 2;
        while (digits < *end && text[digits] >= u8'0' && text[digits] <= u8'9')
            ++digits;
        if (digits > at + 2)
            out.emplace_back(at, digits - at);
    }
    return out;
}

}

std::u8string changeCase(std::u8string_view text, Case kind, const TextServices &services)
{
    const auto raw = core::toUtf16(text);
    const auto p = core::project(raw);
    switch (kind) {
    case Case::Lower:
        return core::toUtf8(mapTextSpans(raw, p, services.lower));
    case Case::Upper:
        return core::toUtf8(mapTextSpans(raw, p, services.upper));
    case Case::Title:
    case Case::Sentence: {
        auto cells = cellsOf(raw, p);
        if (kind == Case::Title)
            titleCase(cells, services);
        else
            sentenceCase(cells, services);
        return core::toUtf8(rebuild(raw, p, cells));
    }
    }
    return std::u8string(text);
}

std::u8string changeWidth(std::u8string_view text, Width width)
{
    const auto raw = core::toUtf16(text);
    return core::toUtf8(mapTextSpans(raw, core::project(raw), [width](u16v piece) {
        u16 out(piece);
        for (auto &c : out)
            c = width == Width::Full ? fullWidthOf(c) : halfWidthOf(c);
        return out;
    }));
}

std::u8string swapAlternatives(std::u8string_view text)
{
    return replaceAllPlain(replaceAllLua2(replaceAllLua1(text)), u8"{*}{*", u8"{**");
}

std::u8string removeBreaks(std::u8string_view text, const TextServices &services)
{
    const auto raw = core::toUtf16(text);
    return core::toUtf8(removeHardBreaks(raw, core::project(raw), services, [](const auto &) { return true; }));
}

std::u8string moveBreak(std::u8string_view text, Direction direction, const TextServices &services)
{
    const auto raw = core::toUtf16(text);
    const auto p = core::project(raw);
    const auto first = std::ranges::find(p.spans, SpanKind::HardBreak, &core::ProjectionSpan::kind);
    std::optional<std::size_t> ink;
    u16 base = raw;
    if (first != p.spans.end()) {
        ink = inkBefore(p.text, first->displayStart);
        base = removeHardBreaks(raw, p, services, [&](const auto &s) { return &s == &*first; });
    }
    const auto q = core::project(base);
    const auto points = breakPoints(base, q, services);
    const BreakPoint *target = nullptr;
    if (direction == Direction::Right) {
        for (const auto &bp : points)
            if (!ink || inkAt(q, bp) > *ink) {
                target = &bp;
                break;
            }
    } else {
        for (const auto &bp : points)
            if (!ink || inkAt(q, bp) < *ink)
                target = &bp;
    }
    if (!target)
        return ink ? core::toUtf8(base) : std::u8string(text);
    return core::toUtf8(applyBreak(base, *target));
}

std::u8string balancedBreak(std::u8string_view text, const TextServices &services,
                            const std::function<double(std::u8string_view)> &width)
{
    const auto base = core::toUtf16(removeBreaks(text, services));
    const auto p = core::project(base);
    const auto points = breakPoints(base, p, services);
    if (points.empty())
        return std::u8string(text);
    const auto prefixWidth = [&](std::size_t end) { return width(core::toUtf8(u16v(base).substr(0, end))); };
    const double full = prefixWidth(base.size());
    struct Scored {
        const BreakPoint *point;
        double diff;
    };
    std::optional<Scored> best, bestPunct;
    for (const auto &bp : points) {
        const double left = prefixWidth(bp.rawStart);
        const double right = full - prefixWidth(bp.rawEnd);
        const Scored scored{&bp, std::abs(left - right)};
        if (!best || scored.diff < best->diff)
            best = scored;
        if (u16v(u".,?!:;。、，？！：；").find(bp.before) != u16v::npos && (!bestPunct || scored.diff < bestPunct->diff))
            bestPunct = scored;
    }
    const auto &chosen = bestPunct && bestPunct->diff - best->diff <= 0.10 * full ? *bestPunct : *best;
    return core::toUtf8(applyBreak(base, *chosen.point));
}

bool hasWrapStyle2(std::u8string_view text)
{
    return std::ranges::any_of(wrapTags(text), [&](const auto &t) { return text.substr(t.first, t.second) == u8"\\q2"; });
}

std::u8string setWrapStyle2(std::u8string_view text, bool on)
{
    std::u8string out(text);
    const auto tags = wrapTags(text);
    if (on) {
        if (hasWrapStyle2(text))
            return out;
        if (!tags.empty())
            return out.replace(tags.back().first, tags.back().second, u8"\\q2");
        return startBlockEnd(text) ? out.insert(1, u8"\\q2") : u8"{\\q2}" + out;
    }
    for (auto it = tags.rbegin(); it != tags.rend(); ++it)
        if (out.substr(it->first, it->second) == u8"\\q2")
            out.erase(it->first, it->second);
    if (out.starts_with(u8"{}"))
        out.erase(0, 2);
    return out;
}

namespace {

const char *undoName(Command command)
{
    switch (command) {
    case Command::InsertBalancedBreak:
        return "Insert balanced break";
    case Command::MoveBreakLeft:
        return "Move break left";
    case Command::MoveBreakRight:
        return "Move break right";
    case Command::RemoveBreaks:
        return "Remove breaks";
    case Command::ToggleWrapStyle2:
        return "Wrap only at \\N";
    case Command::Lowercase:
        return "lowercase";
    case Command::Uppercase:
        return "UPPERCASE";
    case Command::TitleCase:
        return "Title Case";
    case Command::SentenceCase:
        return "Sentence case";
    case Command::FullWidth:
        return "Full-width";
    case Command::HalfWidth:
        return "Half-width";
    case Command::SwapAlternatives:
        return "Swap dialogue alternatives";
    }
    return "Text";
}

std::vector<const core::LineRecord *> shownSelected(const EditSession &session, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *l : session.document().lines())
        if (session.selection().selected.contains(l->id) && (!visible || visible(l->id)))
            out.push_back(l);
    return out;
}

const std::u8string &editedText(const core::LineRecord &line)
{
    return line.translation.empty() ? line.text : line.translation;
}

}

bool selectionWrapsAtBreaksOnly(const EditSession &session, const LineVisible &visible)
{
    const auto lines = shownSelected(session, visible);
    return !lines.empty() && std::ranges::all_of(lines, [](const auto *l) { return hasWrapStyle2(editedText(*l)); });
}

std::expected<void, CommandRefusal> run(EditSession &session, Command command, const TextServices &services,
                                        TextMeasurePort &measure, const LineVisible &visible)
{
    const auto lines = shownSelected(session, visible);
    if (lines.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto styles = core::decodeStyles(session.document());
    const bool wrapOn = command == Command::ToggleWrapStyle2 && !selectionWrapsAtBreaksOnly(session, visible);
    struct Change {
        core::LineId id;
        bool translation;
        std::u8string text;
        bool toggleComment;
    };
    std::vector<Change> changes;
    for (const auto *line : lines) {
        const auto &text = editedText(*line);
        std::u8string next;
        switch (command) {
        case Command::InsertBalancedBreak: {
            const core::StyleValues *style = styles.empty() ? nullptr : &styles.front();
            for (const auto &s : styles)
                if (s.name == line->style) {
                    style = &s;
                    break;
                }
            next = style ? balancedBreak(text, services,
                                         [&](std::u8string_view prefix) {
                                             return taggedTextWidth(measure, *style, core::toUtf16(prefix));
                                         })
                         : std::u8string(text);
            break;
        }
        case Command::MoveBreakLeft:
            next = moveBreak(text, Direction::Left, services);
            break;
        case Command::MoveBreakRight:
            next = moveBreak(text, Direction::Right, services);
            break;
        case Command::RemoveBreaks:
            next = removeBreaks(text, services);
            break;
        case Command::ToggleWrapStyle2:
            next = setWrapStyle2(text, wrapOn);
            break;
        case Command::Lowercase:
            next = changeCase(text, Case::Lower, services);
            break;
        case Command::Uppercase:
            next = changeCase(text, Case::Upper, services);
            break;
        case Command::TitleCase:
            next = changeCase(text, Case::Title, services);
            break;
        case Command::SentenceCase:
            next = changeCase(text, Case::Sentence, services);
            break;
        case Command::FullWidth:
            next = changeWidth(text, Width::Full);
            break;
        case Command::HalfWidth:
            next = changeWidth(text, Width::Half);
            break;
        case Command::SwapAlternatives:
            next = swapAlternatives(text);
            break;
        }
        const bool toggle = command == Command::SwapAlternatives && line->effect == u8"***";
        if (next != text || toggle)
            changes.push_back({line->id, !line->translation.empty(), std::move(next), toggle});
    }
    if (changes.empty())
        return {};
    std::set<core::LineId> touched;
    for (const auto &c : changes)
        touched.insert(c.id);
    const auto ran = session.run(application::Command{undoName(command), session.revision(), touched, [&](core::Document &d) {
        for (const auto &c : changes)
            if (!d.editLine(c.id, [&](core::LineRecord &r) {
                    (c.translation ? r.translation : r.text) = c.text;
                    if (c.toggleComment)
                        r.comment = !r.comment;
                }))
                return false;
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

}
