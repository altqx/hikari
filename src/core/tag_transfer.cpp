#include "hikari/core/tag_transfer.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <set>

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
    u"fscy",  u"move",  u"shad",  u"fad",   u"fax",   u"fay",   u"frx",  u"fry",  u"frz",  u"fsp",  u"org",
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
        tag.name = tagName(content.substr(i + 1));
        const std::size_t argsStart = i + 1 + tag.name.size();
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
        if (raw[i] == u'}') {
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

enum class Kind { None, Flag, Bold, Colour, Alpha, Number, Text, Transform, Reset };

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
    for (const auto number : {u"fs", u"fscx", u"fscy", u"fsp", u"frx", u"fry", u"fax", u"fay", u"be", u"blur", u"fe",
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
        if (std::from_chars(first, last, n).ec != std::errc{})
            return value;
        if (kind == Kind::Flag || n == 0 || n == 1)
            return n != 0 ? u"1" : u"0";
        return ascii(std::to_string(n));
    }
    case Kind::Colour:
    case Kind::Alpha: {
        while (first != last && (*first == '&' || *first == 'H' || *first == 'h'))
            ++first;
        unsigned long n = 0;
        if (std::from_chars(first, last, n, 16).ec != std::errc{})
            return value;
        return kind == Kind::Colour ? hex(n & 0xFFFFFF, 6) : hex(n & 0xFF, 2);
    }
    case Kind::Number: {
        double n = 0;
        if (std::from_chars(first, last, n).ec != std::errc{})
            return value;
        char buf[32];
        const auto r = std::to_chars(buf, buf + sizeof buf, n);
        return ascii(std::string(buf, r.ptr));
    }
    default:
        return value;
    }
}

std::u16string valueIn(const TagState &state, const std::u16string &component)
{
    const auto it = state.find(component);
    return it == state.end() ? std::u16string() : it->second;
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
    return d;
}

// One component's value after a tag.
struct Assignment {
    std::size_t order = 0;
    std::u16string component, value, tag;
};

// Applies `tag`, appending what it set to `history` when given.
void apply(TagState &state, const Tag &tag, const TagState &defaults, std::vector<Assignment> *history)
{
    const auto effect = effectOf(tag.name);
    std::vector<std::u16string> touched = effect.components;
    switch (effect.kind) {
    case Kind::None:
        return;
    case Kind::Reset: {
        for (const auto &[component, value] : state)
            touched.push_back(component);
        state = defaults;
        if (const auto style = trimmed(tag.args); !style.empty())
            state[u"r"] = style;
        for (const auto &[component, value] : state)
            touched.push_back(component);
        break;
    }
    case Kind::Transform:
        state[u"t"] += tag.text;
        break;
    default:
        for (const auto &component : effect.components) {
            if (trimmed(tag.args).empty()) {
                if (const auto it = defaults.find(component); it != defaults.end())
                    state[component] = it->second;
                else
                    state.erase(component);
            } else {
                state[component] = normalised(effect.kind, tag.args);
            }
        }
    }
    if (history)
        for (const auto &component : touched)
            history->push_back({tag.order, component, valueIn(state, component), tag.text});
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
        } else if (open && segments[groups.back().last].differs == segments[i].differs) {
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
// base), in the order the Original text wrote them.
std::u16string transition(const OriginalTags &tags, int from, int to)
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
    std::vector<TagSetter> parts;
    for (const auto &component : components) {
        if (valueOf(f, component) == valueOf(t, component))
            continue;
        const auto &setters = t && t->differs.contains(component) ? t->opens : f->closes;
        if (const auto it = setters.find(component); it != setters.end() && !it->second.tag.empty())
            parts.push_back(it->second);
    }
    std::ranges::stable_sort(parts, {}, &TagSetter::order);
    std::u16string out;
    std::set<std::u16string> written;
    for (const auto &part : parts)
        if (written.insert(part.tag).second)
            out += part.tag;
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

TagSetter lastSetter(const std::vector<Assignment> &history, const std::u16string &component, std::size_t before,
                     const std::u16string &value)
{
    TagSetter found;
    for (const auto &a : history)
        if (a.order < before && a.component == component && a.value == value)
            found = {a.order, a.tag};
    return found;
}

} // namespace

