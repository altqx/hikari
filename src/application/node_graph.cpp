#include "hikari/application/node_graph.h"

#include "hikari/core/ass_save.h"

#include <algorithm>
#include <map>
#include <set>

namespace hikari::application {

using json::Json;

namespace {

GraphProblem problem(std::string node, std::string port, std::string message)
{
    return {std::move(node), std::move(port), std::move(message)};
}

std::string endText(const GraphEnd &end)
{
    return end.node + "." + end.port;
}

std::optional<GraphEnd> endOf(std::string_view text)
{
    const auto dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size())
        return std::nullopt;
    return GraphEnd{std::string(text.substr(0, dot)), std::string(text.substr(dot + 1))};
}

bool onlyKeys(const Json &object, std::initializer_list<std::string_view> keys, std::string *unknown)
{
    for (const Json::Member &m : object.asObject())
        if (std::find(keys.begin(), keys.end(), m.key) == keys.end()) {
            *unknown = m.key;
            return false;
        }
    return true;
}

std::expected<GraphNode, GraphProblem> nodeOf(const Json &json, std::size_t index)
{
    const std::string where = "nodes[" + std::to_string(index) + "]";
    if (!json.isObject())
        return std::unexpected(problem({}, {}, where + " is not an object"));
    std::string unknown;
    const Json *id = json.find("id");
    const std::string name = id && id->isString() ? id->asString() : where;
    if (!onlyKeys(json, {"id", "type", "fields", "at"}, &unknown))
        return std::unexpected(problem(name, {}, "has an unknown key \"" + unknown + "\""));
    if (!id || !id->isString())
        return std::unexpected(problem({}, {}, where + " has no id"));
    const Json *type = json.find("type");
    if (!type || !type->isString())
        return std::unexpected(problem(name, {}, "has no type"));
    GraphNode node{id->asString(), type->asString(), Json::object(), std::nullopt};
    if (const Json *fields = json.find("fields")) {
        if (!fields->isObject())
            return std::unexpected(problem(name, {}, "fields is not an object"));
        node.fields = *fields;
    }
    if (const Json *at = json.find("at")) {
        if (!at->isArray() || at->asArray().size() != 2 || !at->asArray()[0].isNumber() ||
            !at->asArray()[1].isNumber())
            return std::unexpected(problem(name, {}, "at is not [x, y]"));
        node.at = std::pair{at->asArray()[0].asNumber(), at->asArray()[1].asNumber()};
    }
    return node;
}

std::expected<GraphWire, GraphProblem> wireOf(const Json &json, std::size_t index)
{
    const std::string where = "wires[" + std::to_string(index) + "]";
    std::string unknown;
    if (!json.isObject() || !onlyKeys(json, {"from", "to"}, &unknown))
        return std::unexpected(problem({}, {}, where + " is not {\"from\", \"to\"}"));
    const Json *from = json.find("from");
    const Json *to = json.find("to");
    const auto a = from && from->isString() ? endOf(from->asString()) : std::nullopt;
    const auto b = to && to->isString() ? endOf(to->asString()) : std::nullopt;
    if (!a || !b)
        return std::unexpected(problem({}, {}, where + " needs \"from\" and \"to\" as \"node.port\""));
    return GraphWire{*a, *b};
}

std::expected<std::vector<PortSpec>, GraphProblem> portsOf(const GraphNode &node, std::string_view fieldName)
{
    const Json list = nodeField(node, fieldName);
    std::vector<PortSpec> out;
    if (!list.isArray())
        return std::unexpected(problem(node.id, std::string(fieldName), "is not a list of ports"));
    for (const Json &entry : list.asArray()) {
        const Json *name = entry.isObject() ? entry.find("name") : nullptr;
        const Json *type = entry.isObject() ? entry.find("type") : nullptr;
        const auto portType = type && type->isString() ? portTypeNamed(type->asString()) : std::nullopt;
        if (!name || !name->isString() || name->asString().empty() || !portType || *portType == PortType::Any)
            return std::unexpected(
                problem(node.id, std::string(fieldName), "needs each port as {\"name\", \"type\"} with a known type"));
        for (const PortSpec &known : out)
            if (known.name == name->asString())
                return std::unexpected(
                    problem(node.id, std::string(fieldName), "names the port \"" + known.name + "\" twice"));
        out.push_back({name->asString(), *portType});
    }
    return out;
}

std::expected<NodeGraph, GraphProblem> innerGraph(const GraphNode &group)
{
    const Json inner = nodeField(group, "graph");
    if (!inner.isObject())
        return std::unexpected(problem(group.id, "graph", "is not an object"));
    Json whole = Json::object({{"format", Json(kNodeGraphFormat)}});
    for (const Json::Member &m : inner.asObject()) {
        if (m.key != "nodes" && m.key != "wires")
            return std::unexpected(problem(group.id, "graph", "has an unknown key \"" + m.key + "\""));
        whole[m.key] = m.value;
    }
    auto graph = graphFromJson(whole);
    if (!graph) {
        GraphProblem p = graph.error();
        p.node = p.node.empty() ? group.id : group.id + "/" + p.node;
        return std::unexpected(std::move(p));
    }
    return graph;
}

std::string narrow(std::u8string_view s)
{
    return std::string(s.begin(), s.end());
}

std::u8string wide(std::string_view s)
{
    return std::u8string(s.begin(), s.end());
}

}

