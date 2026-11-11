#include "hikari/application/interpolate.h"

#include "hikari/application/resample.h"
#include "hikari/application/tag_lerp.h"
#include "hikari/application/visual_script.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <variant>

namespace hikari::application::interpolate {
namespace {

struct Item {
    std::string name; // empty: text inside the block that is not a tag
    std::string args;
    std::string raw;
    std::string text() const { return name.empty() ? raw : "\\" + name + args; }
};

struct Piece {
    bool block = false;
    std::string text;
    std::vector<Item> items;
    std::size_t at = 0;
    bool emptied = false;
};

constexpr std::array<std::string_view, 51> kNames{
    "iclip", "alpha", "xbord", "ybord", "xshad", "yshad", "clip", "bord", "shad", "blur", "fscx", "fscy", "fade",
    "move",  "fsp",   "frx",   "fry",   "frz",   "fax",   "fay",  "fad",  "pos",  "org",  "pbo",  "fs",   "fr",
    "fn",    "fe",    "an",    "be",    "kf",    "ko",    "1c",   "2c",   "3c",   "4c",   "1a",   "2a",   "3a",
    "4a",    "b",     "i",     "u",     "s",     "a",     "q",    "p",    "r",    "t",    "k",    "c"};

constexpr lerp::TagSpec kIclip{"iclip", lerp::Kind::Rect};

std::vector<Item> parseItems(std::string_view content)
{
    std::vector<Item> items;
    std::size_t i = content.find('\\');
    if (i != 0) {
        items.push_back(Item{{}, {}, std::string(content.substr(0, i))});
        if (i == std::string_view::npos)
            return items;
    }
    while (i < content.size()) {
        const std::string_view rest = content.substr(i + 1);
        std::string name;
        for (const auto known : kNames)
            if (rest.starts_with(known)) {
                name = known;
                break;
            }
        if (name.empty()) {
            std::size_t n = 0;
            while (n < rest.size() && ((rest[n] >= 'a' && rest[n] <= 'z') || (rest[n] >= 'A' && rest[n] <= 'Z')))
                ++n;
            name = rest.substr(0, n);
        }
        std::size_t end = i + 1 + name.size();
        int depth = 0;
        while (end < content.size()) {
            const char c = content[end];
            if (c == '(')
                ++depth;
            else if (c == ')' && depth > 0)
                --depth;
            else if (c == '\\' && depth == 0)
                break;
            ++end;
        }
        const auto args = content.substr(i + 1 + name.size(), end - i - 1 - name.size());
        if (name.empty())
            items.push_back(Item{{}, {}, "\\" + std::string(args)});
        else
            items.push_back(Item{name, std::string(args), {}});
        i = end;
    }
    return items;
}

std::size_t characters(std::string_view s)
{
    return static_cast<std::size_t>(std::ranges::count_if(s, [](char c) { return (c & 0xC0) != 0x80; }));
}

struct Parsed {
    std::vector<Piece> pieces;
    std::string plain;
};

Parsed parse(std::string_view text)
{
    Parsed out;
    std::size_t i = 0, at = 0;
    const auto addText = [&](std::string_view t) {
        out.pieces.push_back(Piece{false, std::string(t), {}, at});
        out.plain += t;
        at += characters(t);
    };
    while (i < text.size()) {
        const auto open = text.find('{', i);
        const auto close = open == std::string_view::npos ? open : text.find('}', open);
        if (close == std::string_view::npos) {
            addText(text.substr(i));
            break;
        }
        if (open > i)
            addText(text.substr(i, open - i));
        out.pieces.push_back(Piece{true, {}, parseItems(text.substr(open + 1, close - open - 1)), at});
        i = close + 1;
    }
    return out;
}

std::string serialize(const std::vector<Piece> &pieces)
{
    std::string out;
    for (const auto &p : pieces) {
        if (!p.block) {
            out += p.text;
            continue;
        }
        if (p.emptied && p.items.empty())
            continue;
        out += '{';
        for (const auto &item : p.items)
            out += item.text();
        out += '}';
    }
    return out;
}

const lerp::TagSpec *specOf(std::string_view name)
{
    return name == "iclip" ? &kIclip : lerp::tagSpec(name);
}

bool lineWide(std::string_view key)
{
    return key == "pos" || key == "org" || key == "clip" || key == "iclip";
}

std::string numberText(double v, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string out = buf;
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0')
            out.pop_back();
        if (out.back() == '.')
            out.pop_back();
    }
    return out == "-0" ? "0" : out;
}

std::size_t byteOf(std::string_view s, std::size_t characters)
{
    for (std::size_t i = 0, n = 0; i < s.size(); ++i)
        if ((s[i] & 0xC0) != 0x80 && n++ == characters)
            return i;
    return s.size();
}

std::string textFrom(std::string_view s, std::size_t at, std::size_t count)
{
    const auto begin = byteOf(s, at);
    return std::string(s.substr(begin, byteOf(s, at + count) - begin));
}

std::string spelled(std::string formatted, std::string_view key, std::string_view name)
{
    return "\\" + std::string(name) + formatted.substr(1 + key.size());
}

struct Transform {
    std::vector<double> numbers; // t1, t2 and accel as written: 0 to 3 of them
    std::vector<Item> inner;
};

std::optional<Transform> parseTransform(std::string_view args)
{
    if (args.size() < 2 || args.front() != '(' || args.back() != ')')
        return std::nullopt;
    args = args.substr(1, args.size() - 2);
    Transform out;
    while (out.numbers.size() < 3) {
        const auto comma = args.find(',');
        const auto slash = args.find('\\');
        if (comma == std::string_view::npos || (slash != std::string_view::npos && slash < comma))
            break;
        const std::string head(args.substr(0, comma));
        char *end = nullptr;
        const double v = std::strtod(head.c_str(), &end);
        if (end == head.c_str())
            return std::nullopt;
        out.numbers.push_back(v);
        args.remove_prefix(comma + 1);
    }
    out.inner = parseItems(args);
    return out;
}

std::string transformText(const Transform &t)
{
    std::string out = "\\t(";
    for (std::size_t i = 0; i < t.numbers.size(); ++i)
        out += numberText(t.numbers[i], i < 2 && t.numbers.size() >= 2 ? 0 : 2) + ",";
    for (const auto &item : t.inner)
        out += item.text();
    return out + ")";
}

bool sameShape(const Transform &a, const Transform &b)
{
    if (a.numbers.size() != b.numbers.size() || a.inner.size() != b.inner.size())
        return false;
    for (std::size_t i = 0; i < a.inner.size(); ++i)
        if (a.inner[i].name != b.inner[i].name)
            return false;
    return true;
}

using ClipValue = std::variant<lerp::Value, lerp::Drawing>;

std::optional<ClipValue> clipValue(const lerp::TagSpec &spec, std::string_view args)
{
    if (const auto rect = lerp::parse(spec, args))
        return *rect;
    if (const auto drawing = lerp::parseDrawing(args))
        return *drawing;
    return std::nullopt;
}

std::string clipText(const lerp::TagSpec &spec, const ClipValue &v)
{
    if (const auto *rect = std::get_if<lerp::Value>(&v))
        return lerp::format(spec, *rect);
    return lerp::format(spec.key, std::get<lerp::Drawing>(v));
}

double rotationFrom(double from, double to)
{
    double a = std::fmod(from, 360), b = std::fmod(to, 360);
    if (a < 0)
        a += 360;
    if (b < 0)
        b += 360;
    const double delta = b - a;
    return std::abs(delta) > 180 ? a + (delta > 0 ? 360 : -360) : a;
}

double rotationTo(double from, double to)
{
    double b = std::fmod(to, 360);
    return b < 0 ? b + 360 : b;
}

bool isRotation(std::string_view key)
{
    return key == "frx" || key == "fry" || key == "frz";
}

struct End {
    Parsed parsed;
    const LineIn *line = nullptr;
    std::optional<lerp::Value> pos, org;
    std::optional<ClipValue> clip, iclip;
    bool ownOrg = false;
};

End readEnd(const LineIn &line)
{
    End e{parse(line.text), &line, {}, {}, {}, {}, false};
    std::optional<lerp::Value> move;
    for (const auto &p : e.parsed.pieces)
        for (const auto &item : p.items) {
            if (item.name == "pos" && !e.pos)
                e.pos = lerp::parse(*lerp::tagSpec("pos"), item.args);
            else if (item.name == "org" && !e.org)
                e.org = lerp::parse(*lerp::tagSpec("org"), item.args);
            else if (item.name == "move" && !move) {
                const auto open = item.args.find('(');
                const auto second = item.args.find(',', item.args.find(',', open) + 1);
                if (open != std::string::npos && second != std::string::npos)
                    move = lerp::parse(*lerp::tagSpec("pos"), item.args.substr(open, second - open) + ")");
            } else if (item.name == "clip" && !e.clip)
                e.clip = clipValue(*lerp::tagSpec("clip"), item.args);
            else if (item.name == "iclip" && !e.iclip)
                e.iclip = clipValue(kIclip, item.args);
        }
    if (!e.pos)
        e.pos = move ? *move : lerp::Value{{line.defaultX, line.defaultY}};
    e.ownOrg = e.org.has_value();
    if (!e.org)
        e.org = e.pos;
    return e;
}

std::vector<const Item *> itemsAt(const Parsed &parsed, std::size_t at)
{
    std::vector<const Item *> out;
    for (const auto &p : parsed.pieces)
        if (p.block && p.at == at)
            for (const auto &item : p.items)
                out.push_back(&item);
    return out;
}

struct SlotValues {
    std::map<std::string, lerp::Value> values;
    std::map<std::string, std::string> names;
    std::vector<std::string> order;
    bool reset = false;
    std::vector<Transform> transforms;
    std::string transformsText;
};

SlotValues slotValues(const Parsed &parsed, std::size_t at)
{
    SlotValues out;
    for (const auto *item : itemsAt(parsed, at)) {
        if (item->name == "r") {
            out.values.clear();
            out.reset = true;
            continue;
        }
        if (item->name == "t") {
            if (const auto t = parseTransform(item->args))
                out.transforms.push_back(*t);
            out.transformsText += item->text();
            continue;
        }
        const auto *spec = specOf(item->name);
        if (!spec || lineWide(spec->key))
            continue;
        if (const auto v = lerp::parse(*spec, item->args)) {
            const std::string key(spec->key);
            if (!out.values.contains(key))
                out.order.push_back(key);
            out.values[key] = *v;
            out.names[key] = item->name;
        }
    }
    return out;
}

struct Slot {
    std::size_t at = 0;
    std::vector<Change> changes;
    std::map<std::string, std::pair<lerp::Value, lerp::Value>> values;
    std::map<std::string, std::string> names;
    std::vector<Transform> fromTransforms, toTransforms;
};

std::string changeId(std::string_view key, std::size_t at)
{
    return std::string(key) + "@" + std::to_string(at);
}

std::vector<Slot> slotChanges(const End &first, const End &last, const std::vector<std::size_t> &offsets)
{
    std::vector<Slot> slots;
    std::map<std::string, lerp::Value> currentFrom, currentTo;
    for (const auto at : offsets) {
        const auto a = slotValues(first.parsed, at), b = slotValues(last.parsed, at);
        if (a.reset)
            currentFrom.clear();
        if (b.reset)
            currentTo.clear();
        Slot slot{at, {}, {}, {}, {}, {}};
        std::vector<std::string> keys = a.order;
        for (const auto &k : b.order)
            if (std::ranges::find(keys, k) == keys.end())
                keys.push_back(k);
        for (const auto &key : keys) {
            const auto *spec = lerp::tagSpec(key);
            const auto valueOf = [&](const SlotValues &s, std::map<std::string, lerp::Value> &current,
                                     const LineIn &line) {
                if (const auto it = s.values.find(key); it != s.values.end())
                    current[key] = it->second;
                else if (!current.contains(key))
                    current[key] = lerp::styleDefault(*spec, line.style).value_or(lerp::Value{});
                return current[key];
            };
            const auto from = valueOf(a, currentFrom, *first.line), to = valueOf(b, currentTo, *last.line);
            const std::string name = a.names.contains(key) ? a.names.at(key) : b.names.at(key);
            const auto fromText = spelled(lerp::format(*spec, from), key, name);
            const auto toText = spelled(lerp::format(*spec, to), key, name);
            if (fromText == toText)
                continue;
            slot.changes.push_back(Change{changeId(key, at), key, at, fromText, toText, Skip::None, name, {}});
            slot.values[key] = {from, to};
            slot.names[key] = name;
        }
        if (a.transformsText != b.transformsText) {
            bool same = a.transforms.size() == b.transforms.size();
            for (std::size_t i = 0; same && i < a.transforms.size(); ++i)
                same = sameShape(a.transforms[i], b.transforms[i]);
            slot.changes.push_back(Change{changeId("t", at), "t", at, a.transformsText, b.transformsText,
                                          same ? Skip::None : Skip::DifferentTransforms, "t", {}});
            slot.fromTransforms = a.transforms;
            slot.toTransforms = b.transforms;
        }
        slots.push_back(std::move(slot));
    }
    return slots;
}

std::optional<Change> clipChange(const lerp::TagSpec &spec, const std::optional<ClipValue> &from,
                                 const std::optional<ClipValue> &to)
{
    if (!from && !to)
        return std::nullopt;
    const std::string key(spec.key);
    Change c{key, key, 0, from ? clipText(spec, *from) : "", to ? clipText(spec, *to) : "", Skip::None, key, {}};
    if (c.from == c.to)
        return std::nullopt;
    if (!from || !to)
        c.skip = Skip::OneSidedClip;
    else if (from->index() != to->index())
        c.skip = Skip::RectAndDrawing;
    else if (const auto *d = std::get_if<lerp::Drawing>(&*from); d && !lerp::lerp(*d, std::get<lerp::Drawing>(*to), 0))
        c.skip = Skip::DifferentShapes;
    return c;
}

lerp::Value lerped(std::string_view key, const lerp::Value &from, const lerp::Value &to, double t,
                   const Options &options)
{
    const auto *spec = lerp::tagSpec(key);
    if (options.shortestRotation && isRotation(key)) {
        const lerp::Value a{{rotationFrom(from.v[0], to.v[0])}}, b{{rotationTo(from.v[0], to.v[0])}};
        auto out = lerp::lerp(*spec, a, b, t);
        if (out.v[0] < 0)
            out.v[0] += 360;
        return out;
    }
    return lerp::lerp(*spec, from, to, t);
}

Transform lerpedTransform(const Transform &from, const Transform &to, double t)
{
    Transform out = from;
    for (std::size_t i = 0; i < out.numbers.size(); ++i) {
        out.numbers[i] = from.numbers[i] + (to.numbers[i] - from.numbers[i]) * t;
        if (i < 2 && out.numbers.size() >= 2)
            out.numbers[i] = std::round(out.numbers[i]);
    }
    for (std::size_t i = 0; i < out.inner.size(); ++i) {
        const auto &a = from.inner[i], &b = to.inner[i];
        const auto *spec = specOf(a.name);
        if (!spec)
            continue;
        const auto va = lerp::parse(*spec, a.args), vb = lerp::parse(*spec, b.args);
        if (va && vb)
            out.inner[i].args = spelled(lerp::format(*spec, lerp::lerp(*spec, *va, *vb, t)), spec->key, a.name)
                                    .substr(1 + a.name.size());
    }
    return out;
}

bool matches(const Item &item, std::string_view key)
{
    if (item.name.empty())
        return false;
    if (key == "pos")
        return item.name == "pos" || item.name == "move";
    const auto *spec = specOf(item.name);
    return spec && spec->key == key;
}

void removeKey(Piece &block, std::string_view key)
{
    std::erase_if(block.items, [&](const Item &item) { return matches(item, key); });
    for (auto &item : block.items) {
        if (item.name != "t")
            continue;
        auto t = parseTransform(item.args);
        if (!t || std::ranges::none_of(t->inner, [&](const Item &i) { return matches(i, key); }))
            continue;
        std::erase_if(t->inner, [&](const Item &i) { return matches(i, key); });
        item.args = t->inner.empty() ? std::string() : transformText(*t).substr(2);
    }
    std::erase_if(block.items, [](const Item &item) { return item.name == "t" && item.args.empty(); });
    block.emptied = true;
}

// The last block at `at`, made when the Line has none there; nullptr past the Line's text.
Piece *blockAt(std::vector<Piece> &pieces, std::size_t at)
{
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it)
        if (it->block && it->at == at)
            return &*it;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        auto &p = pieces[i];
        if (p.block)
            continue;
        const auto length = characters(p.text);
        if (at < p.at || at > p.at + length || (at == p.at + length && i + 1 < pieces.size()))
            continue;
        std::size_t byte = 0;
        for (std::size_t n = at - p.at; n > 0; --n)
            do
                ++byte;
            while (byte < p.text.size() && (p.text[byte] & 0xC0) == 0x80);
        Piece after{false, p.text.substr(byte), {}, at};
        p.text.resize(byte);
        std::vector<Piece> inserted{Piece{true, {}, {}, at}};
        if (!after.text.empty())
            inserted.push_back(std::move(after));
        pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(i) + 1, inserted.begin(), inserted.end());
        std::erase_if(pieces, [](const Piece &x) { return !x.block && x.text.empty(); });
        return blockAt(pieces, at);
    }
    if (pieces.empty() && at == 0) {
        pieces.push_back(Piece{true, {}, {}, 0});
        return &pieces.back();
    }
    return nullptr;
}

