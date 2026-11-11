#include "hikari/application/node_effects.h"

#include "hikari/application/karaoke_layout.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/template_preview.h"

#include <boost/dynamic_bitset.hpp>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <charconv>
#include <mutex>
#include <numbers>
#include <thread>

namespace hikari::application {

using json::Json;
using karaoke_layout::numberText;
using karaoke_layout::Unit;

struct NodeEffectCache::Store {
    std::mutex mutex;
    std::map<std::string, std::vector<std::pair<std::vector<int>, core::LineRecord>>> lines;
};

NodeEffectCache::NodeEffectCache() : m_store(std::make_unique<Store>()) {}
NodeEffectCache::~NodeEffectCache() = default;

void NodeEffectCache::clear()
{
    std::scoped_lock lock(m_store->mutex);
    m_store->lines.clear();
}

std::size_t NodeEffectCache::size() const
{
    std::scoped_lock lock(m_store->mutex);
    return m_store->lines.size();
}

namespace {

NodeColour nodeColour(karaoke_layout::StyleColour c)
{
    return {c.r, c.g, c.b};
}

enum class Kind {
    ForEach, Repeat, Frames, ClipGradient, Array, Particles, Highlights, InlineFx, Info, Number, Text, Colour, Point, Choose, Calculate, Compare, Progress, Random,
    Retime, Stagger, Tag, Move, Fade, Transform, Gradient, Shape, AudioPeaks, Join, Emit, Lua
};

const std::map<std::string, Kind, std::less<>> kKinds{
    {"units.for_each", Kind::ForEach}, {"units.repeat", Kind::Repeat},     {"units.frames", Kind::Frames},
    {"units.clip_gradient", Kind::ClipGradient}, {"units.array", Kind::Array}, {"units.particles", Kind::Particles},
    {"units.highlights", Kind::Highlights}, {"units.inline_fx", Kind::InlineFx},
    {"units.info", Kind::Info},        {"values.number", Kind::Number},    {"values.text", Kind::Text},
    {"values.colour", Kind::Colour},   {"values.point", Kind::Point},      {"values.choose", Kind::Choose},
    {"math.calculate", Kind::Calculate}, {"math.compare", Kind::Compare},  {"math.progress", Kind::Progress},
    {"math.random", Kind::Random},     {"time.retime", Kind::Retime},      {"time.stagger", Kind::Stagger},
    {"tags.tag", Kind::Tag},           {"tags.move", Kind::Move},          {"tags.fade", Kind::Fade},
    {"tags.transform", Kind::Transform}, {"colours.gradient", Kind::Gradient}, {"drawings.shape", Kind::Shape},
    {"audio.peaks", Kind::AudioPeaks},
    {"text.join", Kind::Join},         {"output.emit", Kind::Emit},        {"lua.code", Kind::Lua}};

enum class Level { Constant, Line, Element };

enum class UnitKind { Line, Syl, Char, Furi };

std::string narrow(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

std::u8string wide(std::string_view s)
{
    return std::u8string(s.begin(), s.end());
}

NodeColour colourOf(const Json &value)
{
    const std::string &s = value.asString();
    const auto hex = [&](std::size_t at) { return static_cast<int>(std::stoul(s.substr(at, 2), nullptr, 16)); };
    return {hex(1), hex(3), hex(5)};
}

NodeValue constantOf(const Json &value, PortType type)
{
    switch (type) {
    case PortType::Text:
        return value.isString() ? NodeValue(value.asString()) : NodeValue(std::string());
    case PortType::Colour:
        return value.isString() ? NodeValue(colourOf(value)) : NodeValue(NodeColour{});
    case PortType::Point:
        return value.isArray() ? NodeValue(NodePoint{value.asArray()[0].asNumber(), value.asArray()[1].asNumber()})
                               : NodeValue(NodePoint{});
    case PortType::Bool:
        return NodeValue(value.isBool() && value.asBool());
    case PortType::Tags:
        return NodeValue(NodeTags{});
    case PortType::Drawing:
        return NodeValue(NodeDrawing{});
    default:
        return NodeValue(value.isNumber() ? value.asNumber() : 0.0);
    }
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    std::uint64_t z = h;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::uint64_t hashText(std::string_view s)
{
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001B3ULL;
    }
    return h;
}

std::string hexByte(int v)
{
    constexpr char kDigits[] = "0123456789ABCDEF";
    v = std::clamp(v, 0, 255);
    return {kDigits[v >> 4], kDigits[v & 15]};
}

std::string assColour(const NodeColour &c)
{
    return "&H" + hexByte(c.b) + hexByte(c.g) + hexByte(c.r) + "&";
}

std::string assAlpha(double a)
{
    return "&H" + hexByte(static_cast<int>(std::floor(a + 0.5))) + "&";
}

double toThousandths(double v)
{
    return std::floor(v * 1000 + 0.5) / 1000;
}

double unitDraw(std::uint64_t h)
{
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

NodeColour gradientAt(const std::vector<NodeValue> &stops, double progress)
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (stops.size() == 1 || std::isnan(p))
        return std::get<NodeColour>(stops.front());
    const double at = p * double(stops.size() - 1);
    const std::size_t k = std::min(static_cast<std::size_t>(at), stops.size() - 2);
    const double f = at - double(k);
    const NodeColour a = std::get<NodeColour>(stops[k]), b = std::get<NodeColour>(stops[k + 1]);
    const auto channel = [&](int x, int y) { return static_cast<int>(std::floor(x + (y - x) * f + 0.5)); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

struct PathStop {
    NodePoint at;
    double angle = 0;
};

std::expected<std::vector<PathStop>, std::string> pathStops(const NodeDrawing &drawing, int count)
{
    std::vector<std::string> tokens;
    std::string token;
    for (const char c : drawing.commands + ' ') {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!token.empty())
                tokens.push_back(std::move(token));
            token.clear();
        } else {
            token += c;
        }
    }
    const double scale = std::ldexp(1.0, 1 - std::max(1, drawing.scale));
    std::vector<std::pair<NodePoint, NodePoint>> segments;
    NodePoint pen;
    char command = 0;
    std::size_t i = 0;
    const auto coordinate = [&](std::size_t at) -> std::optional<NodePoint> {
        if (at + 1 >= tokens.size())
            return std::nullopt;
        const auto read = [](const std::string &t) -> std::optional<double> {
            double v = 0;
            const auto [end, error] = std::from_chars(t.data(), t.data() + t.size(), v);
            if (error != std::errc() || end != t.data() + t.size() || !std::isfinite(v))
                return std::nullopt;
            return v;
        };
        const auto x = read(tokens[at]), y = read(tokens[at + 1]);
        if (!x || !y)
            return std::nullopt;
        return NodePoint{*x * scale, *y * scale};
    };
    while (i < tokens.size()) {
        if (tokens[i].size() == 1 && std::isalpha(static_cast<unsigned char>(tokens[i][0]))) {
            command = tokens[i][0];
            ++i;
            if (command != 'm' && command != 'n' && command != 'l' && command != 'b')
                return std::unexpected("uses a path command other than m, n, l and b");
            continue;
        }
        if (command == 'b') {
            const auto p1 = coordinate(i), p2 = coordinate(i + 2), p3 = coordinate(i + 4);
            if (!p1 || !p2 || !p3)
                return std::unexpected("has a path that is not drawing commands");
            constexpr int kPieces = 16;
            NodePoint last = pen;
            for (int k = 1; k <= kPieces; ++k) {
                const double t = double(k) / kPieces, u = 1 - t;
                const auto at = [&](double a, double b, double c, double d) {
                    return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d;
                };
                const NodePoint next{at(pen.x, p1->x, p2->x, p3->x), at(pen.y, p1->y, p2->y, p3->y)};
                segments.emplace_back(last, next);
                last = next;
            }
            pen = *p3;
            i += 6;
            continue;
        }
        const auto p = coordinate(i);
        if (!p || command == 0)
            return std::unexpected("has a path that is not drawing commands");
        if (command == 'l')
            segments.emplace_back(pen, *p);
        pen = *p;
        i += 2;
    }
    double total = 0;
    for (const auto &[a, b] : segments)
        total += std::hypot(b.x - a.x, b.y - a.y);
    if (!(total > 0))
        return std::unexpected("has a path with no length");
    std::vector<PathStop> stops;
    std::size_t at = 0;
    double walked = 0;
    for (int k = 0; k < count; ++k) {
        const double target = count > 1 ? total * k / (count - 1) : 0;
        while (at + 1 < segments.size() &&
               walked + std::hypot(segments[at].second.x - segments[at].first.x,
                                   segments[at].second.y - segments[at].first.y) < target) {
            walked += std::hypot(segments[at].second.x - segments[at].first.x,
                                 segments[at].second.y - segments[at].first.y);
            ++at;
        }
        const auto &[a, b] = segments[at];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        const double f = length > 0 ? std::clamp((target - walked) / length, 0.0, 1.0) : 0;
        const double angle = std::atan2(-(b.y - a.y), b.x - a.x) * 180 / std::numbers::pi;
        stops.push_back({{toThousandths(a.x + (b.x - a.x) * f), toThousandths(a.y + (b.y - a.y) * f)},
                         toThousandths(angle)});
    }
    return stops;
}

constexpr int kUnwired = -1;

struct CNode {
    const GraphNode *graph = nullptr;
    Kind kind = Kind::Number;
    NodePorts ports;
    struct Input {
        int node = kUnwired;
        int port = 0;
        NodeValue constant;
        bool hasField = false;
    };
    std::vector<Input> inputs;
    int firstSlot = 0;
    Level level = Level::Constant;
    std::string mode;
    bool flag = false;
    double number = 0;
    std::string text;
    std::vector<NodeValue> list;
    std::optional<double> start, end;
    std::map<std::string, int, std::less<>> inputIndex, outputIndex;

    int in(std::string_view name) const { return inputIndex.at(std::string(name)); }
};

struct Compiled {
    NodeGraph flat;
    std::vector<CNode> nodes;
    int slots = 0;
    std::vector<int> emits;
    std::vector<int> lua;
    std::vector<std::vector<int>> emitLua;
    bool chars = false, furi = false, audio = false;
    std::vector<std::string> emitSignature;
};

std::string upstreamSignature(const Compiled &c, int node, std::set<int> &seen)
{
    if (!seen.insert(node).second)
        return {};
    const CNode &n = c.nodes[node];
    std::string out = n.graph->id + '|' + n.graph->type + '|' + n.graph->fields.dump() + '|';
    if (n.kind == Kind::Lua)
        out += n.text + '|';
    for (const CNode::Input &in : n.inputs)
        if (in.node != kUnwired)
            out += c.nodes[in.node].graph->id + '.' + std::to_string(in.port) + ';';
    out += '\n';
    for (const CNode::Input &in : n.inputs)
        if (in.node != kUnwired)
            out += upstreamSignature(c, in.node, seen);
    return out;
}

Compiled compile(const NodeGraph &flat)
{
    Compiled c;
    c.flat = flat;
    std::map<std::string, int, std::less<>> index;
    for (const GraphNode &g : c.flat.nodes) {
        CNode n;
        n.graph = &g;
        const PluginNode *plugin = pluginNode(g.type);
        n.kind = plugin ? Kind::Lua : kKinds.at(g.type);
        n.ports = *nodePorts(g);
        for (std::size_t i = 0; i < n.ports.inputs.size(); ++i) {
            const PortSpec &p = n.ports.inputs[i];
            n.inputIndex[p.name] = static_cast<int>(i);
            CNode::Input in;
            const NodeSpec *spec = nodeSpec(g.type);
            in.hasField = spec && fieldSpec(*spec, p.name) && !nodeField(g, p.name).isNull();
            in.constant = constantOf(nodeField(g, p.name), p.type);
            n.inputs.push_back(std::move(in));
        }
        for (std::size_t i = 0; i < n.ports.outputs.size(); ++i)
            n.outputIndex[n.ports.outputs[i].name] = static_cast<int>(i);
        n.firstSlot = c.slots;
        c.slots += static_cast<int>(std::max<std::size_t>(1, n.ports.outputs.size()));
        const auto text = [&](std::string_view name) {
            const Json v = nodeField(g, name);
            return v.isString() ? v.asString() : std::string();
        };
        switch (n.kind) {
        case Kind::ForEach:
            n.mode = text("unit");
            n.flag = nodeField(g, "skip_blank").asBool();
            c.chars = c.chars || n.mode == "char";
            c.furi = c.furi || n.mode == "furi";
            break;
        case Kind::Frames:
        case Kind::AudioPeaks:
            c.audio = c.audio || n.kind == Kind::AudioPeaks;
            if (const Json v = nodeField(g, "start"); v.isNumber())
                n.start = v.asNumber();
            if (const Json v = nodeField(g, "end"); v.isNumber())
                n.end = v.asNumber();
            break;
        case Kind::Choose: {
            n.mode = text("type");
            const PortType type = n.ports.outputs.front().type;
            for (const Json &v : nodeField(g, "values").asArray())
                n.list.push_back(constantOf(v, type));
            break;
        }
        case Kind::Gradient:
        case Kind::ClipGradient:
            n.mode = text("direction");
            for (const Json &v : nodeField(g, "colours").asArray())
                n.list.push_back(colourOf(v));
            break;
        case Kind::Array:
            n.mode = text("shape");
            break;
        case Kind::Particles:
            n.mode = text("area");
            n.number = nodeField(g, "seed").asNumber();
            break;
        case Kind::Highlights:
            n.flag = nodeField(g, "skip_blank").asBool();
            break;
        case Kind::InlineFx:
            n.text = text("fx");
            break;
        case Kind::Calculate:
        case Kind::Compare:
            n.mode = text("op");
            break;
        case Kind::Progress:
            n.mode = text("ease");
            break;
        case Kind::Random:
            n.mode = text("scope");
            n.number = nodeField(g, "seed").asNumber();
            break;
        case Kind::Retime:
            n.mode = text("mode");
            break;
        case Kind::Stagger:
            n.mode = text("from");
            break;
        case Kind::Tag:
            n.mode = text("tag");
            break;
        case Kind::Move:
        case Kind::Transform:
            n.flag = nodeField(g, "timed").asBool();
            break;
        case Kind::Shape:
            n.text = text("shape");
            n.number = nodeField(g, "scale").asNumber();
            break;
        case Kind::Join:
            n.mode = text("unit_text");
            break;
        case Kind::Emit:
            n.mode = text("unit_text");
            n.number = nodeField(g, "order").asNumber();
            break;
        case Kind::Lua:
            n.text = plugin ? plugin->code : text("code");
            break;
        default:
            break;
        }
        index[g.id] = static_cast<int>(c.nodes.size());
        c.nodes.push_back(std::move(n));
    }
    for (const GraphWire &w : c.flat.wires) {
        CNode &to = c.nodes[index.at(w.to.node)];
        const int from = index.at(w.from.node);
        CNode::Input &in = to.inputs[to.in(w.to.port)];
        in.node = from;
        in.port = c.nodes[from].outputIndex.at(w.from.port);
    }

    std::vector<int> state(c.nodes.size(), 0);
    std::function<void(int)> level = [&](int i) {
        if (state[i])
            return;
        state[i] = 1;
        CNode &n = c.nodes[i];
        Level l = Level::Constant;
        for (const CNode::Input &in : n.inputs)
            if (in.node != kUnwired) {
                level(in.node);
                l = std::max(l, c.nodes[in.node].level);
            }
        switch (n.kind) {
        case Kind::ForEach:
        case Kind::Repeat:
        case Kind::Frames:
        case Kind::ClipGradient:
        case Kind::Array:
        case Kind::Particles:
        case Kind::Highlights:
        case Kind::InlineFx:
        case Kind::Info:
        case Kind::AudioPeaks:
        case Kind::Retime:
        case Kind::Lua:
            l = Level::Element;
            break;
        case Kind::Random:
            l = n.mode == "line" ? std::max(l, Level::Line) : Level::Element;
            break;
        case Kind::Join:
            l = Level::Line;
            break;
        default:
            break;
        }
        n.level = l;
    };
    for (std::size_t i = 0; i < c.nodes.size(); ++i)
        level(static_cast<int>(i));

    for (std::size_t i = 0; i < c.nodes.size(); ++i) {
        if (c.nodes[i].kind == Kind::Emit)
            c.emits.push_back(static_cast<int>(i));
        if (c.nodes[i].kind == Kind::Lua)
            c.lua.push_back(static_cast<int>(i));
    }
    std::stable_sort(c.emits.begin(), c.emits.end(),
                     [&](int a, int b) { return c.nodes[a].number < c.nodes[b].number; });
    for (const int e : c.emits) {
        std::set<int> seen;
        c.emitSignature.push_back(upstreamSignature(c, e, seen));
        std::vector<int> lua;
        std::set<int> visited;
        std::function<void(int)> collect = [&](int i) {
            if (!visited.insert(i).second)
                return;
            for (const CNode::Input &in : c.nodes[i].inputs)
                if (in.node != kUnwired)
                    collect(in.node);
            if (c.nodes[i].kind == Kind::Lua)
                lua.push_back(i);
        };
        collect(e);
        c.emitLua.push_back(std::move(lua));
    }
    return c;
}

struct Loop {
    int node = 0;
    int index = 1, count = 1;
    double start = 0, end = 0;
    NodePoint from, to;
    double value = 0;
};

struct Element {
    UnitKind kind = UnitKind::Syl;
    int syl = 0, ch = -1, furi = -1;
    int index = 1, count = 1;
    std::vector<Loop> loops;
    const Unit *timed = nullptr;
};

using Memo = std::vector<std::optional<NodeValue>>;

struct Problems {
    std::vector<NodeEffectProblem> &out;
    core::LineId line;

    void add(const std::string &node, std::string message)
    {
        NodeEffectProblem p{node, line, std::move(message)};
        if (std::ranges::find(out, p) == out.end())
            out.push_back(std::move(p));
    }
};

struct Generated {
    std::vector<int> key;
    core::LineRecord line;
};

class Evaluator {
public:
    Evaluator(const Compiled &c, const karaoke_layout::Line &line, std::u8string_view name, std::int64_t seed,
              const NodeEffectOptions &options, std::atomic<bool> &luaStopped, Problems &problems,
              NodeValueTrace *trace)
        : m_c(c), m_line(line), m_name(name), m_seed(seed), m_lua(options.lua), m_audio(options.audio),
          m_luaStopped(luaStopped), m_problems(problems), m_trace(trace),
          m_lineMemo(static_cast<std::size_t>(c.slots))
    {
    }

    bool emit(int emitRank, std::vector<Generated> &out);

private:
    const Unit &unitOf(const Element &e) const
    {
        if (e.timed)
            return *e.timed;
        switch (e.kind) {
        case UnitKind::Line:
            return m_line.line;
        case UnitKind::Char:
            return m_line.chars[e.syl][e.ch];
        case UnitKind::Furi:
            return m_line.furi[e.furi];
        case UnitKind::Syl:
            break;
        }
        return m_line.syls[e.syl];
    }

    std::vector<Element> expand(int node)
    {
        const CNode &n = m_c.nodes[node];
        std::vector<Element> out;
        if (n.kind == Kind::ForEach) {
            std::vector<Element> all;
            if (n.mode == "line") {
                all.push_back({UnitKind::Line, 0, -1, -1, 1, 1, {}});
            } else if (n.mode == "furi") {
                for (std::size_t f = 0; f < m_line.furi.size(); ++f)
                    if (!n.flag || !m_line.furi[f].blank)
                        all.push_back({UnitKind::Furi, m_line.furi[f].i > 0 ? int(m_line.furi[f].i) : 0, -1, int(f),
                                       1, 1, {}});
            } else {
                for (std::size_t s = 1; s < m_line.syls.size(); ++s) {
                    if (n.flag && m_line.syls[s].blank)
                        continue;
                    if (n.mode == "syl") {
                        all.push_back({UnitKind::Syl, int(s), -1, -1, 1, 1, {}});
                        continue;
                    }
                    for (std::size_t ch = 0; ch < m_line.chars[s].size(); ++ch)
                        if (!n.flag || !m_line.chars[s][ch].blank)
                            all.push_back({UnitKind::Char, int(s), int(ch), -1, 1, 1, {}});
                }
            }
            for (std::size_t i = 0; i < all.size(); ++i) {
                all[i].index = static_cast<int>(i) + 1;
                all[i].count = static_cast<int>(all.size());
            }
            return all;
        }
        const int parent = n.inputs[n.in("units")].node;
        for (const Element &p : expand(parent)) {
            if (n.kind == Kind::InlineFx) {
                if (inlineFxOf(p) == n.text)
                    out.push_back(p);
                continue;
            }
            if (n.kind == Kind::Highlights) {
                highlights(node, p, out);
                continue;
            }
            Memo memo(static_cast<std::size_t>(m_c.slots));
            if (n.kind == Kind::Repeat) {
                const double count = number(node, "count", p, memo);
                const int times = std::isfinite(count) ? std::max(0, static_cast<int>(count)) : 0;
                for (int j = 1; j <= times; ++j) {
                    Element e = p;
                    e.loops.push_back({node, j, times, 0, 0});
                    out.push_back(std::move(e));
                }
                continue;
            }
            if (n.kind == Kind::ClipGradient) {
                strips(node, p, memo, out);
                continue;
            }
            if (n.kind == Kind::Array) {
                copies(node, p, memo, out);
                continue;
            }
            if (n.kind == Kind::Particles) {
                particles(node, p, memo, out);
                continue;
            }
            const Unit &u = unitOf(p);
            const double lineStart = static_cast<double>(m_line.macro.startMs);
            const bool wiredStart = n.inputs[n.in("start")].node != kUnwired;
            const bool wiredEnd = n.inputs[n.in("end")].node != kUnwired;
            const double start = wiredStart || n.start ? number(node, "start", p, memo) : lineStart + u.relStart;
            const double end = wiredEnd || n.end ? number(node, "end", p, memo) : lineStart + u.relEnd;
            const double step = number(node, "step", p, memo);
            if (!(step >= 1) || !std::isfinite(start) || !std::isfinite(end)) {
                m_problems.add(n.graph->id, "has a step below 1 ms or a time that is not a number");
                continue;
            }
            const int count = end > start ? static_cast<int>(std::ceil((end - start) / step)) : 0;
            for (int k = 0; k < count; ++k) {
                Element e = p;
                e.loops.push_back({node, k + 1, count, start + k * step, std::min(start + (k + 1) * step, end)});
                out.push_back(std::move(e));
            }
        }
        return out;
    }

    Loop loopAt(int node, int index, int count) const
    {
        Loop l;
        l.node = node;
        l.index = index;
        l.count = count;
        return l;
    }

    const std::string &inlineFxOf(const Element &e) const
    {
        static const std::string kNone;
        if (e.kind == UnitKind::Furi)
            return static_cast<std::size_t>(e.furi) < m_line.karaFuri.size() ? m_line.karaFuri[e.furi].inlineFx : kNone;
        if (e.kind == UnitKind::Line || static_cast<std::size_t>(e.syl) >= m_line.karaSyllables.size())
            return kNone;
        return m_line.karaSyllables[e.syl].inlineFx;
    }

    void highlights(int node, const Element &p, std::vector<Element> &out)
    {
        const CNode &n = m_c.nodes[node];
        const Unit &u = unitOf(p);
        const bool syllable = p.kind == UnitKind::Syl || p.kind == UnitKind::Char;
        if (!syllable || static_cast<std::size_t>(p.syl) >= m_line.karaSyllables.size()) {
            if (n.flag && u.duration <= 0)
                return;
            Element e = p;
            e.loops.push_back(loopAt(node, 1, 1));
            out.push_back(std::move(e));
            return;
        }
        const std::vector<karaoke_layout::Highlight> &all = m_line.karaSyllables[p.syl].highlights;
        const int count = static_cast<int>(all.size());
        for (int k = 0; k < count; ++k) {
            const karaoke_layout::Highlight &h = all[k];
            if (n.flag && h.duration <= 0)
                continue;
            Unit &t = m_timed.emplace_back(u);
            t.start = t.relStart = h.start;
            t.end = t.relEnd = h.end;
            t.duration = h.duration;
            t.kdur = h.duration / 10;
            t.mid = h.start + h.duration / 2;
            t.blank = u.blank || h.duration <= 0;
            Element e = p;
            e.timed = &t;
            e.loops.push_back(loopAt(node, k + 1, count));
            out.push_back(std::move(e));
        }
    }

    void strips(int node, const Element &p, Memo &memo, std::vector<Element> &out)
    {
        const CNode &n = m_c.nodes[node];
        const double strips = number(node, "strips", p, memo), pad = number(node, "padding", p, memo);
        if (!(strips >= 1) || !std::isfinite(strips) || !std::isfinite(pad)) {
            m_problems.add(n.graph->id, "has fewer than 1 strip or a padding that is not a number");
            return;
        }
        const Unit &u = unitOf(p);
        const double left = std::floor(u.left - pad), right = std::ceil(u.right + pad);
        const double top = std::floor(u.top - pad), bottom = std::ceil(u.bottom + pad);
        const bool across = n.mode != "down";
        const double lo = across ? left : top, hi = across ? right : bottom;
        const int count = static_cast<int>(strips);
        const auto edge = [&](int k) { return std::floor(lo + (hi - lo) * k / count + 0.5); };
        for (int k = 0; k < count; ++k) {
            Element e = p;
            Loop l = loopAt(node, k + 1, count);
            l.from = across ? NodePoint{edge(k), top} : NodePoint{left, edge(k)};
            l.to = across ? NodePoint{edge(k + 1), bottom} : NodePoint{right, edge(k + 1)};
            l.value = count > 1 ? double(k) / double(count - 1) : 0.0;
            e.loops.push_back(l);
            out.push_back(std::move(e));
        }
    }

    void copies(int node, const Element &p, Memo &memo, std::vector<Element> &out)
    {
        const CNode &n = m_c.nodes[node];
        const auto place = [&](int index, int count, NodePoint offset, double angle) {
            Element e = p;
            Loop l = loopAt(node, index, count);
            l.from = offset;
            l.value = angle;
            e.loops.push_back(l);
            out.push_back(std::move(e));
        };
        if (n.mode == "grid") {
            const int columns = static_cast<int>(nodeField(*n.graph, "columns").asNumber());
            const int rows = static_cast<int>(nodeField(*n.graph, "rows").asNumber());
            const Json spacing = nodeField(*n.graph, "spacing");
            const double dx = spacing.asArray()[0].asNumber(), dy = spacing.asArray()[1].asNumber();
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < columns; ++c)
                    place(r * columns + c + 1, rows * columns,
                          {toThousandths((c - (columns - 1) / 2.0) * dx), toThousandths((r - (rows - 1) / 2.0) * dy)},
                          0);
            return;
        }
        const double wanted = number(node, "count", p, memo);
        if (!(wanted >= 1) || !std::isfinite(wanted)) {
            m_problems.add(n.graph->id, "has fewer than 1 copy");
            return;
        }
        const int count = static_cast<int>(wanted);
        if (n.mode == "circle") {
            const double radius = number(node, "radius", p, memo);
            const double first = nodeField(*n.graph, "start_angle").asNumber();
            for (int k = 0; k < count; ++k) {
                const double degrees = first + 360.0 * k / count;
                const double rad = degrees * std::numbers::pi / 180;
                place(k + 1, count, {toThousandths(radius * std::cos(rad)), toThousandths(-radius * std::sin(rad))},
                      toThousandths(degrees));
            }
            return;
        }
        const auto stops = pathStops(get<NodeDrawing>(node, "path", p, memo), count);
        if (!stops) {
            m_problems.add(n.graph->id, stops.error());
            return;
        }
        for (int k = 0; k < count; ++k)
            place(k + 1, count, (*stops)[static_cast<std::size_t>(k)].at, (*stops)[static_cast<std::size_t>(k)].angle);
    }

    void particles(int node, const Element &p, Memo &memo, std::vector<Element> &out)
    {
        const CNode &n = m_c.nodes[node];
        const double wanted = number(node, "count", p, memo);
        const double width = number(node, "width", p, memo), height = number(node, "height", p, memo);
        if (!(wanted >= 0) || !std::isfinite(wanted) || !std::isfinite(width) || !std::isfinite(height)) {
            m_problems.add(n.graph->id, "has a count, width or height that is not a number");
            return;
        }
        const int count = static_cast<int>(wanted);
        const std::uint64_t base = seeded(n, p, "element");
        for (int k = 0; k < count; ++k) {
            const std::uint64_t h = mix(base, static_cast<std::uint64_t>(k + 1));
            const double a = unitDraw(mix(h, 1)), b = unitDraw(mix(h, 2));
            NodePoint at{(a - 0.5) * width, (b - 0.5) * height};
            if (n.mode == "ellipse") {
                const double r = std::sqrt(a), turn = 2 * std::numbers::pi * b;
                at = {width / 2 * r * std::cos(turn), height / 2 * r * std::sin(turn)};
            }
            Element e = p;
            Loop l = loopAt(node, k + 1, count);
            l.from = {toThousandths(at.x), toThousandths(at.y)};
            l.value = unitDraw(mix(h, 3));
            e.loops.push_back(l);
            out.push_back(std::move(e));
        }
    }

    std::uint64_t seeded(const CNode &n, const Element &e, std::string_view scope) const
    {
        std::uint64_t h = mix(static_cast<std::uint64_t>(m_seed), static_cast<std::uint64_t>(n.number));
        h = mix(h, hashText(n.graph->id));
        h = mix(h, static_cast<std::uint64_t>(m_line.macro.startMs));
        h = mix(h, hashText(m_line.macro.text));
        if (scope != "line") {
            h = mix(h, static_cast<std::uint64_t>(e.kind));
            h = mix(h, static_cast<std::uint64_t>(e.syl + 1));
            h = mix(h, static_cast<std::uint64_t>(e.ch + 1));
            h = mix(h, static_cast<std::uint64_t>(e.furi + 1));
        }
        if (scope == "element")
            for (const Loop &l : e.loops)
                h = mix(mix(h, static_cast<std::uint64_t>(l.node)), static_cast<std::uint64_t>(l.index));
        return h;
    }

    NodeValue loopOutput(const CNode &n, const Loop &loop, std::string_view name, const Element &e) const
    {
        if (name == "index")
            return double(loop.index);
        if (name == "count")
            return double(loop.count);
        switch (n.kind) {
        case Kind::ClipGradient:
            if (name == "progress")
                return loop.value;
            if (name == "clip")
                return NodeTags{"\\clip(" + numberText(loop.from.x) + "," + numberText(loop.from.y) + "," +
                                numberText(loop.to.x) + "," + numberText(loop.to.y) + ")"};
            if (name == "colour")
                return gradientAt(n.list, loop.value);
            return name == "from" ? loop.from : loop.to;
        case Kind::Array:
        case Kind::Particles: {
            if (name == "offset")
                return loop.from;
            if (name == "position") {
                const Unit &u = unitOf(e);
                return NodePoint{u.x + loop.from.x, u.y + loop.from.y};
            }
            return loop.value;
        }
        default:
            break;
        }
        if (name == "start")
            return loop.start;
        if (name == "end")
            return loop.end;
        return loop.count > 1 ? double(loop.index - 1) / double(loop.count - 1) : 0.0;
    }

    const Loop *loopOf(const Element &e, int node) const
    {
        for (const Loop &l : e.loops)
            if (l.node == node)
                return &l;
        return nullptr;
    }

    NodeValue input(int node, std::string_view port, const Element &e, Memo &memo)
    {
        const CNode::Input &in = m_c.nodes[node].inputs[m_c.nodes[node].in(port)];
        if (in.node == kUnwired)
            return in.constant;
        return value(in.node, in.port, e, memo);
    }

    bool wired(int node, std::string_view port) const
    {
        return m_c.nodes[node].inputs[m_c.nodes[node].in(port)].node != kUnwired;
    }

    double number(int node, std::string_view port, const Element &e, Memo &memo)
    {
        const NodeValue v = input(node, port, e, memo);
        if (const double *d = std::get_if<double>(&v))
            return *d;
        if (const bool *b = std::get_if<bool>(&v))
            return *b ? 1 : 0;
        return 0;
    }

    template <class T> T get(int node, std::string_view port, const Element &e, Memo &memo)
    {
        NodeValue v = input(node, port, e, memo);
        if (T *t = std::get_if<T>(&v))
            return std::move(*t);
        return T{};
    }

    NodeValue value(int node, int port, const Element &e, Memo &memo)
    {
        const CNode &n = m_c.nodes[node];
        const std::size_t slot = static_cast<std::size_t>(n.firstSlot + port);
        Memo &cache = n.level == Level::Element ? memo : m_lineMemo;
        if (cache[slot])
            return *cache[slot];
        NodeValue v = compute(node, port, e, memo);
        if (n.kind != Kind::Info)
            cache[slot] = v;
        if (m_trace && port < static_cast<int>(n.ports.outputs.size()))
            m_trace->values[n.graph->id].try_emplace(n.ports.outputs[static_cast<std::size_t>(port)].name, v);
        return v;
    }

    NodeValue compute(int node, int port, const Element &e, Memo &memo);
    NodeValue info(std::string_view port, const Element &e) const;
    NodeValue join(int node, const Element &e);
    NodeValue lua(int node, int port, const Element &e, Memo &memo);
    NodeValue audioPeak(int node, int port, const Element &e, Memo &memo);
    bool luaBatch(int node, const std::vector<Element> &elements, std::vector<Memo> &memos);

    const Compiled &m_c;
    const karaoke_layout::Line &m_line;
    std::u8string_view m_name;
    std::int64_t m_seed;
    LuaNodePort *m_lua;
    std::optional<const DisplayAudio *> m_audio;
    std::atomic<bool> &m_luaStopped;
    Problems &m_problems;
    NodeValueTrace *m_trace;
    Memo m_lineMemo;
    std::deque<Unit> m_timed;
};

NodeValue Evaluator::info(std::string_view port, const Element &e) const
{
    const Unit &u = unitOf(e);
    const Unit &l = m_line.line;
    const karaoke_layout::StyleInfo &s = *m_line.style;
    static const std::map<std::string_view, std::function<NodeValue(const Unit &, const Unit &,
                                                                             const karaoke_layout::StyleInfo &,
                                                                             const karaoke_layout::Line &)>>
        kFields{
            {"text", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.text); }},
            {"i", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.i); }},
            {"n", [](auto &, auto &, auto &, auto &line) { return NodeValue(double(line.syln)); }},
            {"start", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.start); }},
            {"end", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.end); }},
            {"duration", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.duration); }},
            {"kdur", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.kdur); }},
            {"mid", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.mid); }},
            {"left", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.left); }},
            {"center", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.center); }},
            {"right", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.right); }},
            {"width", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.width); }},
            {"top", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.top); }},
            {"middle", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.middle); }},
            {"bottom", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.bottom); }},
            {"height", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.height); }},
            {"x", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.x); }},
            {"y", [](auto &u, auto &, auto &, auto &) { return NodeValue(u.y); }},
            {"position", [](auto &u, auto &, auto &, auto &) { return NodeValue(NodePoint{u.x, u.y}); }},
            {"layer", [](auto &, auto &, auto &, auto &line) { return NodeValue(double(line.macro.layer)); }},
            {"line_index", [](auto &, auto &, auto &, auto &line) { return NodeValue(line.li); }},
            {"line_start", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.start); }},
            {"line_end", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.end); }},
            {"line_duration", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.duration); }},
            {"line_left", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.left); }},
            {"line_center", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.center); }},
            {"line_right", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.right); }},
            {"line_width", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.width); }},
            {"line_top", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.top); }},
            {"line_middle", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.middle); }},
            {"line_bottom", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.bottom); }},
            {"line_height", [](auto &, auto &l, auto &, auto &) { return NodeValue(l.height); }},
            {"style_fontsize", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.fontsize); }},
            {"style_scale_x", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.scaleX); }},
            {"style_scale_y", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.scaleY); }},
            {"style_spacing", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.spacing); }},
            {"style_angle", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.angle); }},
            {"style_outline", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.outline); }},
            {"style_shadow", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.shadow); }},
            {"style_alignment", [](auto &, auto &, auto &s, auto &) { return NodeValue(double(s.align)); }},
            {"style_primary", [](auto &, auto &, auto &s, auto &) { return NodeValue(nodeColour(s.primary)); }},
            {"style_secondary", [](auto &, auto &, auto &s, auto &) { return NodeValue(nodeColour(s.secondary)); }},
            {"style_outline_colour",
             [](auto &, auto &, auto &s, auto &) { return NodeValue(nodeColour(s.outlineColour)); }},
            {"style_back", [](auto &, auto &, auto &s, auto &) { return NodeValue(nodeColour(s.back)); }},
        };
    return kFields.at(port)(u, l, s, m_line);
}