int formatOf(const NodeGraph &graph)
{
    return std::max(graph.format, graph.target == GraphTarget::Lines ? 2 : 1);
}

Json graphJson(const NodeGraph &graph)
{
    Json nodes = Json::array();
    for (const GraphNode &n : graph.nodes) {
        Json node = Json::object({{"id", Json(n.id)}, {"type", Json(n.type)}, {"fields", n.fields}});
        if (n.at)
            node["at"] = Json::array({Json(n.at->first), Json(n.at->second)});
        nodes.push(std::move(node));
    }
    Json wires = Json::array();
    for (const GraphWire &w : graph.wires)
        wires.push(Json::object({{"from", Json(endText(w.from))}, {"to", Json(endText(w.to))}}));
    Json out = Json::object({{"format", Json(formatOf(graph))}, {"seed", Json(graph.seed)}});
    if (graph.allStyles)
        out["allStyles"] = Json(true);
    if (graph.target == GraphTarget::Lines)
        out["target"] = Json("lines");
    out["nodes"] = std::move(nodes);
    out["wires"] = std::move(wires);
    return out;
}

std::expected<NodeGraph, GraphProblem> graphFromJson(const Json &json)
{
    if (!json.isObject())
        return std::unexpected(problem({}, {}, "the graph is not a JSON object"));
    std::string unknown;
    if (!onlyKeys(json, {"format", "seed", "allStyles", "target", "nodes", "wires"}, &unknown))
        return std::unexpected(problem({}, {}, "the graph has an unknown key \"" + unknown + "\""));
    NodeGraph graph;
    const Json *format = json.find("format");
    if (!format || !format->isInt() || format->asInt() < 1)
        return std::unexpected(problem({}, {}, "the graph has no format number"));
    if (format->asInt() > kNodeGraphFormat)
        return std::unexpected(problem(
            {}, {}, "the graph needs a newer HikariSub (graph format " + std::to_string(format->asInt()) + ")"));
    graph.format = static_cast<int>(format->asInt());
    if (const Json *seed = json.find("seed")) {
        if (!seed->isInt())
            return std::unexpected(problem({}, {}, "the graph's seed is not an integer"));
        graph.seed = seed->asInt();
    }
    if (const Json *all = json.find("allStyles")) {
        if (!all->isBool())
            return std::unexpected(problem({}, {}, "allStyles is not true or false"));
        graph.allStyles = all->asBool();
    }
    if (const Json *target = json.find("target")) {
        if (graph.format < 2 || !target->isString() ||
            (target->asString() != "karaoke" && target->asString() != "lines"))
            return std::unexpected(problem({}, {}, "target is not \"karaoke\" or \"lines\" in graph format 2"));
        graph.target = target->asString() == "lines" ? GraphTarget::Lines : GraphTarget::Karaoke;
    }
    const Json *nodes = json.find("nodes");
    if (!nodes || !nodes->isArray())
        return std::unexpected(problem({}, {}, "the graph has no node list"));
    for (std::size_t i = 0; i < nodes->asArray().size(); ++i) {
        auto node = nodeOf(nodes->asArray()[i], i);
        if (!node)
            return std::unexpected(node.error());
        graph.nodes.push_back(std::move(*node));
    }
    if (const Json *wires = json.find("wires")) {
        if (!wires->isArray())
            return std::unexpected(problem({}, {}, "wires is not a list"));
        for (std::size_t i = 0; i < wires->asArray().size(); ++i) {
            auto wire = wireOf(wires->asArray()[i], i);
            if (!wire)
                return std::unexpected(wire.error());
            graph.wires.push_back(std::move(*wire));
        }
    }
    return graph;
}

