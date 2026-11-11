#include "hikari/application/layer_builders.h"

#include "override_blocks.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace hikari::application {
namespace {

using namespace blocks;
using Text = std::vector<Piece>;

enum class Where { Outside, Inside, Everywhere };

bool reaches(Where where, bool inTransform)
{
    return where == Where::Everywhere || (where == Where::Inside) == inTransform;
}

struct Transform {
    u8s head;
    std::vector<Item> inner;
    u8s tail;
};

std::optional<Transform> splitTransform(u8v value)
{
    const std::size_t tags = value.find(u8'\\');
    if (tags == u8v::npos)
        return std::nullopt;
    std::size_t close = value.rfind(u8')');
    if (close == u8v::npos || close < tags)
        close = value.size();
    return Transform{u8s(value.substr(0, tags)), parseTags(value.substr(tags, close - tags)), u8s(value.substr(close))};
}

u8s joinTransform(const Transform &t)
{
    u8s out = t.head;
    for (const auto &item : t.inner)
        out += item.tag ? u8"\\" + item.tag->name + item.tag->value : item.raw;
    return out + t.tail;
}

bool hasTags(const std::vector<Item> &items)
{
    return std::ranges::any_of(items, [](const Item &i) { return i.tag.has_value(); });
}

using ListEdit = std::function<void(std::vector<Item> &, bool inTransform)>;

void eachList(Text &text, const ListEdit &edit)
{
    for (auto &piece : text) {
        if (!piece.block)
            continue;
        const bool had = !piece.items.empty();
        edit(piece.items, false);
        std::erase_if(piece.items, [&](Item &item) {
            if (!item.tag || item.tag->name != u8"t")
                return false;
            auto t = splitTransform(item.tag->value);
            if (!t)
                return false;
            edit(t->inner, true);
            if (!hasTags(t->inner))
                return true;
            item.tag->value = joinTransform(*t);
            return false;
        });
        if (had && piece.items.empty())
            piece.emptied = true;
    }
}

bool named(const Item &item, std::initializer_list<u8v> names)
{
    if (!item.tag)
        return false;
    const u8s name = canonical(item.tag->name);
    return std::ranges::find(names, u8v(name)) != names.end();
}

void drop(Text &text, std::initializer_list<u8v> names, Where where = Where::Everywhere)
{
    eachList(text, [&](std::vector<Item> &items, bool inT) {
        if (reaches(where, inT))
            std::erase_if(items, [&](const Item &i) { return named(i, names); });
    });
}

void mapValues(Text &text, std::initializer_list<u8v> names, const std::function<u8s(u8v)> &to,
               Where where = Where::Everywhere)
{
    eachList(text, [&](std::vector<Item> &items, bool inT) {
        if (!reaches(where, inT))
            return;
        for (auto &item : items)
            if (named(item, names))
                item.tag->value = to(item.tag->value);
    });
}

void follow(Text &text, std::initializer_list<u8v> names, const std::function<Tag(const Tag &)> &make,
            Where where = Where::Everywhere)
{
    eachList(text, [&](std::vector<Item> &items, bool inT) {
        if (!reaches(where, inT))
            return;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (!named(items[i], names))
                continue;
            Item made{make(*items[i].tag), {}};
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(++i), std::move(made));
        }
    });
}

struct Facts {
    double bord = 0, xbord = 0, ybord = 0;
    double xshad = 0, yshad = 0;
    double blur = 0;
    bool blurred = false;
    core::Colour fill, outline, back;
    bool hasBorder() const { return std::max({bord, xbord, ybord}) > 0; }
};

Facts styleFacts(const core::StyleValues &style)
{
    Facts f;
    f.bord = f.xbord = f.ybord = number(style.outlineWidth);
    f.xshad = f.yshad = number(style.shadow);
    f.fill = style.primary;
    f.outline = style.outline;
    f.back = style.back;
    return f;
}