NodeValue Evaluator::join(int node, const Element &)
{
    const CNode &n = m_c.nodes[node];
    std::string out;
    for (const Element &sub : expand(n.inputs[n.in("units")].node)) {
        Memo memo(static_cast<std::size_t>(m_c.slots));
        const std::string tags = get<NodeTags>(node, "tags", sub, memo).text;
        if (!tags.empty())
            out += "{" + tags + "}";
        if (wired(node, "text"))
            out += get<std::string>(node, "text", sub, memo);
        else if (n.mode != "none")
            out += n.mode == "raw" ? unitOf(sub).raw : unitOf(sub).text;
    }
    return out;
}

double calculated(std::string_view op, double a, double b)
{
    if (op == "add")
        return a + b;
    if (op == "subtract")
        return a - b;
    if (op == "multiply")
        return a * b;
    if (op == "divide")
        return a / b;
    if (op == "modulo")
        return a - std::floor(a / b) * b;
    if (op == "power")
        return std::pow(a, b);
    if (op == "minimum")
        return std::min(a, b);
    if (op == "maximum")
        return std::max(a, b);
    if (op == "floor")
        return std::floor(a);
    if (op == "ceil")
        return std::ceil(a);
    if (op == "round")
        return std::floor(a + 0.5);
    if (op == "absolute")
        return std::fabs(a);
    return -a;
}

