#include "hikari/application/node_graph_editing.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <set>

namespace hikari::application {

namespace {

const GraphNode *nodeOf(const NodeGraph &graph, std::string_view id)
{
    const auto it = std::ranges::find(graph.nodes, id, &GraphNode::id);
    return it == graph.nodes.end() ? nullptr : &*it;
}

const PortSpec *portOf(const std::vector<PortSpec> &ports, std::string_view name)
{
    const auto it = std::ranges::find(ports, name, &PortSpec::name);
    return it == ports.end() ? nullptr : &*it;
}

const core::LineRecord *lineOf(const core::Document &document, core::LineId id)
{
    for (const core::LineRecord *line : document.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

bool feeds(const GraphWire &wire, const GraphEnd &input)
{
    return wire.to.node == input.node && wire.to.port == input.port;
}

std::string narrow(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

std::u8string wide(std::string_view s)
{
    return std::u8string(s.begin(), s.end());
}

bool namedGraph(const core::Document &document, std::u8string_view name, std::optional<core::LineId> except = {})
{
    return std::ranges::any_of(documentGraphs(document), [&](const DocumentGraph &g) {
        return g.name == name && g.line != except;
    });
}

std::expected<void, CommandRefusal> runOnGraphLine(EditSession &session, core::LineId graphLine, std::string name,
                                                   std::function<bool(core::Document &)> change)
{
    Command command;
    command.name = std::move(name);
    command.expectedRevision = session.revision();
    command.touches = {graphLine};
    command.apply = [&](core::Document &document) {
        const core::LineRecord *line = lineOf(document, graphLine);
        return line && graphNameOf(*line) && change(document);
    };
    return session.run(command);
}

constexpr double kColumn = 240, kGap = 28, kMargin = 40, kHeader = 46, kPortRow = 22;

std::size_t shownOutputs(const NodeGraph &graph, const GraphNode &node, const NodePorts &ports)
{
    if (!foldsUnwiredOutputs(node))
        return ports.outputs.size();
    const auto wired = std::ranges::count_if(ports.outputs, [&](const PortSpec &p) {
        return std::ranges::any_of(graph.wires, [&](const GraphWire &w) { return w.from.node == node.id && w.from.port == p.name; });
    });
    return static_cast<std::size_t>(wired) + 1;
}

double heightOf(const NodeGraph &graph, const GraphNode &node)
{
    const auto ports = nodePorts(node);
    const std::size_t rows = ports ? std::max(ports->inputs.size(), shownOutputs(graph, node, *ports)) : 1;
    return kHeader + kPortRow * static_cast<double>(std::max<std::size_t>(rows, 1));
}

}

std::optional<RefusedWire> wireRefusal(const NodeGraph &graph, const GraphWire &wire)
{
    const GraphNode *from = nodeOf(graph, wire.from.node), *to = nodeOf(graph, wire.to.node);
    if (!from || !to)
        return RefusedWire{WireRefusal::UnknownNode};
    if (from == to)
        return RefusedWire{WireRefusal::SameNode};
    const auto fromPorts = nodePorts(*from), toPorts = nodePorts(*to);
    if (!fromPorts || !toPorts)
        return RefusedWire{WireRefusal::UnknownNode};
    const PortSpec *out = portOf(fromPorts->outputs, wire.from.port);
    if (!out)
        return RefusedWire{WireRefusal::NotAnOutput};
    const PortSpec *in = portOf(toPorts->inputs, wire.to.port);
    if (!in)
        return RefusedWire{WireRefusal::NotAnInput};
    if (!portTypesConnect(out->type, in->type))
        return RefusedWire{WireRefusal::Types, out->type, in->type};
    std::set<std::string> reached;
    std::function<bool(const std::string &)> downstream = [&](const std::string &node) {
        if (node == wire.from.node)
            return true;
        if (!reached.insert(node).second)
            return false;
        return std::ranges::any_of(graph.wires, [&](const GraphWire &w) {
            return w.from.node == node && !feeds(w, wire.to) && downstream(w.to.node);
        });
    };
    if (downstream(wire.to.node))
        return RefusedWire{WireRefusal::Loop};
    return std::nullopt;
}

NodeGraph withWire(NodeGraph graph, const GraphWire &wire)
{
    std::erase_if(graph.wires, [&](const GraphWire &w) { return feeds(w, wire.to); });
    graph.wires.push_back(wire);
    return graph;
}

NodeGraph withoutNode(NodeGraph graph, std::string_view node)
{
    std::erase_if(graph.nodes, [&](const GraphNode &n) { return n.id == node; });
    std::erase_if(graph.wires, [&](const GraphWire &w) { return w.from.node == node || w.to.node == node; });
    return graph;
}

std::optional<NodeGraph> withNodeRenamed(NodeGraph graph, std::string_view from, std::string_view to)
{
    if (!isNodeId(to) || nodeOf(graph, to) || !nodeOf(graph, from))
        return std::nullopt;
    for (GraphNode &n : graph.nodes)
        if (n.id == from)
            n.id = std::string(to);
    for (GraphWire &w : graph.wires) {
        if (w.from.node == from)
            w.from.node = std::string(to);
        if (w.to.node == from)
            w.to.node = std::string(to);
    }
    return graph;
}

std::string freeNodeId(const NodeGraph &graph, std::string_view type)
{
    const auto dot = type.rfind('.');
    std::string base(dot == std::string_view::npos ? type : type.substr(dot + 1));
    std::erase_if(base, [](char c) { return !isNodeId(std::string_view(&c, 1)); });
    if (base.empty())
        base = "node";
    if (!nodeOf(graph, base))
        return base;
    for (int n = 2;; ++n)
        if (std::string id = base + std::to_string(n); !nodeOf(graph, id))
            return id;
}

namespace {

std::optional<NodeGraph> innerOf(const GraphNode &group)
{
    if (group.type != "groups.group")
        return std::nullopt;
    json::Json whole = nodeField(group, "graph");
    if (!whole.isObject())
        return std::nullopt;
    whole["format"] = json::Json(kNodeGraphFormat);
    auto inner = graphFromJson(whole);
    return inner ? std::optional(std::move(*inner)) : std::nullopt;
}

void setInner(GraphNode &group, const NodeGraph &inner)
{
    const json::Json whole = graphJson(inner);
    json::Json fields = json::Json::object({{"nodes", *whole.find("nodes")}, {"wires", *whole.find("wires")}});
    group.fields["graph"] = std::move(fields);
}

GraphNode *nodeIn(NodeGraph &graph, std::string_view id)
{
    const auto it = std::ranges::find(graph.nodes, id, &GraphNode::id);
    return it == graph.nodes.end() ? nullptr : &*it;
}

std::string freePortName(const json::Json &ports, std::string base)
{
    const auto taken = [&](const std::string &name) {
        return std::ranges::any_of(ports.asArray(), [&](const json::Json &p) { return p.find("name")->asString() == name; });
    };
    if (!taken(base))
        return base;
    for (int n = 2;; ++n)
        if (std::string name = base + std::to_string(n); !taken(name))
            return name;
}

}

std::optional<NodeGraph> graphAt(const NodeGraph &root, std::span<const std::string> path)
{
    std::optional<NodeGraph> at = root;
    for (const std::string &group : path) {
        const GraphNode *node = at ? nodeOf(*at, group) : nullptr;
        at = node ? innerOf(*node) : std::nullopt;
    }
    return at;
}

std::optional<NodeGraph> withGraphAt(NodeGraph root, std::span<const std::string> path, const NodeGraph &inner)
{
    if (path.empty())
        return inner;
    GraphNode *group = nodeIn(root, path.front());
    const std::optional<NodeGraph> below = group ? innerOf(*group) : std::nullopt;
    const auto replaced = below ? withGraphAt(*below, path.subspan(1), inner) : std::nullopt;
    if (!replaced)
        return std::nullopt;
    setInner(*group, *replaced);
    return root;
}

std::optional<NodeGraph> groupedNodes(const NodeGraph &graph, std::span<const std::string> nodes, std::string_view groupId)
{
    const std::set<std::string> chosen(nodes.begin(), nodes.end());
    if (chosen.empty() || !isNodeId(groupId) || (nodeOf(graph, groupId) && !chosen.contains(std::string(groupId))))
        return std::nullopt;
    for (const std::string &id : chosen)
        if (!nodeOf(graph, id))
            return std::nullopt;

    NodeGraph inner, outer;
    outer.format = graph.format;
    outer.seed = graph.seed;
    outer.allStyles = graph.allStyles;
    outer.target = graph.target;
    double left = 0, right = 0, top = 0, sumX = 0, sumY = 0;
    bool first = true;
    for (const GraphNode &n : graph.nodes) {
        if (!chosen.contains(n.id)) {
            outer.nodes.push_back(n);
            continue;
        }
        inner.nodes.push_back(n);
        const auto [x, y] = n.at.value_or(std::pair(0.0, 0.0));
        left = first ? x : std::min(left, x);
        right = first ? x : std::max(right, x);
        top = first ? y : std::min(top, y);
        sumX += x;
        sumY += y;
        first = false;
    }
    const std::string inId = freeNodeId(inner, "in"), outId = freeNodeId(inner, "out");
    GraphNode in{inId, "groups.input"}, out{outId, "groups.output"};
    in.fields["ports"] = json::Json::array();
    out.fields["ports"] = json::Json::array();
    in.at = std::pair(left - kColumn, top);
    out.at = std::pair(right + kColumn, top);

    std::map<std::pair<std::string, std::string>, std::string> inputFor, outputFor;
    const auto typeOf = [&](const GraphEnd &output) {
        const GraphNode *n = nodeOf(graph, output.node);
        if (!n)
            return PortType::Number;
        const auto ports = nodePorts(*n);
        const PortSpec *p = ports ? portOf(ports->outputs, output.port) : nullptr;
        return p ? p->type : PortType::Number;
    };
    const auto addPort = [&](GraphNode &io, const GraphEnd &source, std::string base) {
        const std::string name = freePortName(io.fields["ports"], std::move(base));
        io.fields["ports"].push(json::Json::object({{"name", json::Json(name)},
                                                    {"type", json::Json(portTypeName(typeOf(source)))}}));
        return name;
    };
    std::vector<GraphWire> outerWires;
    for (const GraphWire &w : graph.wires) {
        const bool fromIn = chosen.contains(w.from.node), toIn = chosen.contains(w.to.node);
        if (fromIn && toIn) {
            inner.wires.push_back(w);
        } else if (toIn) {
            auto [it, added] = inputFor.try_emplace({w.from.node, w.from.port});
            if (added) {
                it->second = addPort(in, w.from, w.to.port);
                outerWires.push_back({w.from, {std::string(groupId), it->second}});
            }
            inner.wires.push_back({{inId, it->second}, w.to});
        } else if (fromIn) {
            auto [it, added] = outputFor.try_emplace({w.from.node, w.from.port});
            if (added) {
                it->second = addPort(out, w.from, w.from.port);
                inner.wires.push_back({w.from, {outId, it->second}});
            }
            outerWires.push_back({{std::string(groupId), it->second}, w.to});
        } else {
            outer.wires.push_back(w);
        }
    }
    inner.nodes.push_back(std::move(in));
    inner.nodes.push_back(std::move(out));
    GraphNode group{std::string(groupId), "groups.group"};
    const double count = static_cast<double>(chosen.size());
    group.at = std::pair(sumX / count, sumY / count);
    setInner(group, inner);
    outer.nodes.push_back(std::move(group));
    std::ranges::move(outerWires, std::back_inserter(outer.wires));
    return outer;
}

bool foldsUnwiredOutputs(const GraphNode &node)
{
    return node.type == "units.info";
}

NodeGraph laidOutGraph(NodeGraph graph)
{
    std::map<std::string, int> depth;
    std::function<int(const std::string &, int)> depthOf = [&](const std::string &id, int guard) {
        if (const auto known = depth.find(id); known != depth.end())
            return known->second;
        int d = 0;
        if (guard < static_cast<int>(graph.nodes.size()))
            for (const GraphWire &w : graph.wires)
                if (w.to.node == id && nodeOf(graph, w.from.node))
                    d = std::max(d, depthOf(w.from.node, guard + 1) + 1);
        depth[id] = d;
        return d;
    };
    std::map<int, double> bottom;
    for (GraphNode &node : graph.nodes) {
        const int column = depthOf(node.id, 0);
        if (node.at)
            continue;
        double &y = bottom.try_emplace(column, kMargin).first->second;
        node.at = std::pair(kMargin + kColumn * column, y);
        y += heightOf(graph, node) + kGap;
    }
    return graph;
}

std::u8string freeGraphName(const core::Document &document, std::u8string_view base)
{
    std::u8string name(base);
    for (int n = 2; !isGraphName(name) || namedGraph(document, name); ++n) {
        if (!isGraphName(base))
            base = u8"graph";
        name = std::u8string(base) + u8" " + wide(std::to_string(n));
    }
    return name;
}

std::expected<core::LineId, CommandRefusal> addGraph(EditSession &session, std::u8string_view name,
                                                     std::u8string_view style, const NodeGraph &graph,
                                                     std::string commandName)
{
    if (!isGraphName(name) || namedGraph(session.document(), name))
        return std::unexpected(CommandRefusal::Invalid);
    std::optional<core::LineId> added;
    Command command;
    command.name = std::move(commandName);
    command.expectedRevision = session.revision();
    command.apply = [&](core::Document &document) {
        const std::vector<DocumentGraph> graphs = documentGraphs(document);
        const std::vector<const core::LineRecord *> lines = document.lines();
        core::LineRecord line = graphLine(name, style, graph);
        if (!graphs.empty())
            added = document.insertLineAfter(graphs.back().line, std::move(line));
        else if (!lines.empty())
            added = document.insertLineBefore(lines.front()->id, std::move(line));
        else
            added = document.appendLine(std::move(line));
        return added.has_value();
    };
    if (auto ran = session.run(command); !ran)
        return std::unexpected(ran.error());
    return *added;
}

std::expected<void, CommandRefusal> editGraph(EditSession &session, core::LineId graphLine, const NodeGraph &graph)
{
    const std::u8string text = wide(graphText(graph));
    return runOnGraphLine(session, graphLine, "Edit graph", [&](core::Document &document) {
        return lineOf(document, graphLine)->text == text || document.setLineText(graphLine, text);
    });
}

std::expected<void, CommandRefusal> setGraphStyle(EditSession &session, core::LineId graphLine, std::u8string_view style)
{
    return runOnGraphLine(session, graphLine, "Edit graph", [&](core::Document &document) {
        return document.editLine(graphLine, [&](core::LineRecord &line) { line.style = std::u8string(style); });
    });
}

std::expected<void, CommandRefusal> renameGraph(EditSession &session, core::LineId graphLine, std::u8string_view name)
{
    if (!isGraphName(name) || namedGraph(session.document(), name, graphLine))
        return std::unexpected(CommandRefusal::Invalid);
    return runOnGraphLine(session, graphLine, "Rename graph", [&](core::Document &document) {
        const std::u8string old = *graphNameOf(*lineOf(document, graphLine));
        std::vector<core::LineId> generated;
        for (const core::LineRecord *line : document.lines())
            if (isGeneratedBy(*line, old))
                generated.push_back(line->id);
        bool done = document.editLine(graphLine, [&](core::LineRecord &line) { line.effect = u8"graph " + std::u8string(name); });
        for (core::LineId id : generated)
            done = done && document.editLine(id, [&](core::LineRecord &line) { line.effect = generatedEffect(name); });
        return done;
    });
}

std::expected<void, CommandRefusal> deleteGraph(EditSession &session, core::LineId graphLine)
{
    return runOnGraphLine(session, graphLine, "Delete graph", [&](core::Document &document) {
        const std::u8string name = *graphNameOf(*lineOf(document, graphLine));
        document.removeLinesIf([&](const core::LineRecord &line) {
            return line.id == graphLine || isGeneratedBy(line, name);
        });
        return true;
    });
}

}
