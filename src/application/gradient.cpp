#include "hikari/application/gradient.h"

#include "motion_tags.h"

#include "hikari/core/graphemes.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace hikari::application::gradient {
namespace {

using motion::Piece;
constexpr double kInf = std::numeric_limits<double>::infinity();

// Longest names first, so "\fscx" is not read as "\fs".
constexpr std::string_view kNames[] = {
    "alpha", "xbord", "ybord", "xshad", "yshad", "iclip", "fscx", "fscy", "bord", "shad", "blur", "clip",
    "move",  "pos",   "org",   "fsp",   "frx",   "fry",   "frz",  "fax",  "fay",  "fs",   "fr",   "be",
    "1c",    "2c",    "3c",    "4c",    "1a",    "2a",    "3a",   "4a",   "an",   "c",    "p",    "t",
};

std::string_view nameOf(std::string_view tag)
{
    if (tag.empty() || tag.front() != '\\')
        return {};
    tag.remove_prefix(1);
    while (!tag.empty() && tag.front() == ' ')
        tag.remove_prefix(1);
    for (const auto name : kNames)
        if (tag.starts_with(name)) {
            if (name == "p" && tag.size() > 1 && !(tag[1] >= '0' && tag[1] <= '9'))
                continue;
            return name;
        }
    return {};
}

std::string_view argsOf(std::string_view tag)
{
    const auto name = nameOf(tag);
    return tag.substr(tag.find(name) + name.size());
}

std::string_view keyOf(std::string_view name)
{
    const auto *spec = lerp::tagSpec(name);
    return spec ? spec->key : name;
}

std::vector<Piece> withFirstBlock(std::vector<Piece> pieces)
{
    if (pieces.empty() || !pieces.front().block)
        pieces.insert(pieces.begin(), Piece{true, {}, {}});
    return pieces;
}

void dropKey(Piece &block, std::string_view key)
{
    std::erase_if(block.tags, [&](const std::string &t) {
        const auto name = nameOf(t);
        return !name.empty() && name != "t" && keyOf(name) == key;
    });
}

void dropKeyEverywhere(std::vector<Piece> &pieces, std::string_view key)
{
    for (auto &p : pieces)
        if (p.block)
            dropKey(p, key);
}

struct ClipRead {
    bool inverse = false;
    std::optional<Rect> rect;
    Shape shape;
};

std::optional<ClipRead> clipOf(const std::vector<Piece> &pieces)
{
    for (const auto &p : pieces)
        if (p.block)
            for (const auto &t : p.tags) {
                const auto name = nameOf(t);
                if (name != "clip" && name != "iclip")
                    continue;
                std::string_view args = argsOf(t);
                while (!args.empty() && args.front() == ' ')
                    args.remove_prefix(1);
                if (args.size() < 2 || args.front() != '(')
                    continue;
                args = args.substr(1, args.rfind(')') == std::string_view::npos ? args.size() - 1 : args.rfind(')') - 1);
                ClipRead out;
                out.inverse = name == "iclip";
                if (const auto rect = lerp::parse(*lerp::tagSpec("clip"), "(" + std::string(args) + ")")) {
                    const auto &v = rect->v;
                    out.rect = Rect{std::min(v[0], v[2]), std::min(v[1], v[3]), std::max(v[0], v[2]),
                                    std::max(v[1], v[3])};
                    out.shape = {rectContour(*out.rect)};
                    return out;
                }
                double divisor = 1;
                const auto comma = args.find(',');
                if (comma != std::string_view::npos && args.substr(0, comma).find('m') == std::string_view::npos) {
                    const int scale = std::max(1, std::atoi(std::string(args.substr(0, comma)).c_str()));
                    divisor = std::pow(2.0, scale - 1);
                    args.remove_prefix(comma + 1);
                }
                out.shape = parseDrawing(args, divisor);
                return out;
            }
    return std::nullopt;
}

void dropClips(std::vector<Piece> &pieces)
{
    for (auto &p : pieces)
        if (p.block)
            std::erase_if(p.tags, [](const std::string &t) {
                const auto name = nameOf(t);
                return name == "clip" || name == "iclip";
            });
}

std::string clipTag(const Shape &shape, bool rect)
{
    if (rect)
        if (const auto r = asRect(shape))
            return "\\clip(" + std::to_string(std::lround(r->x1)) + "," + std::to_string(std::lround(r->y1)) + "," +
                   std::to_string(std::lround(r->x2)) + "," + std::to_string(std::lround(r->y2)) + ")";
    return "\\clip(" + formatShape(shape) + ")";
}

std::optional<lerp::Value> valueIn(const Piece &block, std::string_view key)
{
    std::optional<lerp::Value> out;
    for (const auto &t : block.tags) {
        const auto name = nameOf(t);
        if (name.empty() || name == "t" || keyOf(name) != key)
            continue;
        if (const auto *spec = lerp::tagSpec(name))
            if (const auto v = lerp::parse(*spec, argsOf(t)))
                out = v;
    }
    return out;
}

struct Region {
    Shape shape;
    bool rect = false;
    bool inverse = false;
    bool fromClip = false;
};

std::expected<Region, Problem> regionOf(const LineIn &line)
{
    const auto pieces = motion::parseText(line.text);
    if (const auto clip = clipOf(pieces)) {
        if (clip->shape.empty())
            return std::unexpected(Problem::NoArea);
        return Region{clip->shape, clip->rect.has_value(), clip->inverse, true};
    }
    if (!line.box)
        return std::unexpected(Problem::NoArea);
    const Rect r{std::floor(line.box->x1), std::floor(line.box->y1), std::ceil(line.box->x2), std::ceil(line.box->y2)};
    return Region{{rectContour(r)}, true, false, false};
}

using TagsAt = std::function<std::string(double t)>;

struct Span {
    double from = 0, to = 0;
    double t = 0;
};

std::expected<std::vector<Shape>, Problem> linearShapes(const Spec &spec, const Region &region,
                                                        std::vector<double> *ts, const TagsAt &tags)
{
    Point end = spec.end;
    if (spec.mode == Mode::Rect) {
        if (std::abs(end.x - spec.start.x) >= std::abs(end.y - spec.start.y))
            end.y = spec.start.y;
        else
            end.x = spec.start.x;
    }
    const auto frame = linearFrame(spec.start, end, spec.mode == Mode::Skew ? spec.skew : 0);
    if (!frame || spec.strip <= 0)
        return std::unexpected(Problem::NoLength);
    const int count = std::max(2, static_cast<int>(std::ceil(frame->length / spec.strip)));
    std::vector<double> bounds(count + 1);
    bounds.front() = -kInf;
    bounds.back() = kInf;
    for (int k = 1; k < count; ++k) {
        double at = k * spec.strip;
        if (spec.mode == Mode::Rect) {
            const bool horizontal = end.y == spec.start.y;
            const double origin = horizontal ? spec.start.x : spec.start.y;
            const double sign = (horizontal ? end.x - spec.start.x : end.y - spec.start.y) < 0 ? -1 : 1;
            at = std::abs(std::round(origin + sign * at) - origin);
        }
        bounds[k] = at;
    }
    std::vector<Span> pieces;
    std::string last;
    for (int k = 0; k < count; ++k) {
        const double t = lerp::accelerate(static_cast<double>(k) / (count - 1), spec.accel);
        const std::string text = tags(t);
        const double overlap = k + 1 < count ? spec.overlap : 0;
        if (spec.merge && !pieces.empty() && text == last) {
            pieces.back().to = bounds[k + 1] + overlap;
            continue;
        }
        pieces.push_back({bounds[k], bounds[k + 1] + overlap, t});
        last = text;
    }
    std::vector<Shape> out;
    for (const auto &p : pieces) {
        auto shape = slab(region.shape, *frame, p.from, p.to);
        if (shape.empty())
            continue;
        out.push_back(std::move(shape));
        if (ts)
            ts->push_back(p.t);
    }
    if (out.empty())
        return std::unexpected(Problem::NoArea);
    return out;
}

std::expected<std::vector<Shape>, Problem> edgeShapes(const Spec &spec, const Region &region, std::vector<double> *ts,
                                                      const TagsAt &tags)
{
    if (!region.fromClip)
        return std::unexpected(Problem::NeedsClip);
    if (region.inverse)
        return std::unexpected(Problem::InverseClip);
    if (spec.strip <= 0)
        return std::unexpected(Problem::NoLength);
    std::vector<Shape> insets;
    for (std::size_t k = 0; k < 512; ++k) {
        auto s = inset(region.shape, static_cast<double>(k) * spec.strip);
        if (s.empty())
            break;
        insets.push_back(std::move(s));
    }
    if (insets.empty())
        return std::unexpected(Problem::NoArea);
    const std::size_t count = insets.size();
    std::vector<std::pair<std::size_t, std::size_t>> groups;
    std::vector<double> groupT;
    std::string last;
    for (std::size_t k = 0; k < count; ++k) {
        const double t = lerp::accelerate(count < 2 ? 0 : static_cast<double>(k) / (count - 1), spec.accel);
        const std::string text = tags(t);
        if (spec.merge && !groups.empty() && text == last) {
            groups.back().second = k;
            continue;
        }
        groups.emplace_back(k, k);
        groupT.push_back(t);
        last = text;
    }
    std::vector<Shape> out;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        Shape band = insets[groups[g].first];
        if (groups[g].second + 1 < count)
            for (const auto &c : insets[groups[g].second + 1])
                band.push_back(Contour(c.rbegin(), c.rend()));
        out.push_back(std::move(band));
        if (ts)
            ts->push_back(groupT[g]);
    }
    return out;
}