double eased(double t, std::string_view ease)
{
    t = std::clamp(t, 0.0, 1.0);
    if (ease == "in")
        return t * t;
    if (ease == "out")
        return 1 - (1 - t) * (1 - t);
    if (ease == "in_out")
        return t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
    return t;
}

NodeValue Evaluator::compute(int node, int port, const Element &e, Memo &memo)
{
    const CNode &n = m_c.nodes[node];
    const auto num = [&](std::string_view name) { return number(node, name, e, memo); };
    const auto before = [&]() { return get<NodeTags>(node, "before", e, memo).text; };
    switch (n.kind) {
    case Kind::ForEach:
        return double(port == 1 ? e.index : e.count);
    case Kind::Repeat:
    case Kind::Frames:
    case Kind::ClipGradient:
    case Kind::Array:
    case Kind::Particles:
    case Kind::Highlights: {
        const Loop *loop = loopOf(e, node);
        if (!loop) {
            m_problems.add(n.graph->id, "was read outside its units");
            return constantOf(Json(), n.ports.outputs[port].type);
        }
        const std::string &name = n.ports.outputs[port].name;
        if (n.kind == Kind::Repeat)
            return double(name == "j" ? loop->index : loop->count);
        return loopOutput(n, *loop, name, e);
    }
    case Kind::Info:
        return info(n.ports.outputs[port].name, e);
    case Kind::Number:
    case Kind::Text:
    case Kind::Colour:
        return constantOf(nodeField(*n.graph, "value"), n.ports.outputs.front().type);
    case Kind::Point:
        return NodePoint{num("x"), num("y")};
    case Kind::Choose: {
        const double index = num("index");
        if (!std::isfinite(index) || n.list.empty()) {
            m_problems.add(n.graph->id, "has no value at that place");
            return n.list.empty() ? constantOf(Json(), n.ports.outputs.front().type) : n.list.front();
        }
        const auto count = static_cast<std::int64_t>(n.list.size());
        const auto k = static_cast<std::int64_t>(std::floor(index)) - 1;
        return n.list[static_cast<std::size_t>(((k % count) + count) % count)];
    }
    case Kind::Calculate:
        return calculated(n.mode, num("a"), num("b"));
    case Kind::Compare: {
        const double a = num("a"), b = num("b");
        const std::string &op = n.mode;
        return op == "less"               ? a < b
               : op == "less_or_equal"    ? a <= b
               : op == "equal"            ? a == b
               : op == "not_equal"        ? a != b
               : op == "greater_or_equal" ? a >= b
                                          : a > b;
    }
    case Kind::Progress: {
        const double index = num("index"), count = num("count");
        return eased(count > 1 ? (index - 1) / (count - 1) : 0.0, n.mode);
    }
    case Kind::Random: {
        const double lo = num("min"), hi = num("max"), step = num("step");
        const double u = unitDraw(seeded(n, e, n.mode));
        if (step > 0) {
            const double steps = std::floor((hi - lo) / step);
            return lo + std::floor(u * (steps + 1)) * step;
        }
        return lo + u * (hi - lo);
    }
    case Kind::Retime: {
        const Unit &u = unitOf(e);
        const double ls = static_cast<double>(m_line.macro.startMs), le = static_cast<double>(m_line.macro.endMs);
        const double a = num("start_offset"), b = num("end_offset");
        const double ss = u.relStart, se = u.relEnd, dur = u.relEnd - u.relStart;
        const std::string &m = n.mode;
        double s = ls, t = le;
        if (m == "syl")
            s = ls + ss + a, t = ls + se + b;
        else if (m == "presyl")
            s = ls + ss + a, t = ls + ss + b;
        else if (m == "postsyl")
            s = ls + se + a, t = ls + se + b;
        else if (m == "line")
            s = ls + a, t = le + b;
        else if (m == "preline")
            s = ls + a, t = ls + b;
        else if (m == "postline")
            s = le + a, t = le + b;
        else if (m == "start2syl")
            s = ls + a, t = ls + ss + b;
        else if (m == "syl2end")
            s = ls + se + a, t = le + b;
        else if (m == "set")
            s = a, t = b;
        else if (m == "sylpct")
            s = ls + ss + a * dur / 100, t = ls + ss + b * dur / 100;
        return port == 0 ? s : t;
    }
    case Kind::Stagger: {
        const double index = num("index"), count = num("count"), step = num("step");
        if (n.mode == "end")
            return (count - index) * step;
        if (n.mode == "centre")
            return std::fabs(index - (count + 1) / 2) * step;
        return (index - 1) * step;
    }
    case Kind::Tag: {
        const std::string &tag = n.mode;
        std::string out = before();
        if (tag == "raw")
            return NodeTags{out + get<std::string>(node, "text", e, memo)};
        if (tag == "fn" || tag == "r")
            return NodeTags{out + "\\" + tag + get<std::string>(node, "text", e, memo)};
        if (tag == "pos" || tag == "org") {
            const NodePoint p = get<NodePoint>(node, "point", e, memo);
            return NodeTags{out + "\\" + tag + "(" + numberText(p.x) + "," + numberText(p.y) + ")"};
        }
        if (tag == "clip" || tag == "iclip") {
            if (wired(node, "drawing")) {
                const NodeDrawing d = get<NodeDrawing>(node, "drawing", e, memo);
                const std::string scale = d.scale != 1 ? std::to_string(d.scale) + "," : std::string();
                return NodeTags{out + "\\" + tag + "(" + scale + d.commands + ")"};
            }
            const NodePoint a = get<NodePoint>(node, "point", e, memo), b = get<NodePoint>(node, "point2", e, memo);
            return NodeTags{out + "\\" + tag + "(" + numberText(a.x) + "," + numberText(a.y) + "," +
                            numberText(b.x) + "," + numberText(b.y) + ")"};
        }
        if (tag.size() == 2 && tag[1] == 'c')
            return NodeTags{out + "\\" + tag + assColour(get<NodeColour>(node, "colour", e, memo))};
        if (tag == "alpha" || (tag.size() == 2 && tag[1] == 'a'))
            return NodeTags{out + "\\" + tag + assAlpha(num("alpha"))};
        return NodeTags{out + "\\" + tag + numberText(num("value"))};
    }
    case Kind::Move: {
        const NodePoint a = get<NodePoint>(node, "from", e, memo), b = get<NodePoint>(node, "to", e, memo);
        std::string out = before() + "\\move(" + numberText(a.x) + "," + numberText(a.y) + "," + numberText(b.x) +
                          "," + numberText(b.y);
        if (n.flag)
            out += "," + numberText(num("t1")) + "," + numberText(num("t2"));
        return NodeTags{out + ")"};
    }
    case Kind::Fade:
        return NodeTags{before() + "\\fad(" + numberText(num("fade_in")) + "," + numberText(num("fade_out")) + ")"};
    case Kind::Transform: {
        std::string out = before() + "\\t(";
        if (n.flag)
            out += numberText(num("t1")) + "," + numberText(num("t2")) + ",";
        if (const double accel = num("accel"); accel != 1)
            out += numberText(accel) + ",";
        return NodeTags{out + get<NodeTags>(node, "tags", e, memo).text + ")"};
    }
    case Kind::Gradient:
        return gradientAt(n.list, num("progress"));
    case Kind::Shape:
        return NodeDrawing{n.text, static_cast<int>(n.number)};
    case Kind::AudioPeaks:
        return audioPeak(node, port, e, memo);
    case Kind::Join:
        return join(node, e);
    case Kind::Lua:
        return lua(node, port, e, memo);
    case Kind::Emit:
        break;
    }
    return 0.0;
}