std::string graphText(const NodeGraph &graph)
{
    return graphJson(graph).dump();
}

std::expected<NodeGraph, GraphProblem> parseGraph(std::string_view text)
{
    auto json = Json::parse(text);
    if (!json)
        return std::unexpected(problem({}, {},
                                       "the graph is not JSON (" + json.error().message + " at byte " +
                                           std::to_string(json.error().offset) + ")"));
    return graphFromJson(*json);
}

Json nodeField(const GraphNode &node, std::string_view name)
{
    if (const Json *value = node.fields.isObject() ? node.fields.find(name) : nullptr)
        return *value;
    if (const NodeSpec *spec = nodeSpec(node.type))
        if (const FieldSpec *f = fieldSpec(*spec, name))
            return f->fallback;
    return Json();
}

std::expected<NodePorts, GraphProblem> nodePorts(const GraphNode &node)
{
    const NodeSpec *spec = nodeSpec(node.type);
    if (!spec)
        return std::unexpected(
            problem(node.id, {}, pluginNodeProblem(node.type).value_or("has the unknown type \"" + node.type + "\"")));
    NodePorts ports{spec->inputs, spec->outputs};
    if (!spec->dynamicPorts)
        return ports;
    if (node.type == "values.choose") {
        const Json type = nodeField(node, "type");
        static const std::map<std::string, PortType> kTypes{{"number", PortType::Number},
                                                            {"text", PortType::Text},
                                                            {"colour", PortType::Colour},
                                                            {"alpha", PortType::Alpha},
                                                            {"time", PortType::Time}};
        const auto it = type.isString() ? kTypes.find(type.asString()) : kTypes.end();
        if (it == kTypes.end())
            return std::unexpected(problem(node.id, "type", "is not a type Choose offers"));
        ports.outputs = {{"value", it->second}};
    } else if (node.type == "lua.code") {
        auto in = portsOf(node, "inputs");
        if (!in)
            return std::unexpected(in.error());
        auto out = portsOf(node, "outputs");
        if (!out)
            return std::unexpected(out.error());
        ports = {std::move(*in), std::move(*out)};
    } else if (node.type == "groups.input") {
        auto out = portsOf(node, "ports");
        if (!out)
            return std::unexpected(out.error());
        ports.outputs = std::move(*out);
    } else if (node.type == "groups.output") {
        auto in = portsOf(node, "ports");
        if (!in)
            return std::unexpected(in.error());
        ports.inputs = std::move(*in);
    } else if (node.type == "groups.group") {
        auto inner = innerGraph(node);
        if (!inner)
            return std::unexpected(inner.error());
        for (const GraphNode &n : inner->nodes) {
            if (n.type != "groups.input" && n.type != "groups.output")
                continue;
            auto relay = nodePorts(n);
            if (!relay) {
                GraphProblem p = relay.error();
                p.node = node.id + "/" + p.node;
                return std::unexpected(std::move(p));
            }
            if (n.type == "groups.input")
                ports.inputs = relay->outputs;
            else
                ports.outputs = relay->inputs;
        }
    }
    return ports;
}

namespace {

struct Flat {
    std::vector<GraphNode> nodes;
    std::vector<GraphWire> wires;
    std::set<std::string> relays;
};

std::optional<GraphProblem> flattenInto(const NodeGraph &graph, const std::string &prefix, Flat &out)
{
    struct Relays {
        std::string input, output;
    };
    std::map<std::string, Relays> groups;
    for (const GraphNode &node : graph.nodes) {
        const std::string id = prefix + node.id;
        if (node.type != "groups.group") {
            GraphNode copy = node;
            copy.id = id;
            if (node.type == "groups.input" || node.type == "groups.output")
                out.relays.insert(id);
            out.nodes.push_back(std::move(copy));
            continue;
        }
        auto inner = innerGraph(node);
        if (!inner) {
            GraphProblem p = inner.error();
            p.node = prefix + p.node;
            return p;
        }
        Relays relays;
        for (const GraphNode &n : inner->nodes) {
            if (n.type == "groups.input")
                relays.input = id + "/" + n.id;
            else if (n.type == "groups.output")
                relays.output = id + "/" + n.id;
        }
        groups[id] = relays;
        if (auto p = flattenInto(*inner, id + "/", out))
            return p;
    }
    for (const GraphWire &wire : graph.wires) {
        GraphWire w{{prefix + wire.from.node, wire.from.port}, {prefix + wire.to.node, wire.to.port}};
        if (const auto g = groups.find(w.to.node); g != groups.end()) {
            if (g->second.input.empty())
                continue;
            w.to.node = g->second.input;
        }
        if (const auto g = groups.find(w.from.node); g != groups.end()) {
            if (g->second.output.empty())
                continue;
            w.from.node = g->second.output;
        }
        out.wires.push_back(std::move(w));
    }
    return std::nullopt;
}

}