std::expected<std::vector<Shape>, Problem> shapesFor(const Spec &spec, const LineIn &line, std::vector<double> *ts,
                                                     const TagsAt &tags, bool *rect)
{
    const auto region = regionOf(line);
    if (!region)
        return std::unexpected(region.error());
    if (spec.mode != Mode::ClipEdge && region->inverse)
        return std::unexpected(Problem::InverseClip);
    *rect = region->rect && spec.mode == Mode::Rect;
    return spec.mode == Mode::ClipEdge ? edgeShapes(spec, *region, ts, tags) : linearShapes(spec, *region, ts, tags);
}

std::string valuesText(const Spec &spec, double t)
{
    std::string out;
    for (const auto &stop : spec.tags)
        if (const auto *s = lerp::tagSpec(stop.key))
            out += lerp::format(*s, lerp::lerp(*s, stop.from, stop.to, t, spec.blend));
    return out;
}

std::string withTags(std::vector<Piece> pieces, const std::vector<std::pair<std::string, std::string>> &tags,
                     const std::string &clip)
{
    pieces = withFirstBlock(std::move(pieces));
    dropClips(pieces);
    for (const auto &[key, tag] : tags)
        dropKeyEverywhere(pieces, key);
    for (const auto &[key, tag] : tags)
        pieces.front().tags.push_back(tag);
    if (!clip.empty())
        pieces.front().tags.push_back(clip);
    return motion::renderText(pieces);
}