NodeValue Evaluator::audioPeak(int node, int port, const Element &e, Memo &memo)
{
    const CNode &n = m_c.nodes[node];
    if (!m_audio) {
        m_problems.add(n.graph->id, "has no audio to read, so it gives 0");
        return 0.0;
    }
    if (!*m_audio || (*m_audio)->sampleRate() <= 0) {
        m_problems.add(n.graph->id, "waits for the audio to open, so it gives 0");
        return 0.0;
    }
    const Unit &u = unitOf(e);
    const double lineStart = static_cast<double>(m_line.macro.startMs);
    const double start = wired(node, "start") || n.start ? number(node, "start", e, memo) : lineStart + u.relStart;
    const double end = wired(node, "end") || n.end ? number(node, "end", e, memo) : lineStart + u.relEnd;
    if (!std::isfinite(start) || !std::isfinite(end) || end <= start)
        return 0.0;
    const auto peaks = audioPeaks(**m_audio, static_cast<std::int64_t>(std::max(0.0, start)),
                                  static_cast<std::int64_t>(std::max(1.0, end)), 1);
    if (!peaks) {
        m_problems.add(n.graph->id, "reads a span too long while the audio is still loading, so it gives 0");
        return 0.0;
    }
    const double low = peaks->min.front() / 32768.0, high = peaks->max.front() / 32768.0;
    return port == 0 ? std::max(std::fabs(low), std::fabs(high)) : port == 1 ? low : high;
}