std::vector<Piece *> blocksAt(std::vector<Piece> &pieces, std::size_t at)
{
    std::vector<Piece *> out;
    for (auto &p : pieces)
        if (p.block && p.at == at)
            out.push_back(&p);
    return out;
}

Piece *firstBlock(std::vector<Piece> &pieces)
{
    if (pieces.empty() || !pieces.front().block)
        pieces.insert(pieces.begin(), Piece{true, {}, {}, 0});
    return &pieces.front();
}

bool included(const Change &c, const Options &options)
{
    return c.skip == Skip::None && std::ranges::find(options.excluded, c.id) == options.excluded.end();
}

struct Ends {
    const End &first, &last;
    std::vector<Change> wide;
    std::vector<Slot> slots;
};

std::string middleText(const Ends &run, const LineIn &line, double t, const Options &options)
{
    auto pieces = parse(line.text).pieces;
    for (const auto &c : run.wide) {
        if (!included(c, options))
            continue;
        for (auto &p : pieces)
            if (p.block)
                removeKey(p, c.key);
        std::string text;
        if (c.key == "pos" || c.key == "org") {
            const auto &spec = *lerp::tagSpec(c.key);
            const auto &from = c.key == "pos" ? *run.first.pos : *run.first.org;
            const auto &to = c.key == "pos" ? *run.last.pos : *run.last.org;
            text = lerp::format(spec, lerp::lerp(spec, from, to, t));
        } else {
            const auto &spec = c.key == "clip" ? *lerp::tagSpec("clip") : kIclip;
            const auto &from = c.key == "clip" ? *run.first.clip : *run.first.iclip;
            const auto &to = c.key == "clip" ? *run.last.clip : *run.last.iclip;
            if (const auto *rect = std::get_if<lerp::Value>(&from))
                text = lerp::format(spec, lerp::lerp(spec, *rect, std::get<lerp::Value>(to), t));
            else
                text = lerp::format(spec.key, *lerp::lerp(std::get<lerp::Drawing>(from), std::get<lerp::Drawing>(to), t));
        }
        auto items = parseItems(text);
        auto *block = firstBlock(pieces);
        block->items.insert(block->items.begin(), items.begin(), items.end());
    }
    std::map<std::string, std::string> written;
    for (const auto &slot : run.slots) {
        for (const auto &c : slot.changes) {
            if (!included(c, options))
                continue;
            for (auto *b : blocksAt(pieces, slot.at)) {
                if (c.key == "t") {
                    std::erase_if(b->items, [](const Item &i) { return i.name == "t"; });
                    b->emptied = true;
                } else {
                    removeKey(*b, c.key);
                }
            }
            std::string text;
            if (c.key == "t") {
                for (std::size_t i = 0; i < slot.fromTransforms.size(); ++i)
                    text += transformText(lerpedTransform(slot.fromTransforms[i], slot.toTransforms[i], t));
            } else {
                const auto &[from, to] = slot.values.at(c.key);
                text = spelled(lerp::format(*lerp::tagSpec(c.key), lerped(c.key, from, to, t, options)), c.key,
                               slot.names.at(c.key));
                if (written[c.key] == text)
                    continue;
                written[c.key] = text;
            }
            if (auto *block = blockAt(pieces, slot.at)) {
                const auto items = parseItems(text);
                block->items.insert(block->items.end(), items.begin(), items.end());
            }
        }
    }
    return serialize(pieces);
}

}

