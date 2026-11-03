#include "hikari/application/node_effects.h"

#include "hikari/application/macro_transaction.h"
#include "hikari/application/template_preview.h"
#include "node_layout.h"

#include <boost/dynamic_bitset.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace hikari::application {

using json::Json;
using node_layout::numberText;
using node_layout::Unit;

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

enum class Kind {
    ForEach, Repeat, Frames, Info, Number, Text, Colour, Point, Choose, Calculate, Compare, Progress, Random,
    Retime, Stagger, Tag, Move, Fade, Transform, Gradient, Shape, Join, Emit, Lua
};

const std::map<std::string, Kind, std::less<>> kKinds{
    {"units.for_each", Kind::ForEach}, {"units.repeat", Kind::Repeat},     {"units.frames", Kind::Frames},
    {"units.info", Kind::Info},        {"values.number", Kind::Number},    {"values.text", Kind::Text},
    {"values.colour", Kind::Colour},   {"values.point", Kind::Point},      {"values.choose", Kind::Choose},
    {"math.calculate", Kind::Calculate}, {"math.compare", Kind::Compare},  {"math.progress", Kind::Progress},
    {"math.random", Kind::Random},     {"time.retime", Kind::Retime},      {"time.stagger", Kind::Stagger},
    {"tags.tag", Kind::Tag},           {"tags.move", Kind::Move},          {"tags.fade", Kind::Fade},
    {"tags.transform", Kind::Transform}, {"colours.gradient", Kind::Gradient}, {"drawings.shape", Kind::Shape},
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
    bool chars = false, furi = false;
    std::vector<std::string> emitSignature;
};

std::string upstreamSignature(const Compiled &c, int node, std::set<int> &seen)
{
    if (!seen.insert(node).second)
        return {};
    const CNode &n = c.nodes[node];
    std::string out = n.graph->id + '|' + n.graph->type + '|' + n.graph->fields.dump() + '|';
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
        n.kind = kKinds.at(g.type);
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
            for (const Json &v : nodeField(g, "colours").asArray())
                n.list.push_back(colourOf(v));
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
            n.text = text("code");
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
        case Kind::Info:
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
};

struct Element {
    UnitKind kind = UnitKind::Syl;
    int syl = 0, ch = -1, furi = -1;
    int index = 1, count = 1;
    std::vector<Loop> loops;
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
    Evaluator(const Compiled &c, const node_layout::Line &line, std::u8string_view name, std::int64_t seed,
              LuaNodePort *lua, Problems &problems)
        : m_c(c), m_line(line), m_name(name), m_seed(seed), m_lua(lua), m_problems(problems),
          m_lineMemo(static_cast<std::size_t>(c.slots))
    {
    }

    bool emit(int emitRank, std::vector<Generated> &out);

private:
    const Unit &unitOf(const Element &e) const
    {
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
        return v;
    }

    NodeValue compute(int node, int port, const Element &e, Memo &memo);
    NodeValue info(std::string_view port, const Element &e) const;
    NodeValue join(int node, const Element &e);
    NodeValue lua(int node, int port, const Element &e, Memo &memo);
    bool luaBatch(int node, const std::vector<Element> &elements, std::vector<Memo> &memos);

    const Compiled &m_c;
    const node_layout::Line &m_line;
    std::u8string_view m_name;
    std::int64_t m_seed;
    LuaNodePort *m_lua;
    Problems &m_problems;
    Memo m_lineMemo;
};