std::vector<std::pair<std::string, std::string>> formattedTags(const Spec &spec, double t)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto &stop : spec.tags)
        if (const auto *s = lerp::tagSpec(stop.key))
            out.emplace_back(stop.key, lerp::format(*s, lerp::lerp(*s, stop.from, stop.to, t, spec.blend)));
    return out;
}

bool stripMode(Mode m)
{
    return m == Mode::Rect || m == Mode::Rotated || m == Mode::Skew || m == Mode::ClipEdge;
}

struct Unit {
    std::size_t piece = 0, from = 0, to = 0;
    bool space = false, breaks = false;
};

std::vector<Unit> unitsOf(const std::vector<Piece> &pieces)
{
    std::vector<Unit> out;
    for (std::size_t p = 0; p < pieces.size(); ++p) {
        if (pieces[p].block)
            continue;
        const std::string &text = pieces[p].text;
        std::size_t i = 0;
        while (i < text.size()) {
            if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == 'N' || text[i + 1] == 'n' || text[i + 1] == 'h')) {
                out.push_back({p, i, i + 2, text[i + 1] == 'h', text[i + 1] != 'h'});
                i += 2;
                continue;
            }
            std::size_t j = i + 1;
            while (j < text.size() && (static_cast<unsigned char>(text[j]) & 0xC0) == 0x80)
                ++j;
            const std::u16string wide = core::toUtf16(std::u8string(reinterpret_cast<const char8_t *>(text.data() + i),
                                                                    text.size() - i));
            const std::size_t end16 = core::graphemeEnd(wide, 0);
            const std::u8string head = core::toUtf8(std::u16string_view(wide).substr(0, end16));
            j = std::max(j, i + head.size());
            const bool space = text[i] == ' ' || text[i] == '\t';
            out.push_back({p, i, j, space, false});
            i = j;
        }
    }
    return out;
}

}