NodeValue Evaluator::lua(int node, int port, const Element &e, Memo &memo)
{
    std::vector<Element> one{e};
    std::vector<Memo> memos{memo};
    if (!luaBatch(node, one, memos))
        return constantOf(Json(), m_c.nodes[node].ports.outputs[port].type);
    memo = std::move(memos.front());
    return *memo[static_cast<std::size_t>(m_c.nodes[node].firstSlot + port)];
}

bool Evaluator::luaBatch(int node, const std::vector<Element> &elements, std::vector<Memo> &memos)
{
    const CNode &n = m_c.nodes[node];
    if (!m_lua) {
        m_problems.add(n.graph->id, "has no Lua helper to run it");
        return false;
    }
    LuaNodePort::Call call{n.graph->id, n.text, n.ports.inputs, n.ports.outputs, {}};
    for (std::size_t i = 0; i < elements.size(); ++i) {
        std::vector<NodeValue> row;
        for (const PortSpec &p : n.ports.inputs)
            row.push_back(input(node, p.name, elements[i], memos[i]));
        call.rows.push_back(std::move(row));
    }
    auto result = m_lua->run(call);
    if (!result) {
        m_problems.add(n.graph->id, result.error().message);
        if (result.error().stopsRun)
            m_luaStopped = true;
        return false;
    }
    if (result->size() != elements.size()) {
        m_problems.add(n.graph->id, "gave a wrong number of rows");
        return false;
    }
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if ((*result)[i].size() != n.ports.outputs.size()) {
            m_problems.add(n.graph->id, "gave a wrong number of outputs");
            return false;
        }
        for (std::size_t k = 0; k < n.ports.outputs.size(); ++k)
            memos[i][static_cast<std::size_t>(n.firstSlot) + k] = (*result)[i][k];
    }
    return true;
}

bool Evaluator::emit(int emitRank, std::vector<Generated> &out)
{
    const int node = m_c.emits[emitRank];
    const CNode &n = m_c.nodes[node];
    const std::vector<Element> elements = expand(n.inputs[n.in("units")].node);
    std::vector<Memo> memos(elements.size(), Memo(static_cast<std::size_t>(m_c.slots)));
    for (const int lua : m_c.emitLua[emitRank])
        if (!elements.empty() && !luaBatch(lua, elements, memos))
            return false;
    const std::u8string effect = generatedEffect(m_name);
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const Element &e = elements[i];
        Memo &memo = memos[i];
        if (!std::get<bool>(input(node, "when", e, memo)))
            continue;
        const double start = wired(node, "start") ? number(node, "start", e, memo) : double(m_line.macro.startMs);
        const double end = wired(node, "end") ? number(node, "end", e, memo) : double(m_line.macro.endMs);
        const double layer = number(node, "layer", e, memo);
        if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(layer)) {
            m_problems.add(n.graph->id, "got a time or Layer that is not a number");
            continue;
        }
        std::string tags = get<NodeTags>(node, "tags", e, memo).text;
        std::string text;
        if (wired(node, "text")) {
            text = get<std::string>(node, "text", e, memo);
        } else if (wired(node, "drawing")) {
            const NodeDrawing d = get<NodeDrawing>(node, "drawing", e, memo);
            tags += "\\p" + std::to_string(d.scale);
            text = d.commands;
        } else if (n.mode != "none") {
            text = n.mode == "raw" ? unitOf(e).raw : unitOf(e).text;
        }
        MacroDialogueLine d = m_line.macro;
        d.id = 0;
        d.generatedFrom = m_line.macro.id;
        d.comment = false;
        d.layer = static_cast<int>(layer);
        d.startMs = static_cast<std::int64_t>(start);
        d.endMs = static_cast<std::int64_t>(end);
        d.style = narrow(unitOf(e).style);
        d.effect = narrow(effect);
        d.text = (tags.empty() ? std::string() : "{" + tags + "}") + text;
        d.translation.clear();
        std::vector<int> key;
        key.push_back(e.kind == UnitKind::Line ? 0 : e.kind == UnitKind::Furi ? 2 : 1);
        key.push_back(e.kind == UnitKind::Furi ? e.furi : e.syl);
        key.push_back(emitRank);
        key.push_back(e.ch);
        for (const Loop &l : e.loops)
            key.push_back(l.index);
        out.push_back({std::move(key), lineOfMacro(d)});
    }
    return true;
}

struct LineResult {
    std::vector<Generated> lines;
    std::vector<NodeEffectProblem> problems;
};

std::string audioState(const std::optional<const DisplayAudio *> &audio)
{
    if (!audio)
        return "none";
    if (!*audio)
        return "opening";
    const DisplayAudio &a = **audio;
    return std::to_string(reinterpret_cast<std::uintptr_t>(&a)) + ' ' + std::to_string(a.sampleRate()) + ' ' +
           std::to_string(a.sampleCount()) + ' ' + std::to_string(a.decoded()) + (a.finished() ? " done" : "");
}

std::string cacheKey(const Compiled &c, int emitRank, const karaoke_layout::Line &line, std::u8string_view name,
                     const std::string &audio)
{
    return narrow(name) + '\x1e' + std::to_string(c.flat.seed) + '\x1e' + c.emitSignature[emitRank] + '\x1e' +
           line.signature + '\x1e' + audio;
}

}

NodeEffectRun runNodeEffect(const core::Document &document, std::u8string_view name, std::u8string_view style,
                            const NodeGraph &graph, const NodeEffectOptions &options)
{
    const auto began = std::chrono::steady_clock::now();
    NodeEffectRun run;
    run.name = std::u8string(name);
    run.complete = !options.sources.has_value();
    const auto finish = [&]() -> NodeEffectRun {
        run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        return std::move(run);
    };
    if (!isGraphName(name)) {
        run.problems.push_back({{}, std::nullopt, "\"" + narrow(name) + "\" cannot name a graph"});
        return finish();
    }
    for (const GraphProblem &p : validateGraph(graph))
        run.problems.push_back({p.node, std::nullopt, p.port.empty() ? p.message : p.port + " " + p.message});
    if (!options.measure)
        run.problems.push_back({{}, std::nullopt, "no text measurement is available"});
    if (!run.problems.empty())
        return finish();
    const Compiled compiled = compile(*flattenGraph(graph));
    if (!options.trustLua) {
        for (const int lua : compiled.lua)
            if (!pluginOfNodeType(compiled.nodes[lua].graph->type))
                run.problems.push_back({compiled.nodes[lua].graph->id, std::nullopt,
                                        "runs only once you trust this Document's Lua"});
        if (!run.problems.empty())
            return finish();
    }
    run.ran = true;

    karaoke_layout::ExtentsCache extents(*options.measure);
    karaoke_layout::Layout layout(document, extents, options.video);
    std::size_t info = 0, styles = 0;
    for (const auto &section : document.sections())
        for (const auto &record : section.records) {
            if (section.kind == core::SectionKind::ScriptInfo && std::holds_alternative<core::PropertyRecord>(record))
                ++info;
            if (section.kind == core::SectionKind::Styles && std::holds_alternative<core::StyleRecord>(record))
                ++styles;
        }
    double li = static_cast<double>(info + styles + layout.furiganaStylesAdded());
    std::vector<karaoke_layout::Line> lines;
    std::vector<std::vector<std::string>> layoutProblems;
    for (const core::LineRecord *l : document.lines()) {
        if (isGeneratedBy(*l, name))
            continue;
        li += 1;
        if (!isKaraokeLine(*l) || (!graph.allStyles && l->style != style))
            continue;
        if (options.sources && !options.sources->contains(l->id))
            continue;
        std::vector<std::string> problems;
        lines.push_back(layout.line(macroLineOf(*l), li, {.chars = compiled.chars}, problems));
        layoutProblems.push_back(std::move(problems));
        run.sources.push_back(l->id);
    }

    std::vector<LineResult> results(lines.size());
    std::vector<std::atomic<std::int64_t>> nanos(compiled.emits.size());
    std::vector<std::atomic<std::size_t>> counts(compiled.emits.size());
    std::atomic<bool> luaStopped = false;
    const std::string audio = compiled.audio ? audioState(options.audio) : std::string();
    const auto work = [&](std::size_t i) {
        if (luaStopped)
            return;
        LineResult &r = results[i];
        Problems problems{r.problems, core::LineId{lines[i].macro.id}};
        for (const std::string &p : layoutProblems[i])
            problems.add({}, p);
        Evaluator eval(compiled, lines[i], name, graph.seed, options, luaStopped, problems,
                       i == 0 ? options.trace : nullptr);
        for (std::size_t k = 0; k < compiled.emits.size() && !luaStopped; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<Generated> out;
            const std::string key =
                options.cache ? cacheKey(compiled, static_cast<int>(k), lines[i], name, audio) : "";
            bool cached = false;
            if (options.cache) {
                auto &store = options.cache->store();
                std::scoped_lock lock(store.mutex);
                if (const auto it = store.lines.find(key); it != store.lines.end()) {
                    for (const auto &[order, line] : it->second)
                        out.push_back({order, line});
                    cached = true;
                }
            }
            if (!cached) {
                const std::size_t before = r.problems.size();
                eval.emit(static_cast<int>(k), out);
                if (options.cache && r.problems.size() == before) {
                    auto &store = options.cache->store();
                    std::scoped_lock lock(store.mutex);
                    auto &entry = store.lines[key];
                    for (const Generated &g : out)
                        entry.emplace_back(g.key, g.line);
                }
            }
            for (Generated &g : out)
                g.line.generatedFrom = core::LineId{lines[i].macro.id};
            counts[k] += out.size();
            nanos[k] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0)
                            .count();
            for (Generated &g : out)
                r.lines.push_back(std::move(g));
        }
        std::stable_sort(r.lines.begin(), r.lines.end(),
                         [](const Generated &a, const Generated &b) { return a.key < b.key; });
    };

    unsigned threads = options.threads ? options.threads : std::max(1u, std::thread::hardware_concurrency());
    if (!compiled.lua.empty())
        threads = 1;
    threads = static_cast<unsigned>(std::min<std::size_t>(threads, lines.size()));
    if (threads <= 1) {
        for (std::size_t i = 0; i < lines.size(); ++i)
            work(i);
    } else {
        std::atomic<std::size_t> next = 0;
        std::vector<std::jthread> pool;
        for (unsigned t = 0; t < threads; ++t)
            pool.emplace_back([&] {
                for (std::size_t i; (i = next++) < lines.size();)
                    work(i);
            });
    }

    std::set<std::u8string> missing;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        for (NodeEffectProblem &p : results[i].problems)
            if (std::ranges::find(run.problems, p) == run.problems.end())
                run.problems.push_back(std::move(p));
        if (!results[i].lines.empty())
            run.applied.insert(core::LineId{lines[i].macro.id});
        for (Generated &g : results[i].lines) {
            if (layout.addedStyle(g.line.style) && missing.insert(g.line.style).second)
                run.styles.push_back(*layout.addedStyle(g.line.style));
            run.lines.push_back(std::move(g.line));
        }
    }
    for (std::size_t k = 0; k < compiled.emits.size(); ++k)
        run.emits.push_back({compiled.nodes[compiled.emits[k]].graph->id, counts[k].load(),
                             static_cast<double>(nanos[k].load()) / 1e9});
    if (luaStopped) {
        run.ran = false;
        run.applied.clear();
        run.lines.clear();
        run.styles.clear();
    }
    return finish();
}