const core::StyleValues &styleNamed(u8v name, const core::StyleValues &fallback,
                                    const std::vector<core::StyleValues> &styles)
{
    const auto found = std::ranges::find(styles, name, &core::StyleValues::name);
    return name.empty() || found == styles.end() ? fallback : *found;
}

void readTag(Facts &f, const Tag &tag)
{
    const u8s name = canonical(tag.name);
    const double v = number(tag.value);
    if (name == u8"bord")
        f.bord = f.xbord = f.ybord = v;
    else if (name == u8"xbord")
        f.xbord = v;
    else if (name == u8"ybord")
        f.ybord = v;
    else if (name == u8"shad")
        f.xshad = f.yshad = v;
    else if (name == u8"xshad")
        f.xshad = v;
    else if (name == u8"yshad")
        f.yshad = v;
    else if (name == u8"blur") {
        f.blur = v;
        f.blurred = true;
    } else if (name == u8"be")
        f.blurred = true;
    else if (name == u8"1c" || name == u8"3c" || name == u8"4c") {
        auto &c = name == u8"1c" ? f.fill : name == u8"3c" ? f.outline : f.back;
        const auto read = core::legacy::colour(tag.value);
        c.r = read.r, c.g = read.g, c.b = read.b;
    } else if (name == u8"1a" || name == u8"3a" || name == u8"4a" || name == u8"alpha") {
        const int a = hexValue(tag.value) & 0xFF;
        if (name == u8"1a" || name == u8"alpha")
            f.fill.a = a;
        if (name == u8"3a" || name == u8"alpha")
            f.outline.a = a;
        if (name == u8"4a" || name == u8"alpha")
            f.back.a = a;
    }
}

Facts lineFacts(const Text &text, const core::StyleValues &style, const std::vector<core::StyleValues> &styles)
{
    Facts f = styleFacts(style);
    if (text.empty() || !text.front().block)
        return f;
    for (const auto &item : text.front().items) {
        if (!item.tag || item.tag->name == u8"t")
            continue;
        if (item.tag->name == u8"r")
            f = styleFacts(styleNamed(item.tag->value, style, styles));
        else
            readTag(f, *item.tag);
    }
    return f;
}

u8s num(double v)
{
    return wide(fixed(v));
}

u8s colour(const core::Colour &c)
{
    return format(Kind::Colour, {double(c.r), double(c.g), double(c.b)});
}

u8s alpha(int a)
{
    return format(Kind::Alpha, {double(a)});
}

struct Context {
    const core::StyleValues &style;
    const std::vector<core::StyleValues> &styles;
    Facts facts;
};

using Heads = std::function<std::vector<Tag>(const Facts &)>;

bool segmentHas(const std::vector<Item> &items, std::size_t from, u8v name)
{
    for (std::size_t i = from; i < items.size() && !(items[i].tag && items[i].tag->name == u8"r"); ++i)
        if (items[i].tag && canonical(items[i].tag->name) == canonical(name))
            return true;
    return false;
}

void insertMissing(std::vector<Item> &items, std::size_t at, const std::vector<Tag> &tags)
{
    std::vector<Item> missing;
    for (const auto &tag : tags)
        if (!segmentHas(items, at, tag.name))
            missing.push_back(Item{tag, {}});
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(at), missing.begin(), missing.end());
}

// At the Line's start and after each \r, the tags its segment has none of.
void head(Text &text, const Context &cx, const Heads &tags)
{
    if (text.empty() || !text.front().block)
        text.insert(text.begin(), Piece{true, false, {}, {}});
    text.front().emptied = false;
    insertMissing(text.front().items, 0, tags(cx.facts));
    for (auto &piece : text) {
        if (!piece.block)
            continue;
        for (std::size_t i = 0; i < piece.items.size(); ++i)
            if (piece.items[i].tag && piece.items[i].tag->name == u8"r")
                insertMissing(piece.items, i + 1, tags(styleFacts(styleNamed(piece.items[i].tag->value, cx.style, cx.styles))));
    }
}