std::string replaceTag(std::string_view text, std::string_view key, std::string_view tag)
{
    auto pieces = withFirstBlock(motion::parseText(text));
    dropKeyEverywhere(pieces, key);
    pieces.front().tags.emplace_back(tag);
    return motion::renderText(pieces);
}

std::optional<Shape> vectorClip(std::string_view text)
{
    const auto clip = clipOf(motion::parseText(text));
    if (!clip || clip->rect || clip->inverse)
        return std::nullopt;
    return clip->shape;
}

std::optional<lerp::Value> startValue(std::string_view text, std::string_view key, const core::StyleValues &style)
{
    const auto pieces = motion::parseText(text);
    if (!pieces.empty() && pieces.front().block)
        if (const auto v = valueIn(pieces.front(), key))
            return v;
    const auto *spec = lerp::tagSpec(key);
    return spec ? lerp::styleDefault(*spec, style) : std::nullopt;
}

std::expected<std::vector<Shape>, Problem> stripShapes(const Spec &spec, const LineIn &line)
{
    bool rect = false;
    return shapesFor(spec, line, nullptr, [&](double t) { return valuesText(spec, t); }, &rect);
}

std::expected<std::vector<std::string>, Problem> stripTexts(const Spec &spec, const LineIn &line)
{
    if (spec.tags.empty())
        return std::unexpected(Problem::NoTags);
    std::vector<double> ts;
    bool rect = false;
    const auto shapes = shapesFor(spec, line, &ts, [&](double t) { return valuesText(spec, t); }, &rect);
    if (!shapes)
        return std::unexpected(shapes.error());
    const auto pieces = motion::parseText(line.text);
    std::vector<std::string> out;
    for (std::size_t i = 0; i < shapes->size(); ++i)
        out.push_back(withTags(pieces, formattedTags(spec, ts[i]), clipTag((*shapes)[i], rect)));
    return out;
}

std::expected<std::vector<std::string>, Problem> stripTextsFromStops(const Spec &spec,
                                                                     const std::vector<LineIn> &stops)
{
    if (stops.size() < 2)
        return std::unexpected(Problem::TooFewStops);
    std::vector<std::vector<Piece>> layouts;
    for (const auto &s : stops)
        layouts.push_back(withFirstBlock(motion::parseText(s.text)));
    for (const auto &l : layouts) {
        if (l.size() != layouts.front().size())
            return std::unexpected(Problem::StopsDiffer);
        for (std::size_t j = 0; j < l.size(); ++j)
            if (l[j].block != layouts.front()[j].block || (!l[j].block && l[j].text != layouts.front()[j].text))
                return std::unexpected(Problem::StopsDiffer);
    }
    struct Graded {
        std::size_t block;
        std::string key;
        std::vector<lerp::Value> values;
    };
    std::vector<Graded> graded;
    std::vector<std::map<std::string, lerp::Value>> carried(stops.size());
    for (std::size_t j = 0; j < layouts.front().size(); ++j) {
        if (!layouts.front()[j].block)
            continue;
        std::vector<std::string> keys;
        for (std::size_t s = 0; s < stops.size(); ++s)
            for (const auto &t : layouts[s][j].tags) {
                const auto name = nameOf(t);
                if (name.empty() || name == "t" || name == "clip" || name == "iclip" || !lerp::tagSpec(name))
                    continue;
                const std::string key(keyOf(name));
                if (const auto v = valueIn(layouts[s][j], key)) {
                    carried[s][key] = *v;
                    if (std::ranges::find(keys, key) == keys.end())
                        keys.push_back(key);
                }
            }
        for (const auto &key : keys) {
            Graded g{j, key, {}};
            for (std::size_t s = 0; s < stops.size(); ++s) {
                auto it = carried[s].find(key);
                std::optional<lerp::Value> v = it != carried[s].end() ? std::optional(it->second)
                                                                      : lerp::styleDefault(*lerp::tagSpec(key), stops[s].style);
                if (!v)
                    break;
                g.values.push_back(*v);
            }
            if (g.values.size() == stops.size() &&
                std::any_of(g.values.begin(), g.values.end(), [&](const auto &v) { return v != g.values.front(); }))
                graded.push_back(std::move(g));
        }
    }
    if (graded.empty())
        return std::unexpected(Problem::NothingDiffers);
    const auto tagsAt = [&](double t) {
        const auto at = lerp::stopAt(t, stops.size());
        std::vector<std::pair<std::size_t, std::string>> out;
        for (const auto &g : graded) {
            const auto *tag = lerp::tagSpec(g.key);
            out.emplace_back(g.block, lerp::format(*tag, lerp::lerp(*tag, g.values[at.index], g.values[at.index + 1],
                                                                    at.t, spec.blend)));
        }
        return out;
    };
    std::vector<double> ts;
    bool rect = false;
    const auto shapes = shapesFor(spec, stops.front(), &ts,
                                  [&](double t) {
                                      std::string all;
                                      for (const auto &[block, tag] : tagsAt(t))
                                          all += std::to_string(block) + tag;
                                      return all;
                                  },
                                  &rect);
    if (!shapes)
        return std::unexpected(shapes.error());
    std::vector<std::string> out;
    for (std::size_t i = 0; i < shapes->size(); ++i) {
        auto pieces = layouts.front();
        dropClips(pieces);
        for (const auto &g : graded)
            dropKey(pieces[g.block], g.key);
        for (const auto &[block, tag] : tagsAt(ts[i]))
            pieces[block].tags.push_back(tag);
        pieces.front().tags.push_back(clipTag((*shapes)[i], rect));
        out.push_back(motion::renderText(pieces));
    }
    return out;
}