std::expected<NodeEffectRun, GraphProblem> runDocumentGraph(const core::Document &document, core::LineId graphLine,
                                                            const NodeEffectOptions &options)
{
    for (const DocumentGraph &g : documentGraphs(document)) {
        if (g.line != graphLine)
            continue;
        if (!g.graph)
            return std::unexpected(g.graph.error());
        return runNodeEffect(document, g.name, g.style, *g.graph, options);
    }
    return std::unexpected(GraphProblem{{}, {}, "the Line is not a graph Line"});
}

namespace {

bool sameOutput(const core::LineRecord &a, const core::LineRecord &b)
{
    return a.layer.value == b.layer.value && a.start.value == b.start.value && a.end.value == b.end.value &&
           a.style == b.style && a.actor == b.actor && a.marginLeft.value == b.marginLeft.value &&
           a.marginRight.value == b.marginRight.value && a.marginVertical.value == b.marginVertical.value &&
           a.text == b.text;
}

}

bool applyNodeEffect(core::Document &document, const NodeEffectRun &run, const NodeEffectRun *owners)
{
    if (!run.ran)
        return false;
    std::map<core::LineId, core::LineId> ownerOf;
    if (owners) {
        boost::dynamic_bitset<> used(owners->lines.size());
        for (const core::LineRecord *l : document.lines()) {
            if (!isGeneratedBy(*l, run.name) || l->generatedFrom.value != 0)
                continue;
            for (std::size_t k = 0; k < owners->lines.size(); ++k)
                if (!used[k] && sameOutput(*l, owners->lines[k])) {
                    used[k] = true;
                    ownerOf[l->id] = owners->lines[k].generatedFrom;
                    break;
                }
        }
    }
    const std::set<core::LineId> sources(run.sources.begin(), run.sources.end());
    std::set<core::LineId> old;
    std::optional<core::LineId> anchor;
    for (const core::LineRecord *l : document.lines()) {
        if (!isGeneratedBy(*l, run.name))
            continue;
        const auto owner = ownerOf.find(l->id);
        const core::LineId source = owner != ownerOf.end() ? owner->second : l->generatedFrom;
        if (!run.complete && !sources.contains(source))
            continue;
        old.insert(l->id);
        if (!anchor)
            anchor = l->id;
    }
    if (old.empty() && run.lines.empty())
        return false;
    for (const auto &fields : run.styles)
        document.appendStyle(fields);
    for (const core::LineRecord &line : run.lines) {
        if (!(anchor ? document.insertLineBefore(*anchor, line) : document.appendLine(line)))
            return false;
    }
    document.removeLinesIf([&](const core::LineRecord &l) { return old.contains(l.id); });
    for (const core::LineRecord *l : document.lines())
        if (run.applied.contains(l->id) && (!l->comment || l->effect != u8"karaoke"))
            document.editLine(l->id, [](core::LineRecord &line) {
                line.comment = true;
                line.effect = u8"karaoke";
            });
    return true;
}

std::expected<void, CommandRefusal> applyNodeEffect(EditSession &session, const NodeEffectRun &run,
                                                    const NodeEffectRun *owners)
{
    Command command;
    command.name = "Apply node effect";
    command.expectedRevision = session.revision();
    command.apply = [&](core::Document &document) { return applyNodeEffect(document, run, owners); };
    return session.run(command);
}

namespace {

struct Scalar {
    std::optional<NodeValue> constant;
    std::string lua;
    bool var = false;
};

struct PointSym {
    Scalar x, y;
};

using Sym = std::variant<Scalar, PointSym, NodeTags, NodeDrawing>;

enum class Scope { Line, Unit };

struct Context {
    Scope scope = Scope::Unit;
    int loop = kUnwired;
};

std::string luaString(std::string_view s)
{
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c == '\n' ? std::string("\\n") : std::string(1, c);
    }
    return out + "\"";
}

Scalar luaScalar(std::string code)
{
    return {std::nullopt, std::move(code), false};
}

Scalar variable(std::string_view name)
{
    return {std::nullopt, "$" + std::string(name), true};
}

std::string operand(const Scalar &s)
{
    if (!s.constant)
        return s.var ? "(" + s.lua + ")" : s.lua;
    if (const auto *v = std::get_if<double>(&*s.constant))
        return *v < 0 ? "(" + numberText(*v) + ")" : numberText(*v);
    if (const auto *b = std::get_if<bool>(&*s.constant))
        return *b ? "true" : "false";
    if (const auto *t = std::get_if<std::string>(&*s.constant))
        return luaString(*t);
    if (const auto *c = std::get_if<NodeColour>(&*s.constant))
        return luaString(assColour(*c));
    return "nil";
}

std::optional<double> constantNumber(const Scalar &s)
{
    if (s.constant)
        if (const auto *v = std::get_if<double>(&*s.constant))
            return *v;
    return std::nullopt;
}

bool templateSafe(std::string_view text)
{
    return text.find('!') == std::string_view::npos && text.find('$') == std::string_view::npos;
}

bool isLineTemplateKeyword(std::string_view word)
{
    constexpr std::string_view kKeywords[] = {"pre-line", "line", "syl", "furi", "char", "all", "repeat",
                                              "loop", "notext", "keeptags", "noblank", "multi", "fx", "fxgroup"};
    return std::ranges::find(kKeywords, word) != std::end(kKeywords);
}

std::string templateId(std::u8string_view graph, std::string_view emit)
{
    std::string out;
    for (const char c : narrow(graph) + "_" + std::string(emit)) {
        const auto u = static_cast<unsigned char>(c);
        out += std::isalnum(u) && u < 0x80 ? static_cast<char>(std::tolower(u)) : '_';
    }
    return isLineTemplateKeyword(out) ? out + "_" : out;
}

class TemplateWriter {
public:
    TemplateWriter(const Compiled &c, std::u8string_view name, bool allStyles)
        : m_c(c), m_name(name), m_allStyles(allStyles)
    {
    }

    std::vector<TemplateLine> lines()
    {
        std::vector<TemplateLine> out;
        for (const int emit : m_c.emits)
            if (auto more = emitLines(emit))
                out.insert(out.end(), more->begin(), more->end());
        return out;
    }

    std::vector<NodeEffectProblem> problems;

private:
    const CNode &node(int i) const { return m_c.nodes[static_cast<std::size_t>(i)]; }
    const CNode::Input &wire(int i, std::string_view port) const { return node(i).inputs[node(i).in(port)]; }
    bool wired(int i, std::string_view port) const { return wire(i, port).node != kUnwired; }

    std::nullopt_t refuse(int i, std::string message)
    {
        NodeEffectProblem p{node(i).graph->id, std::nullopt, std::move(message)};
        if (std::ranges::find(problems, p) == problems.end())
            problems.push_back(std::move(p));
        return std::nullopt;
    }

    std::optional<Sym> input(int i, std::string_view port, const Context &ctx)
    {
        const CNode::Input &in = wire(i, port);
        if (in.node != kUnwired)
            return output(in.node, in.port, ctx);
        if (const auto *p = std::get_if<NodePoint>(&in.constant))
            return PointSym{Scalar{p->x}, Scalar{p->y}};
        if (const auto *t = std::get_if<NodeTags>(&in.constant))
            return *t;
        if (const auto *d = std::get_if<NodeDrawing>(&in.constant))
            return *d;
        return Scalar{in.constant};
    }

    template <typename T> std::optional<T> get(int i, std::string_view port, const Context &ctx)
    {
        auto sym = input(i, port, ctx);
        if (!sym)
            return std::nullopt;
        if (auto *v = std::get_if<T>(&*sym))
            return std::move(*v);
        return refuse(i, "reads " + std::string(port) + " of a type the template can't write");
    }

    std::optional<std::string> text(int i, const Scalar &s, PortType type)
    {
        if (s.constant) {
            if (const auto *v = std::get_if<double>(&*s.constant))
                return type == PortType::Alpha ? assAlpha(*v) : numberText(*v);
            if (const auto *c = std::get_if<NodeColour>(&*s.constant))
                return assColour(*c);
            if (const auto *t = std::get_if<std::string>(&*s.constant)) {
                if (!templateSafe(*t))
                    return refuse(i, "writes a ! or $, which the templater reads as code");
                return *t;
            }
            return refuse(i, "writes a value the template can't hold");
        }
        if (s.var && type != PortType::Alpha)
            return s.lua;
        if (type == PortType::Alpha)
            return "!string.format(\"&H%02X&\", math.max(0, math.min(255, math.floor(" + operand(s) + " + 0.5))))!";
        return "!" + s.lua + "!";
    }

    std::optional<std::string> textOf(int i, std::string_view port, PortType type, const Context &ctx)
    {
        auto s = get<Scalar>(i, port, ctx);
        return s ? text(i, *s, type) : std::nullopt;
    }

    std::optional<std::string> pointText(int i, std::string_view port, const Context &ctx)
    {
        auto p = get<PointSym>(i, port, ctx);
        if (!p)
            return std::nullopt;
        auto x = text(i, p->x, PortType::Number), y = text(i, p->y, PortType::Number);
        if (!x || !y)
            return std::nullopt;
        return *x + "," + *y;
    }