void headTag(Text &text, const Context &cx, u8v name, const std::function<u8s(const Facts &)> &value)
{
    head(text, cx, [&](const Facts &f) { return std::vector<Tag>{Tag{u8s(name), value(f)}}; });
}

void addDefaultBlur(Text &text, const Context &cx, double blur)
{
    bool blurred = false;
    eachList(text, [&](std::vector<Item> &items, bool) {
        blurred = blurred || std::ranges::any_of(items, [](const Item &i) { return named(i, {u8"blur", u8"be"}); });
    });
    if (!blurred)
        headTag(text, cx, u8"blur", [&](const Facts &) { return num(blur); });
}

void hideShadow(Text &text, const Context &cx)
{
    drop(text, {u8"shad", u8"xshad", u8"yshad"});
    headTag(text, cx, u8"shad", [](const Facts &) { return u8s(u8"0"); });
}

std::function<u8s(u8v)> plus(double delta)
{
    return [delta](u8v v) { return num(number(v) + delta); };
}

void widenBorder(Text &text, const Context &cx, double delta)
{
    mapValues(text, {u8"bord", u8"xbord", u8"ybord"}, plus(delta), Where::Outside);
    headTag(text, cx, u8"bord", [&](const Facts &f) { return num(f.bord + delta); });
}

double lineBlur(const Context &cx, const LayerOptions &o)
{
    return cx.facts.blurred ? cx.facts.blur : o.defaultBlur;
}

std::function<u8s(u8v)> atLeast(int a)
{
    return [a](u8v v) { return alpha(std::max(hexValue(v) & 0xFF, a)); };
}

Text glowLayer(Text text, const Context &cx, const GlowLayer &g, bool hideShadows)
{
    mapValues(text, {u8"blur"}, [&](u8v) { return num(g.blur); });
    drop(text, {u8"be"});
    headTag(text, cx, u8"blur", [&](const Facts &) { return num(g.blur); });
    const bool bordered = cx.facts.hasBorder();
    if (bordered && g.border != 0)
        widenBorder(text, cx, g.border);
    mapValues(text, {u8"alpha", bordered ? u8"3a" : u8"1a"}, atLeast(g.alpha));
    if (g.alpha != 0)
        headTag(text, cx, u8"alpha", [&](const Facts &) { return alpha(g.alpha); });
    if (g.colour) {
        drop(text, {bordered ? u8"3c" : u8"1c"});
        headTag(text, cx, bordered ? u8"3c" : u8"c", [&](const Facts &) { return colour(*g.colour); });
    }
    if (hideShadows)
        hideShadow(text, cx);
    return text;
}

void addFadeFix(Text &text, const Context &cx, const LayerOptions &o, std::int64_t durationMs)
{
    std::optional<std::vector<u8s>> fad;
    for (const auto &piece : text)
        for (const auto &item : piece.items)
            if (!fad && item.tag && item.tag->name == u8"fad")
                fad = arguments(item.tag->value);
    if (!fad || fad->size() != 2)
        return;
    const double in = number((*fad)[0]), out = number((*fad)[1]);
    const u8s shown = alpha(static_cast<int>(cx.facts.outline.a));
    auto &first = text.front().items;
    const auto transform = [&](double t1, double t2, const u8s &a) {
        return Item{Tag{u8"t", u8"(" + num(std::max(0.0, t1)) + u8"," + num(std::max(0.0, t2)) + u8",\\1a" + a + u8")"}, {}};
    };
    if (in > 0) {
        std::erase_if(first, [](const Item &i) { return named(i, {u8"1a"}); });
        first.push_back(Item{Tag{u8"1a", alpha(0xFF)}, {}});
        first.push_back(transform(in - o.fadeMs, in, shown));
    }
    if (out > 0) {
        const double fadeOut = double(durationMs) - out;
        first.push_back(transform(fadeOut, fadeOut + o.fadeMs, alpha(0xFF)));
    }
}