std::expected<std::string, Problem> byCharText(const Spec &spec, const LineIn &line, const Measure &measure)
{
    if (spec.tags.empty())
        return std::unexpected(Problem::NoTags);
    auto pieces = withFirstBlock(motion::parseText(line.text));
    const auto units = unitsOf(pieces);
    std::vector<std::size_t> counted;
    for (std::size_t i = 0; i < units.size(); ++i)
        if (!units[i].breaks)
            counted.push_back(i);
    if (counted.empty())
        return std::unexpected(Problem::NoText);
    std::vector<double> place(counted.size(), 0);
    bool measuredOk = false;
    if (spec.measured && measure && counted.size() > 1) {
        std::string prefix;
        std::vector<double> centres;
        double before = 0;
        measuredOk = true;
        for (const auto i : counted) {
            const auto &u = units[i];
            const std::string unit = pieces[u.piece].text.substr(u.from, u.to - u.from);
            prefix += unit == "\\h" ? " " : unit;
            const auto after = measure(prefix);
            if (!after) {
                measuredOk = false;
                break;
            }
            centres.push_back((before + *after) / 2);
            before = *after;
        }
        if (measuredOk && centres.back() > centres.front())
            for (std::size_t k = 0; k < counted.size(); ++k)
                place[k] = (centres[k] - centres.front()) / (centres.back() - centres.front());
        else
            measuredOk = false;
    }
    if (!measuredOk)
        for (std::size_t k = 0; k < counted.size(); ++k)
            place[k] = counted.size() < 2 ? 0 : static_cast<double>(k) / (counted.size() - 1);
    struct Stop {
        double at;
        lerp::Value value;
    };
    std::vector<std::vector<Stop>> stops;
    for (const auto &tag : spec.tags) {
        std::vector<Stop> list{{0, tag.from}};
        std::size_t k = 0;
        for (std::size_t p = 0; p < pieces.size(); ++p) {
            if (pieces[p].block && p > 0) {
                while (k < counted.size() && units[counted[k]].piece < p)
                    ++k;
                if (k > 0 && k < counted.size())
                    if (const auto v = valueIn(pieces[p], tag.key))
                        list.push_back({place[k], *v});
            }
        }
        list.push_back({1, tag.to});
        stops.push_back(std::move(list));
    }
    for (const auto &tag : spec.tags)
        dropKeyEverywhere(pieces, tag.key);
    const auto valueAt = [&](std::size_t tag, double at) {
        const auto &list = stops[tag];
        std::size_t i = 0;
        while (i + 2 < list.size() && at > list[i + 1].at)
            ++i;
        const double span = list[i + 1].at - list[i].at;
        const double local = lerp::accelerate(span > 0 ? (at - list[i].at) / span : 0, spec.accel);
        const auto *s = lerp::tagSpec(spec.tags[tag].key);
        return lerp::format(*s, lerp::lerp(*s, list[i].value, list[i + 1].value, local, spec.blend));
    };
    std::vector<Piece> out;
    std::vector<std::string> shown(spec.tags.size());
    std::size_t k = 0;
    for (std::size_t p = 0; p < pieces.size(); ++p) {
        if (pieces[p].block) {
            out.push_back(pieces[p]);
            continue;
        }
        const std::string &text = pieces[p].text;
        std::size_t done = 0;
        for (std::size_t i = 0; i < units.size(); ++i) {
            const auto &u = units[i];
            if (u.piece != p)
                continue;
            if (u.breaks || u.space) {
                if (!u.breaks)
                    ++k;
                continue;
            }
            std::vector<std::string> tags;
            for (std::size_t t = 0; t < spec.tags.size(); ++t) {
                auto tag = valueAt(t, place[k]);
                if (tag != shown[t]) {
                    shown[t] = tag;
                    tags.push_back(std::move(tag));
                }
            }
            ++k;
            if (tags.empty())
                continue;
            if (u.from > done) {
                out.push_back({false, text.substr(done, u.from - done), {}});
                done = u.from;
            }
            if (out.empty() || !out.back().block)
                out.push_back({true, {}, {}});
            for (auto &t : tags)
                out.back().tags.push_back(std::move(t));
        }
        if (done < text.size())
            out.push_back({false, text.substr(done), {}});
    }
    return motion::renderText(out);
}