NodeValue Evaluator::info(std::string_view port, const Element &e) const
{
    const Unit &u = unitOf(e);
    const Unit &l = m_line.line;
    const node_layout::StyleInfo &s = *m_line.style;
    static const std::map<std::string_view, std::function<NodeValue(const Unit &, const Unit &,
                                                                             const node_layout::StyleInfo &,
                                                                             const node_layout::Line &)>>
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
            {"style_primary", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.primary); }},
            {"style_secondary", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.secondary); }},
            {"style_outline_colour", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.outlineColour); }},
            {"style_back", [](auto &, auto &, auto &s, auto &) { return NodeValue(s.back); }},
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
    case Kind::Frames: {
        const Loop *loop = loopOf(e, node);
        if (!loop) {
            m_problems.add(n.graph->id, "was read outside its units");
            return 0.0;
        }
        const std::string &name = n.ports.outputs[port].name;
        if (n.kind == Kind::Repeat)
            return double(name == "j" ? loop->index : loop->count);
        if (name == "start")
            return loop->start;
        if (name == "end")
            return loop->end;
        if (name == "index")
            return double(loop->index);
        if (name == "count")
            return double(loop->count);
        return loop->count > 1 ? double(loop->index - 1) / double(loop->count - 1) : 0.0;
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
    case Kind::Calculate: {
        const double a = num("a"), b = num("b");
        const std::string &op = n.mode;
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
        std::uint64_t h = mix(static_cast<std::uint64_t>(m_seed), static_cast<std::uint64_t>(n.number));
        h = mix(h, hashText(n.graph->id));
        h = mix(h, static_cast<std::uint64_t>(m_line.macro.startMs));
        h = mix(h, hashText(m_line.macro.text));
        if (n.mode != "line") {
            h = mix(h, static_cast<std::uint64_t>(e.kind));
            h = mix(h, static_cast<std::uint64_t>(e.syl + 1));
            h = mix(h, static_cast<std::uint64_t>(e.ch + 1));
            h = mix(h, static_cast<std::uint64_t>(e.furi + 1));
        }
        if (n.mode == "element")
            for (const Loop &l : e.loops)
                h = mix(mix(h, static_cast<std::uint64_t>(l.node)), static_cast<std::uint64_t>(l.index));
        const double u = static_cast<double>(h >> 11) * 0x1.0p-53;
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
    case Kind::Gradient: {
        const double p = std::clamp(num("progress"), 0.0, 1.0);
        if (n.list.size() == 1 || std::isnan(p))
            return n.list.front();
        const double at = p * double(n.list.size() - 1);
        const std::size_t k = std::min(static_cast<std::size_t>(at), n.list.size() - 2);
        const double f = at - double(k);
        const NodeColour a = std::get<NodeColour>(n.list[k]), b = std::get<NodeColour>(n.list[k + 1]);
        const auto channel = [&](int x, int y) { return static_cast<int>(std::floor(x + (y - x) * f + 0.5)); };
        return NodeColour{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
    }
    case Kind::Shape:
        return NodeDrawing{n.text, static_cast<int>(n.number)};
    case Kind::Join:
        return join(node, e);
    case Kind::Lua:
        return lua(node, port, e, memo);
    case Kind::Emit:
        break;
    }
    return 0.0;
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
    if (!result || result->size() != elements.size()) {
        m_problems.add(n.graph->id, result ? "gave a wrong number of rows" : result.error());
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
        d.generatedFrom = m_line.source->id.value;
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

std::string cacheKey(const Compiled &c, int emitRank, const node_layout::Line &line, std::u8string_view name)
{
    return narrow(name) + '\x1e' + std::to_string(c.flat.seed) + '\x1e' + c.emitSignature[emitRank] + '\x1e' +
           line.signature;
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
    if (!compiled.lua.empty() && !options.trustLua) {
        for (const int lua : compiled.lua)
            run.problems.push_back({compiled.nodes[lua].graph->id, std::nullopt,
                                    "runs only once you trust this Document's Lua"});
        return finish();
    }
    run.ran = true;

    node_layout::Layout layout(document, *options.measure, options.video);
    std::size_t info = 0, styles = 0;
    for (const auto &section : document.sections())
        for (const auto &record : section.records) {
            if (section.kind == core::SectionKind::ScriptInfo && std::holds_alternative<core::PropertyRecord>(record))
                ++info;
            if (section.kind == core::SectionKind::Styles && std::holds_alternative<core::StyleRecord>(record))
                ++styles;
        }
    double li = static_cast<double>(info + styles + layout.furiganaStylesAdded());
    std::vector<node_layout::Line> lines;
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
        lines.push_back(layout.line(*l, li, compiled.chars, problems));
        layoutProblems.push_back(std::move(problems));
        run.sources.push_back(l->id);
    }

    std::vector<LineResult> results(lines.size());
    std::vector<std::atomic<std::int64_t>> nanos(compiled.emits.size());
    std::vector<std::atomic<std::size_t>> counts(compiled.emits.size());
    const auto work = [&](std::size_t i) {
        LineResult &r = results[i];
        Problems problems{r.problems, lines[i].source->id};
        for (const std::string &p : layoutProblems[i])
            problems.add({}, p);
        Evaluator eval(compiled, lines[i], name, graph.seed, options.lua, problems);
        for (std::size_t k = 0; k < compiled.emits.size(); ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<Generated> out;
            const std::string key = options.cache ? cacheKey(compiled, static_cast<int>(k), lines[i], name) : "";
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
                g.line.generatedFrom = lines[i].source->id;
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
            run.applied.insert(lines[i].source->id);
        for (Generated &g : results[i].lines) {
            if (layout.addedStyle(g.line.style) && missing.insert(g.line.style).second)
                run.styles.push_back(*layout.addedStyle(g.line.style));
            run.lines.push_back(std::move(g.line));
        }
    }
    for (std::size_t k = 0; k < compiled.emits.size(); ++k)
        run.emits.push_back({compiled.nodes[compiled.emits[k]].graph->id, counts[k].load(),
                             static_cast<double>(nanos[k].load()) / 1e9});
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

}