Text borderLayer(Text text, const Context &cx, const LayerOptions &o, std::int64_t durationMs)
{
    if (o.under == UnderBorder::BorderColour) {
        drop(text, {u8"1c", u8"1a"});
        follow(text, {u8"3c"}, [](const Tag &t) { return Tag{u8"c", t.value}; });
        follow(text, {u8"3a"}, [](const Tag &t) { return Tag{u8"1a", t.value}; });
        head(text, cx, [](const Facts &f) {
            return std::vector<Tag>{Tag{u8"c", colour(f.outline)}, Tag{u8"1a", alpha(static_cast<int>(f.outline.a))}};
        });
        if (o.fadeFix)
            addFadeFix(text, cx, o, durationMs);
    } else {
        widenBorder(text, cx, lineBlur(cx, o));
        eachList(text, [](std::vector<Item> &items, bool) {
            for (auto &i : items)
                if (i.tag && i.tag->name == u8"alpha")
                    i.tag->name = u8"3a";
        });
        drop(text, {u8"1a"});
        headTag(text, cx, u8"1a", [](const Facts &) { return alpha(0xFF); });
    }
    if (o.borderBlur) {
        mapValues(text, {u8"blur"}, [&](u8v) { return num(*o.borderBlur); });
        headTag(text, cx, u8"blur", [&](const Facts &) { return num(*o.borderBlur); });
    }
    return text;
}

Text secondBorderLayer(Text text, const Context &cx, const LayerOptions &o)
{
    widenBorder(text, cx, o.secondWidth);
    drop(text, {u8"3c"});
    const auto ring = [&](const Facts &f) { return colour(o.secondColour.value_or(f.fill)); };
    if (o.under == UnderBorder::BorderColour) {
        drop(text, {u8"1c"});
        head(text, cx, [&](const Facts &f) { return std::vector<Tag>{Tag{u8"3c", ring(f)}, Tag{u8"c", ring(f)}}; });
    } else {
        headTag(text, cx, u8"3c", ring);
    }
    return text;
}

Text topLayer(Text text, const Context &cx, const LayerOptions &o)
{
    drop(text, {u8"bord", u8"xbord", u8"ybord", u8"shad", u8"xshad", u8"yshad"}, Where::Inside);
    drop(text, {u8"3c", u8"3a", u8"xbord", u8"ybord"});
    if (o.under == UnderBorder::BorderColour) {
        mapValues(text, {u8"bord"}, [](u8v) { return u8s(u8"0"); });
        headTag(text, cx, u8"bord", [](const Facts &) { return u8s(u8"0"); });
    } else {
        const double thin = lineBlur(cx, o);
        mapValues(text, {u8"bord"}, [&](u8v) { return num(thin); });
        follow(text, {u8"1c"}, [](const Tag &t) { return Tag{u8"3c", t.value}; });
        follow(text, {u8"1a"}, [](const Tag &t) { return Tag{u8"3a", t.value}; });
        head(text, cx, [&](const Facts &f) {
            return std::vector<Tag>{Tag{u8"bord", num(thin)}, Tag{u8"3c", colour(f.fill)},
                                    Tag{u8"3a", alpha(static_cast<int>(f.fill.a))}};
        });
    }
    return text;
}

double cut(double v)
{
    return std::trunc(v * 100) / 100;
}

core::Colour mix(const core::Colour &a, const core::Colour &b, double t)
{
    const auto at = [t](std::int64_t x, std::int64_t y) { return std::llround(double(x) + double(y - x) * t); };
    return core::Colour{at(a.r, b.r), at(a.g, b.g), at(a.b, b.b), 0};
}

