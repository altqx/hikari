#include "hikari/application/frame_bake.h"

#include "override_blocks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>

namespace hikari::application {
namespace {

using namespace blocks;

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