    std::optional<Sym> output(int i, int port, const Context &ctx)
    {
        const CNode &n = node(i);
        const std::string &name = n.ports.outputs[static_cast<std::size_t>(port)].name;
        switch (n.kind) {
        case Kind::Repeat:
            if (i != ctx.loop)
                return refuse(i, "is read outside its units");
            if (name == "j")
                return luaScalar("j");
            if (name == "count")
                return luaScalar("maxj");
            return refuse(i, "gives its units only to an Emit");
        case Kind::Info:
            return info(i, name, ctx);
        case Kind::Number:
        case Kind::Text:
        case Kind::Colour:
            return Scalar{constantOf(nodeField(*n.graph, "value"), n.ports.outputs.front().type)};
        case Kind::Point: {
            auto x = get<Scalar>(i, "x", ctx), y = get<Scalar>(i, "y", ctx);
            if (!x || !y)
                return std::nullopt;
            return PointSym{std::move(*x), std::move(*y)};
        }
        case Kind::Choose:
            return choose(i, ctx);
        case Kind::Calculate:
            return calculate(i, ctx);
        case Kind::Stagger:
            return stagger(i, ctx);
        case Kind::Progress:
            return progress(i, ctx);
        case Kind::Tag:
            return tag(i, ctx);
        case Kind::Move:
            return move(i, ctx);
        case Kind::Fade: {
            auto in = textOf(i, "fade_in", PortType::Time, ctx), out = textOf(i, "fade_out", PortType::Time, ctx);
            auto before = get<NodeTags>(i, "before", ctx);
            if (!in || !out || !before)
                return std::nullopt;
            return NodeTags{before->text + "\\fad(" + *in + "," + *out + ")"};
        }
        case Kind::Transform:
            return transform(i, ctx);
        case Kind::Shape:
            if (!templateSafe(n.text))
                return refuse(i, "writes a ! or $, which the templater reads as code");
            return NodeDrawing{n.text, static_cast<int>(n.number)};
        case Kind::ForEach:
            return refuse(i, "gives a unit's place among the units, which has no templater variable");
        case Kind::ClipGradient:
        case Kind::Array:
        case Kind::Particles:
            return refuse(i, unitsRefusal(n.kind));
        case Kind::Highlights:
            return refuse(i, "gives a highlight's place among the highlights, which has no templater variable");
        case Kind::Retime:
            return refuse(i, "gives times only to an Emit's start and end");
        case Kind::Join:
            return refuse(i, "gives text only to the Emit of its Line");
        case Kind::Random:
            return refuse(i, "draws numbers the templater can't draw again");
        case Kind::Lua:
            return refuse(i, "runs in HikariSub's Lua helper, not in the templater");
        case Kind::AudioPeaks:
            return refuse(i, "reads the audio, which the templater can't");
        default:
            return refuse(i, "has no template form");
        }
    }

    std::optional<Sym> info(int i, const std::string &port, const Context &ctx)
    {
        static const std::map<std::string_view, std::string_view> kVariables{
            {"i", "i"},           {"n", "syln"},          {"start", "start"},     {"end", "end"},
            {"duration", "dur"},  {"kdur", "kdur"},       {"mid", "mid"},         {"left", "left"},
            {"center", "center"}, {"right", "right"},     {"width", "width"},     {"top", "top"},
            {"middle", "middle"}, {"bottom", "bottom"},   {"height", "height"},   {"x", "x"},
            {"y", "y"},           {"layer", "layer"},     {"line_index", "li"},   {"line_start", "lstart"},
            {"line_end", "lend"}, {"line_duration", "ldur"}, {"line_left", "lleft"}, {"line_center", "lcenter"},
            {"line_right", "lright"}, {"line_width", "lwidth"}, {"line_top", "ltop"}, {"line_middle", "lmiddle"},
            {"line_bottom", "lbottom"}, {"line_height", "lheight"}};
        static const std::map<std::string_view, std::string_view> kStyle{
            {"style_fontsize", "fontsize"}, {"style_scale_x", "scale_x"}, {"style_scale_y", "scale_y"},
            {"style_spacing", "spacing"},   {"style_angle", "angle"},     {"style_outline", "outline"},
            {"style_shadow", "shadow"},     {"style_alignment", "align"}};
        if (const auto v = kVariables.find(port); v != kVariables.end())
            return variable(v->second);
        if (const auto s = kStyle.find(port); s != kStyle.end())
            return luaScalar("orgline.styleref." + std::string(s->second));
        if (port == "text")
            return luaScalar(ctx.scope == Scope::Line ? "orgline.text_stripped" : "syl.text_stripped");
        if (port == "position")
            return PointSym{variable("x"), variable("y")};
        return refuse(i, "gives " + port + ", which has no templater variable");
    }

    std::optional<Sym> choose(int i, const Context &ctx)
    {
        const CNode &n = node(i);
        auto index = get<Scalar>(i, "index", ctx);
        if (!index)
            return std::nullopt;
        if (n.list.empty())
            return refuse(i, "has no values");
        const auto count = static_cast<std::int64_t>(n.list.size());
        if (const auto k = constantNumber(*index)) {
            if (!std::isfinite(*k))
                return refuse(i, "has no value at that place");
            const auto at = static_cast<std::int64_t>(std::floor(*k)) - 1;
            return Scalar{n.list[static_cast<std::size_t>(((at % count) + count) % count)]};
        }
        std::string table = "({";
        for (std::size_t k = 0; k < n.list.size(); ++k)
            table += (k ? ", " : "") + operand(Scalar{n.list[k]});
        return luaScalar(table + "})[(math.floor(" + operand(*index) + ") - 1) % " + std::to_string(count) + " + 1]");
    }

    std::optional<Sym> calculate(int i, const Context &ctx)
    {
        static const std::map<std::string_view, std::string_view> kInfix{
            {"add", "+"}, {"subtract", "-"}, {"multiply", "*"}, {"divide", "/"}, {"modulo", "%"}, {"power", "^"}};
        static const std::map<std::string_view, std::string_view> kCalls{
            {"minimum", "math.min"}, {"maximum", "math.max"}, {"floor", "math.floor"},
            {"ceil", "math.ceil"},   {"absolute", "math.abs"}};
        const std::string &op = node(i).mode;
        auto a = get<Scalar>(i, "a", ctx), b = get<Scalar>(i, "b", ctx);
        if (!a || !b)
            return std::nullopt;
        const bool binary = kInfix.contains(op) || op == "minimum" || op == "maximum";
        const auto ca = constantNumber(*a), cb = constantNumber(*b);
        if (ca && (cb || !binary))
            return Scalar{calculated(op, *ca, cb.value_or(0))};
        const std::string x = operand(*a), y = operand(*b);
        if (const auto infix = kInfix.find(op); infix != kInfix.end())
            return luaScalar("(" + x + " " + std::string(infix->second) + " " + y + ")");
        if (const auto call = kCalls.find(op); call != kCalls.end())
            return luaScalar(std::string(call->second) + "(" + x + (binary ? ", " + y : std::string()) + ")");
        if (op == "round")
            return luaScalar("math.floor(" + x + " + 0.5)");
        return luaScalar("(-" + x + ")");
    }

    std::optional<Sym> stagger(int i, const Context &ctx)
    {
        auto index = get<Scalar>(i, "index", ctx), count = get<Scalar>(i, "count", ctx), step = get<Scalar>(i, "step", ctx);
        if (!index || !count || !step)
            return std::nullopt;
        const std::string &from = node(i).mode;
        const std::string x = operand(*index), n = operand(*count), s = operand(*step);
        if (from == "end")
            return luaScalar("((" + n + " - " + x + ") * " + s + ")");
        if (from == "centre")
            return luaScalar("(math.abs(" + x + " - (" + n + " + 1) / 2) * " + s + ")");
        return luaScalar("((" + x + " - 1) * " + s + ")");
    }

    std::optional<Sym> progress(int i, const Context &ctx)
    {
        auto index = get<Scalar>(i, "index", ctx), count = get<Scalar>(i, "count", ctx);
        if (!index || !count)
            return std::nullopt;
        const std::string &ease = node(i).mode;
        if (const auto x = constantNumber(*index), n = constantNumber(*count); x && n)
            return Scalar{eased(*n > 1 ? (*x - 1) / (*n - 1) : 0.0, ease)};
        const std::string curve = ease == "in"       ? "t * t"
                                  : ease == "out"    ? "1 - (1 - t) * (1 - t)"
                                  : ease == "in_out" ? "(t < 0.5 and 2 * t * t or 1 - 2 * (1 - t) * (1 - t))"
                                                     : "t";
        return luaScalar("(function(i, n) local t = n > 1 and (i - 1) / (n - 1) or 0; t = math.max(0, math.min(1, t)); "
                         "return " + curve + " end)(" + operand(*index) + ", " + operand(*count) + ")");
    }

    std::optional<Sym> tag(int i, const Context &ctx)
    {
        const std::string &tag = node(i).mode;
        auto before = get<NodeTags>(i, "before", ctx);
        if (!before)
            return std::nullopt;
        std::optional<std::string> arg;
        if (tag == "raw")
            arg = textOf(i, "text", PortType::Text, ctx);
        else if (tag == "fn" || tag == "r")
            arg = textOf(i, "text", PortType::Text, ctx).transform([&](std::string t) { return "\\" + tag + t; });
        else if (tag == "pos" || tag == "org")
            arg = pointText(i, "point", ctx).transform([&](std::string p) { return "\\" + tag + "(" + p + ")"; });
        else if ((tag == "clip" || tag == "iclip") && wired(i, "drawing")) {
            if (auto d = get<NodeDrawing>(i, "drawing", ctx))
                arg = "\\" + tag + "(" + (d->scale != 1 ? std::to_string(d->scale) + "," : std::string()) + d->commands + ")";
        } else if (tag == "clip" || tag == "iclip") {
            auto a = pointText(i, "point", ctx), b = pointText(i, "point2", ctx);
            if (a && b)
                arg = "\\" + tag + "(" + *a + "," + *b + ")";
        } else if (tag.size() == 2 && tag[1] == 'c')
            arg = textOf(i, "colour", PortType::Colour, ctx).transform([&](std::string c) { return "\\" + tag + c; });
        else if (tag == "alpha" || (tag.size() == 2 && tag[1] == 'a'))
            arg = textOf(i, "alpha", PortType::Alpha, ctx).transform([&](std::string a) { return "\\" + tag + a; });
        else
            arg = textOf(i, "value", PortType::Number, ctx).transform([&](std::string v) { return "\\" + tag + v; });
        if (!arg)
            return std::nullopt;
        return NodeTags{before->text + *arg};
    }

    std::optional<Sym> move(int i, const Context &ctx)
    {
        auto before = get<NodeTags>(i, "before", ctx);
        auto from = pointText(i, "from", ctx), to = pointText(i, "to", ctx);
        if (!before || !from || !to)
            return std::nullopt;
        std::string out = before->text + "\\move(" + *from + "," + *to;
        if (node(i).flag) {
            auto t1 = textOf(i, "t1", PortType::Time, ctx), t2 = textOf(i, "t2", PortType::Time, ctx);
            if (!t1 || !t2)
                return std::nullopt;
            out += "," + *t1 + "," + *t2;
        }
        return NodeTags{out + ")"};
    }

    std::optional<Sym> transform(int i, const Context &ctx)
    {
        auto before = get<NodeTags>(i, "before", ctx), tags = get<NodeTags>(i, "tags", ctx);
        auto accel = get<Scalar>(i, "accel", ctx);
        if (!before || !tags || !accel)
            return std::nullopt;
        std::string out = before->text + "\\t(";
        if (node(i).flag) {
            auto t1 = textOf(i, "t1", PortType::Time, ctx), t2 = textOf(i, "t2", PortType::Time, ctx);
            if (!t1 || !t2)
                return std::nullopt;
            out += *t1 + "," + *t2 + ",";
        }
        const auto a = constantNumber(*accel);
        if (!a)
            return refuse(i, "takes an acceleration that is not a constant");
        if (*a != 1)
            out += numberText(*a) + ",";
        return NodeTags{out + tags->text + ")"};
    }