Text shadowCopy(Text text, const Context &cx, const LayerOptions &o, int k, int steps)
{
    const Facts &f = cx.facts;
    const double t = steps > 1 ? double(k - 1) / double(steps - 1) : 0;
    const core::Colour near = o.nearColour.value_or(f.back), far = o.farColour.value_or(f.back);
    const int a = static_cast<int>(std::lround(o.nearAlpha + (o.farAlpha - o.nearAlpha) * t));
    drop(text, {u8"shad", u8"xshad", u8"yshad", u8"alpha", u8"1a", u8"3a", u8"4a", u8"4c"});
    const double share = double(k) / double(steps);
    head(text, cx, [&](const Facts &) {
        return std::vector<Tag>{Tag{u8"xshad", num(cut(f.xshad * share))}, Tag{u8"yshad", num(cut(f.yshad * share))},
                                Tag{u8"1a", alpha(0xFE)},                  Tag{u8"3a", alpha(0xFF)},
                                Tag{u8"4c", colour(mix(near, far, t))},    Tag{u8"4a", alpha(a)}};
    });
    return text;
}

int shadowSteps(const Facts &f, const LayerOptions &o)
{
    const double offset = std::max(std::abs(f.xshad), std::abs(f.yshad));
    if (offset < 1)
        return 0;
    return o.steps ? *o.steps : static_cast<int>(std::floor(offset));
}

const core::StyleValues &lineStyle(const core::LineRecord &line, const std::vector<core::StyleValues> &styles)
{
    static const core::StyleValues none;
    const auto found = std::ranges::find(styles, line.style, &core::StyleValues::name);
    if (found != styles.end())
        return *found;
    const auto fallback = std::ranges::find(styles, u8s(u8"Default"), &core::StyleValues::name);
    return fallback != styles.end() ? *fallback : none;
}

std::int64_t durationMs(const core::LineRecord &line)
{
    return (line.end.value.microseconds() - line.start.value.microseconds()) / 1000;
}

bool anythingOn(const LayerOptions &o)
{
    return (o.glow && !o.glows.empty()) || o.border || o.shadow;
}

}

BuiltLayers buildLayers(std::u8string_view source, const core::StyleValues &style,
                        const std::vector<core::StyleValues> &styles, std::int64_t duration, const LayerOptions &o)
{
    const Text text = parse(source);
    const Context cx{style, styles, lineFacts(text, style, styles)};
    BuiltLayers out;
    const bool border = o.border && cx.facts.hasBorder();
    out.skippedBorder = o.border && !border;
    const int steps = o.shadow ? shadowSteps(cx.facts, o) : 0;
    out.skippedShadow = o.shadow && steps < 1;
    const bool shadow = steps >= 1;
    Text base = text;
    addDefaultBlur(base, cx, o.defaultBlur);
    if (o.glow)
        for (const auto &g : o.glows)
            out.below.push_back(serialize(glowLayer(base, cx, g, shadow)));
    for (int k = steps; k >= 1; --k)
        out.below.push_back(serialize(shadowCopy(base, cx, o, k, steps)));
    if (border) {
        Text ring = borderLayer(base, cx, o, duration);
        if (o.secondBorder) {
            Text second = secondBorderLayer(ring, cx, o);
            if (shadow)
                hideShadow(second, cx);
            out.below.push_back(serialize(second));
        }
        if (shadow || o.secondBorder)
            hideShadow(ring, cx);
        out.below.push_back(serialize(ring));
    }
    if (border || shadow) {
        Text top = border ? topLayer(base, cx, o) : text;
        hideShadow(top, cx);
        out.top = serialize(top);
    } else {
        out.top = u8s(source);
    }
    return out;
}