std::string cascadeTransforms(std::string_view text, double durationMs)
{
    auto pieces = parse(text).pieces;
    std::vector<Item *> transforms;
    for (auto &p : pieces)
        for (auto &item : p.items)
            if (item.name == "t" && parseTransform(item.args))
                transforms.push_back(&item);
    if (transforms.empty())
        return std::string(text);
    const double interval = durationMs / static_cast<double>(transforms.size());
    for (std::size_t k = 0; k < transforms.size(); ++k) {
        auto t = *parseTransform(transforms[k]->args);
        const std::optional<double> accel =
            t.numbers.size() == 1 || t.numbers.size() == 3 ? std::optional(t.numbers.back()) : std::nullopt;
        t.numbers = {std::round(interval * static_cast<double>(k)), std::round(interval * static_cast<double>(k + 1))};
        if (accel)
            t.numbers.push_back(*accel);
        transforms[k]->args = transformText(t).substr(2);
    }
    return serialize(pieces);
}

std::expected<Plan, Problem> plan(std::span<const LineIn> lines, const Options &options)
{
    if (lines.size() < 3)
        return std::unexpected(Problem::TooFewLines);
    const End first = readEnd(lines.front()), last = readEnd(lines.back());
    Plan out;
    out.startBlockOnly = first.parsed.plain != last.parsed.plain;
    std::set<std::size_t> offsets{0};
    if (!out.startBlockOnly)
        for (const auto *e : {&first, &last})
            for (const auto &p : e->parsed.pieces)
                if (p.block)
                    offsets.insert(p.at);

    Ends run{first, last, {}, {}};
    for (const std::string_view key : {"pos", "org"}) {
        if (key == "org" && !first.ownOrg && !last.ownOrg)
            continue;
        const auto &spec = *lerp::tagSpec(key);
        const auto from = lerp::format(spec, key == "pos" ? *first.pos : *first.org);
        const auto to = lerp::format(spec, key == "pos" ? *last.pos : *last.org);
        if (from != to)
            run.wide.push_back(Change{std::string(key), std::string(key), 0, from, to, Skip::None, std::string(key), {}});
    }
    if (auto c = clipChange(*lerp::tagSpec("clip"), first.clip, last.clip))
        run.wide.push_back(*c);
    if (auto c = clipChange(kIclip, first.iclip, last.iclip))
        run.wide.push_back(*c);
    run.slots = slotChanges(first, last, {offsets.begin(), offsets.end()});

    out.changes = run.wide;
    for (const auto &slot : run.slots)
        for (auto c : slot.changes) {
            if (c.at > 0)
                c.before = textFrom(first.parsed.plain, c.at, 8);
            out.changes.push_back(std::move(c));
        }
    if (out.changes.empty() && !options.cascade)
        return std::unexpected(Problem::NothingChanges);

    const double span = static_cast<double>(lines.size() - 1);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string text = i == 0 || i + 1 == lines.size()
                               ? lines[i].text
                               : middleText(run, lines[i], lerp::accelerate(static_cast<double>(i) / span, options.accel),
                                            options);
        if (options.cascade)
            text = cascadeTransforms(text, lines[i].durationMs);
        out.texts.push_back(std::move(text));
    }
    return out;
}