    static std::string unitsRefusal(Kind kind)
    {
        switch (kind) {
        case Kind::ClipGradient:
            return "cuts units into gradient strips, which a template can't write";
        case Kind::Array:
            return "copies units on a grid, circle or path, which a template can't write";
        case Kind::Particles:
            return "places units at random, which the templater can't repeat";
        case Kind::Repeat:
            return "repeats inside another repeat, which a template can't";
        default:
            return "gives units the templater can't go through";
        }
    }

    struct Units {
        int forEach = kUnwired;
        std::string unit;
        bool skipBlank = false;
        int loop = kUnwired;
        int count = 1;
        bool multi = false;
        std::optional<std::string> fx;
    };

    std::optional<Units> unitsOf(int emit)
    {
        int at = wire(emit, "units").node;
        if (at == kUnwired)
            return refuse(emit, "has no units");
        Units u;
        if (node(at).kind == Kind::Repeat) {
            const auto count = get<Scalar>(at, "count", {}).and_then([](const Scalar &s) { return constantNumber(s); });
            if (!count || *count < 1 || *count != std::floor(*count))
                return refuse(at, "repeats a number of times that is not a constant whole number above 0");
            u.loop = at;
            u.count = static_cast<int>(*count);
            at = wire(at, "units").node;
            if (at == kUnwired)
                return refuse(u.loop, "has no units");
        }
        int highlights = kUnwired, inlineFx = kUnwired;
        while (node(at).kind == Kind::Highlights || node(at).kind == Kind::InlineFx) {
            int &seen = node(at).kind == Kind::Highlights ? highlights : inlineFx;
            if (seen != kUnwired)
                return refuse(at, node(at).kind == Kind::Highlights
                                      ? "gives highlights of highlights, which a template can't"
                                      : "keeps units by a second inline-fx, which a template can't");
            seen = at;
            at = wire(at, "units").node;
            if (at == kUnwired)
                return refuse(seen, "has no units");
        }
        if (node(at).kind == Kind::Repeat && (highlights != kUnwired || inlineFx != kUnwired))
            return refuse(at, "repeats before the highlights or inline-fx after it, which a template can't");
        if (node(at).kind != Kind::ForEach)
            return refuse(at, unitsRefusal(node(at).kind));
        u.forEach = at;
        u.unit = node(at).mode;
        u.skipBlank = node(at).flag;
        if (u.unit == "line" && highlights != kUnwired)
            return refuse(highlights, "gives a Line's highlights, which a line template can't");
        if (u.unit == "line" && inlineFx != kUnwired)
            return refuse(inlineFx, "keeps Lines by inline-fx, which a line template can't");
        if (highlights != kUnwired) {
            if (node(highlights).flag != u.skipBlank)
                return refuse(highlights, "leaves out blank highlights only when its For each leaves out blank "
                                          "units, as a template's noblank does both");
            u.multi = true;
        }
        if (inlineFx != kUnwired) {
            const std::string &name = node(inlineFx).text;
            if (std::ranges::any_of(name, [](unsigned char c) { return std::isspace(c); }))
                return refuse(inlineFx, "keeps an inline-fx with a space in its name, which a template can't name");
            u.fx = name;
        }
        return u;
    }

    std::optional<std::string> retime(int emit, const Context &ctx)
    {
        const CNode::Input &start = wire(emit, "start"), &end = wire(emit, "end");
        if (start.node == kUnwired && end.node == kUnwired)
            return std::string();
        if (start.node == kUnwired || start.node != end.node || node(start.node).kind != Kind::Retime || start.port != 0 ||
            end.port != 1)
            return refuse(emit, "takes its start and end from something other than one Retime node");
        const int r = start.node;
        const std::string &mode = node(r).mode;
        if (ctx.scope == Scope::Line && mode != "line" && mode != "preline" && mode != "postline" && mode != "set")
            return refuse(r, "times a Line by a syllable");
        auto a = get<Scalar>(r, "start_offset", ctx), b = get<Scalar>(r, "end_offset", ctx);
        if (!a || !b)
            return std::nullopt;
        return "!retime(" + luaString(mode) + ", " + operand(*a) + ", " + operand(*b) + ")!";
    }

    static std::string textModifier(const std::string &unitText)
    {
        return unitText == "raw" ? "keeptags" : unitText == "none" ? "notext" : "";
    }

    std::optional<std::vector<TemplateLine>> emitLines(int emit)
    {
        const CNode &n = node(emit);
        const auto units = unitsOf(emit);
        if (!units)
            return std::nullopt;
        const Context ctx{units->unit == "line" ? Scope::Line : Scope::Unit, units->loop};
        const auto when = get<Scalar>(emit, "when", ctx);
        const auto layer = get<Scalar>(emit, "layer", ctx).and_then([](const Scalar &s) { return constantNumber(s); });
        auto times = retime(emit, ctx);
        auto tags = get<NodeTags>(emit, "tags", ctx);
        if (when && !when->constant)
            refuse(emit, "writes Lines on a condition, which a template can't");
        if (!layer)
            refuse(emit, "takes a Layer that is not a constant");
        if (!when || !when->constant || !layer || !times || !tags)
            return std::nullopt;
        if (!std::get<bool>(*when->constant))
            return std::vector<TemplateLine>{};

        std::vector<std::string> modifiers;
        const auto addModifier = [&](std::string m) {
            if (!m.empty())
                modifiers.push_back(std::move(m));
        };
        std::string body, joinText;
        bool joined = false;
        if (ctx.scope == Scope::Line) {
            if (units->skipBlank)
                return refuse(units->forEach, "leaves out blank Lines, which a line template can't");
            if (wired(emit, "drawing"))
                return refuse(emit, "draws in place of a Line's text, which a line template can't");
            if (wired(emit, "text")) {
                const int j = wire(emit, "text").node;
                if (node(j).kind != Kind::Join)
                    return refuse(emit, "writes a Line's text from something other than a Join of its syllables");
                const int syls = wire(j, "units").node;
                if (syls == kUnwired || node(syls).kind != Kind::ForEach || node(syls).mode != "syl" || node(syls).flag)
                    return refuse(j, "joins something other than every syllable of the Line");
                if (wired(j, "text"))
                    return refuse(j, "joins text of its own, which a line template can't");
                auto jtags = get<NodeTags>(j, "tags", Context{Scope::Unit, units->loop});
                if (!jtags)
                    return std::nullopt;
                addModifier(textModifier(node(j).mode));
                joinText = jtags->text.empty() ? std::string() : "{" + jtags->text + "}";
                joined = true;
            } else if (n.mode == "none") {
                return refuse(emit, "writes a Line without text, which a line template can't");
            } else {
                addModifier(textModifier(n.mode));
            }
        } else if (wired(emit, "text")) {
            auto t = textOf(emit, "text", PortType::Text, ctx);
            if (!t)
                return std::nullopt;
            body = *t;
            addModifier("notext");
        } else if (wired(emit, "drawing")) {
            auto d = get<NodeDrawing>(emit, "drawing", ctx);
            if (!d)
                return std::nullopt;
            tags->text += "\\p" + std::to_string(d->scale);
            body = d->commands;
            addModifier("notext");
        } else {
            addModifier(textModifier(n.mode));
        }
        if (units->count != 1)
            addModifier("loop " + std::to_string(units->count));
        if (units->multi)
            addModifier("multi");
        const std::string group = templateId(m_name, n.graph->id);
        if (units->fx)
            addModifier(units->fx->empty() ? "fxgroup " + group : "fx " + *units->fx);
        if (m_allStyles)
            addModifier("all");

        std::string head;
        if (ctx.scope == Scope::Line)
            head = joined ? "pre-line " + templateId(m_name, n.graph->id) : "pre-line";
        else if (units->unit == "furi")
            head = units->skipBlank ? "furi noblank" : "furi";
        else
            head = std::string(units->skipBlank ? "syl noblank" : "syl") + (units->unit == "char" ? " char" : "");
        for (const std::string &m : modifiers)
            head += " " + m;
        const int layerValue = static_cast<int>(*layer);
        const std::string text = *times + (tags->text.empty() ? std::string() : "{" + tags->text + "}") + body;
        std::vector<TemplateLine> out;
        if (units->fx && units->fx->empty())
            out.push_back({0, wide(std::string(units->unit == "furi" ? "code furi" : "code syl") + (m_allStyles ? " all" : "")),
                           wide("fxgroup[" + luaString(group) + "] = (syl.inline_fx == \"\")")});
        out.push_back({layerValue, wide("template " + head), wide(text)});
        if (joined)
            out.push_back({layerValue, wide("template line " + templateId(m_name, n.graph->id)), wide(joinText)});
        return out;
    }

    const Compiled &m_c;
    std::u8string_view m_name;
    bool m_allStyles = false;
};

}

std::expected<std::vector<TemplateLine>, std::vector<NodeEffectProblem>> exportTemplateLines(const NodeGraph &graph,
                                                                                           std::u8string_view name)
{
    std::vector<NodeEffectProblem> problems;
    for (const GraphProblem &p : validateGraph(graph))
        problems.push_back({p.node, std::nullopt, p.port.empty() ? p.message : p.port + " " + p.message});
    if (!problems.empty())
        return std::unexpected(std::move(problems));
    const Compiled compiled = compile(*flattenGraph(graph));
    TemplateWriter writer(compiled, name, graph.allStyles);
    std::vector<TemplateLine> lines = writer.lines();
    if (!writer.problems.empty())
        return std::unexpected(std::move(writer.problems));
    return lines;
}

std::expected<void, std::vector<NodeEffectProblem>> exportTemplateLines(EditSession &session, core::LineId graphLine)
{
    const auto refused = [](std::string message) {
        return std::unexpected(std::vector<NodeEffectProblem>{{{}, std::nullopt, std::move(message)}});
    };
    for (const DocumentGraph &g : documentGraphs(session.document())) {
        if (g.line != graphLine)
            continue;
        if (!g.graph)
            return std::unexpected(std::vector<NodeEffectProblem>{{g.graph.error().node, std::nullopt, g.graph.error().message}});
        auto lines = exportTemplateLines(*g.graph, g.name);
        if (!lines)
            return std::unexpected(std::move(lines.error()));
        Command command;
        command.name = "Export as template lines";
        command.expectedRevision = session.revision();
        command.apply = [&](core::Document &document) {
            const std::vector<const core::LineRecord *> all = document.lines();
            const auto at = std::ranges::find(all, graphLine, &core::LineRecord::id);
            if (at == all.end())
                return false;
            const core::LineRecord source = **at;
            core::LineId after = graphLine;
            for (const TemplateLine &t : *lines) {
                core::LineRecord line = source;
                line.layer = core::IntField{wide(std::to_string(t.layer)), t.layer};
                line.effect = t.effect;
                line.text = t.text;
                const auto added = document.insertLineAfter(after, std::move(line));
                if (!added)
                    return false;
                after = *added;
            }
            return true;
        };
        if (!session.run(command))
            return refused("the file did not take the template Lines");
        return {};
    }
    return refused("the Line is not a graph Line");
}

}