std::expected<LayerPlan, LayerProblem> planLayers(const core::Document &doc, const std::set<core::LineId> &selection,
                                                  const LayerOptions &o, const LineVisible &visible)
{
    if (!anythingOn(o))
        return std::unexpected(LayerProblem::NothingOn);
    if (o.glow && std::ssize(o.glows) > kMaxGlows)
        return std::unexpected(LayerProblem::TooManyGlows);
    if (o.steps && (*o.steps < 1 || *o.steps > kMaxShadowSteps))
        return std::unexpected(LayerProblem::TooManySteps);
    const auto styles = core::decodeStyles(doc);
    const auto lines = doc.lines();
    LayerPlan plan;
    bool any = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto &line = *lines[i];
        if (!selection.contains(line.id) || line.comment || line.unparsed || (visible && !visible(line.id)))
            continue;
        any = true;
        if (o.shadow && !o.steps) {
            const Text text = parse(line.text);
            const auto &style = lineStyle(line, styles);
            if (shadowSteps(lineFacts(text, style, styles), o) > kMaxShadowSteps)
                return std::unexpected(LayerProblem::TooManySteps);
        }
        auto built = buildLayers(line.text, lineStyle(line, styles), styles, durationMs(line), o);
        plan.skippedBorder += built.skippedBorder;
        plan.skippedShadow += built.skippedShadow;
        if (built.below.empty())
            continue;
        const auto k = static_cast<std::int64_t>(built.below.size());
        plan.built += static_cast<int>(k);
        if (plan.built > kMaxBuiltLines)
            return std::unexpected(LayerProblem::TooManyLines);
        plan.sources.push_back(LayerSource{line.id, static_cast<int>(i) + 1, line.layer.value, line.layer.value + k,
                                           std::move(built)});
    }
    if (!any)
        return std::unexpected(LayerProblem::NoLines);
    std::set<core::LineId> sources;
    for (const auto &s : plan.sources)
        sources.insert(s.id);
    for (const auto &s : plan.sources) {
        const auto &src = *lines[static_cast<std::size_t>(s.row - 1)];
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto &other = *lines[i];
            const int row = static_cast<int>(i) + 1;
            if (other.comment || sources.contains(other.id) || (visible && !visible(other.id)))
                continue;
            const bool shown = other.start.value < src.end.value && src.start.value < other.end.value;
            const bool above = other.layer.value > s.layer || (other.layer.value == s.layer && row > s.row);
            if (shown && above && other.layer.value <= s.newLayer)
                plan.covered.push_back(CoveredLine{row, other.layer.value, s.row});
        }
    }
    return plan;
}

bool applyLayerPlan(core::Document &doc, const LayerPlan &plan)
{
    std::map<core::LineId, core::LineRecord> originals;
    for (const auto *line : doc.lines())
        originals.emplace(line->id, *line);
    const auto layerField = [](std::int64_t v) {
        const auto s = std::to_string(v);
        return core::IntField{u8s(s.begin(), s.end()), v};
    };
    for (const auto &s : plan.sources) {
        const auto found = originals.find(s.id);
        if (found == originals.end())
            return false;
        for (std::size_t n = 0; n < s.layers.below.size(); ++n) {
            core::LineRecord copy = found->second;
            copy.text = s.layers.below[n];
            copy.layer = layerField(s.layer + static_cast<std::int64_t>(n));
            copy.bookmark = false;
            copy.group = core::GroupMarker::None;
            copy.extra.clear();
            copy.extradataIds.clear();
            copy.generatedFrom = {};
            if (!doc.insertLineBefore(s.id, std::move(copy)))
                return false;
        }
        const bool edited = doc.editLine(s.id, [&](core::LineRecord &line) {
            line.text = s.layers.top;
            line.layer = layerField(s.newLayer);
        });
        if (!edited)
            return false;
    }
    return true;
}

std::expected<void, CommandRefusal> buildLayerStacks(EditSession &session, const LayerOptions &options,
                                                     const LineVisible &visible)
{
    const auto plan = planLayers(session.document(), session.selection().selected, options, visible);
    if (!plan || plan->sources.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touches;
    for (const auto &s : plan->sources)
        touches.insert(s.id);
    const auto selection = session.selection();
    const auto ran = session.run(Command{"Blur and glow layers", session.revision(), touches,
                                         [&](core::Document &d) { return applyLayerPlan(d, *plan); }});
    if (!ran)
        return ran;
    session.setSelection(selection);
    return {};
}

}