Run selectedRun(const EditSession &session, const LineVisible &visible)
{
    const auto &doc = session.document();
    const auto styles = core::decodeStyles(doc);
    const auto resolution = scriptResolution(doc);
    Run out;
    for (const auto *l : doc.lines()) {
        if (!session.selection().selected.contains(l->id) || l->visibility == core::LineVisibility::Hidden ||
            (visible && !visible(l->id)))
            continue;
        core::StyleValues style;
        const auto found = std::ranges::find(styles, l->style, &core::StyleValues::name);
        if (found != styles.end())
            style = *found;
        else if (!styles.empty())
            style = styles.front();
        const std::string text(l->text.begin(), l->text.end());
        int an = std::atoi(std::string(style.alignment.begin(), style.alignment.end()).c_str());
        if (const auto at = text.find("\\an"); at != std::string::npos)
            an = std::atoi(text.c_str() + at + 3);
        if (an < 1 || an > 9)
            an = 2;
        const auto pos = visual::defaultPosition(*l, style, an, resolution.width, resolution.height);
        const double duration = static_cast<double>((l->end.value.microseconds() - l->start.value.microseconds()) / 1000);
        out.ids.push_back(l->id);
        out.lines.push_back(LineIn{text, style, duration, pos.x, pos.y});
    }
    return out;
}

std::expected<void, CommandRefusal> applyPlan(EditSession &session, const Run &run, const Plan &plan)
{
    std::map<core::LineId, std::u8string> changes;
    for (std::size_t i = 0; i < run.ids.size() && i < plan.texts.size(); ++i)
        if (plan.texts[i] != run.lines[i].text)
            changes.emplace(run.ids[i], std::u8string(plan.texts[i].begin(), plan.texts[i].end()));
    if (changes.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touched;
    for (const auto &[id, _] : changes)
        touched.insert(id);
    return session.run(Command{"Interpolate across Lines", session.revision(), touched, [&](core::Document &d) {
        for (const auto &[id, text] : changes)
            if (!d.editLine(id, [&](core::LineRecord &l) { l.text = text; }))
                return false;
        return true;
    }});
}

}