OriginalTags readOriginalTags(std::u16string_view original, const StyleValues *style)
{
    OriginalTags out;
    out.defaults = defaultsFor(style);
    const auto tokens = tokenize(original);
    if (tokens.malformed) {
        out.malformed = true;
        return out;
    }
    for (const auto &piece : tokens.pieces) {
        for (const auto &tag : piece.tags) {
            if (tag.name == u"p" && normalised(Kind::Number, tag.args) != u"0" && !trimmed(tag.args).empty()) {
                out.drawing = true;
                return out;
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
    std::vector<Assignment> history;
    std::map<std::u16string, TagSetter> leadingSetters;
    std::u16string leading, hoisted, trailing;
    std::vector<Segment> segments;
    std::size_t tagsSeen = 0;
    for (std::size_t i = 0; i < tokens.pieces.size(); ++i) {
        const auto &piece = tokens.pieces[i];
        const bool isLeading = i < first, isTrailing = first < tokens.pieces.size() && i > last;
        if (!isLeading && !baseSet) {
            out.base = state;
            baseSet = true;
        }
        if (!piece.block) {
            if (!isLeading && !isTrailing && !out.karaoke)
                segments.push_back({piece.rawStart, piece.rawEnd, tagsSeen, state, hasVisible(original, piece)});
            continue;
        }
        tagsSeen += piece.tags.size();
        for (const auto &tag : piece.tags) {
            if (isKaraoke(tag.name))
                continue;
            if (isLineWide(tag.name)) {
                (isLeading ? leading : hoisted) += tag.text;
                continue;
            }
            const auto effect = effectOf(tag.name);
            if (isLeading) {
                leading += tag.text;
                for (const auto &component : effect.components)
                    leadingSetters[component] = {tag.order, tag.text};
            } else if (isTrailing) {
                if (std::ranges::any_of(effect.components, [&](const auto &c) { return leadingSetters.contains(c); }))
                    trailing += tag.text;
            }
            apply(state, tag, out.defaults, &history);
        }
    }
    if (!baseSet)
        out.base = state;
    out.leading = braced(leading + hoisted);
    out.trailing = braced(trailing);

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
        run.number = static_cast<int>(out.runs.size()) + 1;
        run.differs = firstSegment.differs;
        auto start = displayOffset(projection, firstSegment.rawStart);
        auto end = displayOffset(projection, lastSegment.rawEnd);
        while (start < end && isDisplaySpace(projection.text[start]))
            ++start;
        while (end > start && isDisplaySpace(projection.text[end - 1]))
            --end;
        run.displayStart = start;
        run.displayEnd = end;
        run.text = projection.text.substr(start, end - start);
        for (const auto &[component, value] : run.differs) {
            run.opens[component] = lastSetter(history, component, firstSegment.order, value);
            const auto baseValue = valueIn(out.base, component);
            TagSetter close;
            for (const auto &a : history) {
                if (a.order >= lastSegment.order && a.component == component && a.value == baseValue) {
                    close = {a.order, a.tag};
                    break;
                }
            }
            if (close.tag.empty()) {
                if (const auto it = leadingSetters.find(component); it != leadingSetters.end())
                    close = it->second;
                else if (component != u"t" && component != u"r" && baseValue == valueIn(out.defaults, component))
                    close = {lastSegment.order, u"\\" + component};
            }
            run.closes[component] = close;
        }
        out.runs.push_back(std::move(run));
    }
    return out;
}

std::u16string composeTranslation(const OriginalTags &tags, const TranslationDraft &draft)
{
    if (draft.body.empty())
        return {};
    std::u16string out = draft.carried ? tags.leading : std::u16string();
    int current = 0;
    std::size_t pos = 0, currentEnd = 0;
    auto moveTo = [&](std::size_t at, int run) {
        out.append(draft.body, pos, at - pos);
        pos = at;
        out += braced(transition(tags, current, run));
        current = run;
    };
    for (const auto &p : validPlacements(tags, draft)) {
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

TranslationDraft readTranslation(const OriginalTags &tags, std::u16string_view translation)
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
    if (tags.runs.empty())
        return plain;
    const auto tokens = tokenize(inner);
    if (tokens.malformed)
        return plain;

    // The translation's own formatting, matched to the runs.
    std::vector<Segment> segments;
    TagState state = tags.base;
    for (const auto &piece : tokens.pieces) {
        if (piece.block)
            for (const auto &tag : piece.tags)
                apply(state, tag, tags.defaults, nullptr);
        else
            segments.push_back({piece.rawStart, piece.rawEnd, 0, differences(state, tags.base), true});
    }
    std::vector<int> labels(segments.size(), 0); // a run, 0 for the base, -1 for the translator's own
    std::vector<bool> used(tags.runs.size(), false);
    const auto groups = groupsOf(segments);
    for (const auto &group : groups) {
        int label = -1;
        for (std::size_t r = 0; r < tags.runs.size(); ++r) {
            if (!used[r] && tags.runs[r].differs == segments[group.first].differs) {
                used[r] = true;
                label = tags.runs[r].number;
                break;
            }
        }
        std::fill(labels.begin() + group.first, labels.begin() + group.last + 1, label);
    }

    // Drop each run's tags where the composer would write them, noting where.
    TranslationDraft draft;
    std::vector<std::size_t> boundary; // before each segment, and at the end
    std::vector<const Piece *> pending;
    auto settle = [&](int before, int after) {
        before = std::max(before, 0);
        after = std::max(after, 0);
        const auto expected = before != after ? transition(tags, before, after) : std::u16string();
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
        if (!settle(previous, labels[s]))
            return plain;
        draft.body += inner.substr(piece.rawStart, piece.rawEnd - piece.rawStart);
        previous = labels[s++];
    }
    if (!settle(previous, 0))
        return plain;
    for (const auto &group : groups)
        if (labels[group.first] > 0)
            draft.placements.push_back({boundary[group.first], boundary[group.last + 1], labels[group.first]});
    if (composeTranslation(tags, draft) != translation)
        return plain;
    return draft;
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
    const auto projection = project(draft.body);
    const std::u16string_view text = projection.text;
    auto isWord = [&](std::size_t at, std::size_t length) {
        const auto &needle = text.substr(at, length);
        if (at > 0 && isAsciiAlnum(text[at - 1]) && isAsciiAlnum(needle.front()))
            return false;
        const auto after = at + length;
        return !(after < text.size() && isAsciiAlnum(text[after]) && isAsciiAlnum(needle.back()));
    };
    auto overlaps = [](std::span<const RunPlacement> list, const RunPlacement &p) {
        return std::ranges::any_of(list, [&](const RunPlacement &q) { return p.start < q.end && q.start < p.end; });
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
        if (p.start < p.end && !overlaps(draft.placements, p) && !overlaps(out, p))
            out.push_back(p);
    }
    std::ranges::sort(out, {}, &RunPlacement::start);
    return out;
}

} // namespace hikari::core