std::expected<NodeGraph, GraphProblem> flattenGraph(const NodeGraph &graph)
{
    Flat flat;
    if (auto p = flattenInto(graph, {}, flat))
        return std::unexpected(std::move(*p));
    const auto feeding = [&](const GraphEnd &input) -> std::optional<GraphEnd> {
        for (const GraphWire &w : flat.wires)
            if (w.to.node == input.node && w.to.port == input.port)
                return w.from;
        return std::nullopt;
    };
    NodeGraph out;
    out.format = graph.format;
    out.seed = graph.seed;
    out.allStyles = graph.allStyles;
    out.target = graph.target;
    for (GraphNode &node : flat.nodes)
        if (!flat.relays.contains(node.id))
            out.nodes.push_back(std::move(node));
    for (const GraphWire &wire : flat.wires) {
        if (flat.relays.contains(wire.to.node))
            continue;
        std::optional<GraphEnd> from = wire.from;
        for (std::size_t hops = 0; from && flat.relays.contains(from->node); ++hops) {
            if (hops > flat.wires.size())
                return std::unexpected(problem(from->node, from->port, "is wired in a loop through groups"));
            from = feeding(*from);
        }
        if (from)
            out.wires.push_back({*from, wire.to});
    }
    return out;
}

std::optional<std::u8string> graphNameOf(const core::LineRecord &line)
{
    constexpr std::u8string_view kPrefix = u8"graph ";
    if (!line.comment || !line.effect.starts_with(kPrefix))
        return std::nullopt;
    std::u8string_view name = std::u8string_view(line.effect).substr(kPrefix.size());
    while (!name.empty() && name.front() == u8' ')
        name.remove_prefix(1);
    while (!name.empty() && name.back() == u8' ')
        name.remove_suffix(1);
    if (!isGraphName(name))
        return std::nullopt;
    return std::u8string(name);
}

bool isGraphName(std::u8string_view name)
{
    if (name.empty() || name.front() == u8' ' || name.back() == u8' ' || name.back() == u8'.')
        return false;
    if (name.find(u8"karaoke") != std::u8string_view::npos || name.find(u8"Karaoke") != std::u8string_view::npos)
        return false;
    constexpr std::u8string_view kNotInFileNames = u8",/\\:*?\"<>|";
    return std::ranges::none_of(name, [&](char8_t c) { return kNotInFileNames.contains(c) || c < 0x20 || c == 0x7F; });
}

std::u8string generatedEffect(std::u8string_view graphName)
{
    return u8"fx " + std::u8string(graphName);
}

bool isGeneratedBy(const core::LineRecord &line, std::u8string_view graphName)
{
    return !line.comment && line.effect == generatedEffect(graphName);
}

core::LineRecord graphLine(std::u8string_view name, std::u8string_view style, const NodeGraph &graph)
{
    core::LineRecord line;
    line.comment = true;
    line.layer = core::IntField{u8"0", 0};
    line.start = core::TimeField{core::legacy::assTimeText(0), core::DocumentTime(0)};
    line.end = line.start;
    line.style = std::u8string(style);
    line.marginLeft = core::IntField{u8"0", 0};
    line.marginRight = line.marginLeft;
    line.marginVertical = line.marginLeft;
    line.effect = u8"graph " + std::u8string(name);
    line.text = wide(graphText(graph));
    return line;
}

std::vector<DocumentGraph> documentGraphs(const core::Document &document)
{
    std::vector<DocumentGraph> out;
    for (const core::LineRecord *line : document.lines())
        if (auto name = graphNameOf(*line))
            out.push_back({line->id, std::move(*name), line->style, parseGraph(narrow(line->text))});
    return out;
}

}