std::expected<std::string, Problem> byLineText(const Spec &spec, const LineIn &line, std::size_t index,
                                               std::size_t count)
{
    if (spec.tags.empty())
        return std::unexpected(Problem::NoTags);
    const double t = lerp::accelerate(count < 2 ? 0 : static_cast<double>(index) / (count - 1), spec.accel);
    std::string text = line.text;
    for (const auto &[key, tag] : formattedTags(spec, t))
        text = replaceTag(text, key, tag);
    return text;
}

std::expected<std::string, Problem> maskText(const Spec &spec, const LineIn &line)
{
    std::string drawing;
    switch (spec.shape) {
    case MaskShape::Wedge:
        drawing = wedgeMask(spec.mask, spec.thickness, spec.spacing);
        break;
    case MaskShape::Box:
        drawing = boxMask(spec.mask, spec.spacing);
        break;
    case MaskShape::Ring:
        drawing = ringMask(spec.mask.p1, spec.mask.p2, spec.spacing);
        break;
    case MaskShape::Star:
        drawing = starMask(spec.mask.p1, spec.mask.p2, spec.spikes, spec.centre);
        break;
    case MaskShape::Vignette:
        drawing = vignetteMask(spec.mask.p1, spec.mask.p2, spec.spacing, spec.hole);
        break;
    }
    if (drawing.empty())
        return std::unexpected(Problem::NoLength);
    auto pieces = motion::parseText(line.text);
    Piece first{true, {}, {}};
    if (!pieces.empty() && pieces.front().block)
        first = pieces.front();
    static const std::set<std::string_view> dropped{"xbord", "ybord", "xshad", "yshad", "fax", "fay", "frx", "fry",
                                                    "pos",   "move",  "an",    "fscx",  "fscy", "bord", "shad", "p"};
    std::erase_if(first.tags, [](const std::string &t) {
        const auto name = nameOf(t);
        if (dropped.contains(name))
            return true;
        return (name == "clip" || name == "iclip") && t.find('m') != std::string::npos;
    });
    for (const char *tag : {"\\an7", "\\pos(0,0)", "\\fscx100", "\\fscy100", "\\bord0", "\\shad0", "\\p1"})
        first.tags.emplace_back(tag);
    return motion::renderText({first}) + drawing;
}

}
