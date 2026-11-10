#include "hikari/application/node_graph.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace hikari::application {

using json::Json;

bool isNodeId(std::string_view id)
{
    return !id.empty() && std::ranges::all_of(id, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

namespace {

bool isColour(const Json &value)
{
    if (!value.isString() || value.asString().size() != 7 || value.asString()[0] != '#')
        return false;
    return std::ranges::all_of(value.asString().substr(1), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

bool isInteger(const Json &value)
{
    return value.isInt() || (value.isNumber() && std::isfinite(value.asNumber()) &&
                             value.asNumber() == std::floor(value.asNumber()));
}

std::optional<std::string> fieldProblem(const FieldSpec &f, const Json &value)
{
    if (value.isNull() && f.fallback.isNull() && (f.kind == FieldKind::Time || f.kind == FieldKind::Number))
        return std::nullopt;
    const auto ranged = [&](double v) -> std::optional<std::string> {
        if (!std::isfinite(v))
            return "is not a finite number";
        if (f.minimum && v < *f.minimum)
            return "is below its minimum";
        if (f.maximum && v > *f.maximum)
            return "is above its maximum";
        return std::nullopt;
    };
    switch (f.kind) {
    case FieldKind::Number:
    case FieldKind::Time:
        if (!value.isNumber())
            return "is not a number";
        return ranged(value.asNumber());
    case FieldKind::Integer:
        if (!isInteger(value))
            return "is not a whole number";
        return ranged(value.asNumber());
    case FieldKind::Alpha:
        if (!isInteger(value) || value.asNumber() < 0 || value.asNumber() > 255)
            return "is not an alpha from 0 to 255";
        return std::nullopt;
    case FieldKind::Text:
        return value.isString() ? std::nullopt : std::optional<std::string>("is not text");
    case FieldKind::Bool:
        return value.isBool() ? std::nullopt : std::optional<std::string>("is not true or false");
    case FieldKind::Colour:
        return isColour(value) ? std::nullopt : std::optional<std::string>("is not a colour as #RRGGBB");
    case FieldKind::Point:
        if (!value.isArray() || value.asArray().size() != 2 || !value.asArray()[0].isNumber() ||
            !value.asArray()[1].isNumber())
            return "is not a point as [x, y]";
        return std::nullopt;
    case FieldKind::Choice:
        if (!value.isString() || std::ranges::find(f.choices, value.asString()) == f.choices.end())
            return "is not one of its choices";
        return std::nullopt;
    case FieldKind::List:
    case FieldKind::Ports:
        return value.isArray() ? std::nullopt : std::optional<std::string>("is not a list");
    case FieldKind::Graph:
        return value.isObject() ? std::nullopt : std::optional<std::string>("is not a graph object");
    }
    return std::nullopt;
}

std::optional<std::string> listProblem(const GraphNode &node)
{
    if (node.type == "colours.gradient") {
        const Json stops = nodeField(node, "colours");
        if (stops.asArray().empty())
            return "has no colour";
        for (const Json &stop : stops.asArray())
            if (!isColour(stop))
                return "holds a value that is not a colour as #RRGGBB";
        return std::nullopt;
    }
    if (node.type != "values.choose")
        return std::nullopt;
    const Json values = nodeField(node, "values");
    const Json type = nodeField(node, "type");
    if (values.asArray().empty())
        return "has no value";
    for (const Json &v : values.asArray()) {
        const std::string &t = type.isString() ? type.asString() : std::string();
        const bool ok = t == "text"     ? v.isString()
                        : t == "colour" ? isColour(v)
                        : t == "alpha"  ? isInteger(v) && v.asNumber() >= 0 && v.asNumber() <= 255
                                        : v.isNumber() && std::isfinite(v.asNumber());
        if (!ok)
            return "holds a value that is not of its type";
    }
    return std::nullopt;
}

constexpr std::string_view kRequiredUnits[] = {"units.repeat", "units.frames", "text.join", "output.emit"};
constexpr std::string_view kLuaTypes[] = {"Number", "Text", "Colour", "Alpha", "Time", "Point", "Bool", "Tags"};

struct Level {
    std::vector<GraphProblem> &problems;

    void add(std::string node, std::string port, std::string message)
    {
        problems.push_back({std::move(node), std::move(port), std::move(message)});
    }

    void check(const NodeGraph &graph, const std::string &prefix, bool inGroup)
    {
        std::map<std::string, const GraphNode *> byId;
        std::map<std::string, NodePorts> ports;
        int inputs = 0, outputs = 0;
        for (const GraphNode &node : graph.nodes) {
            const std::string id = prefix + node.id;
            if (!isNodeId(node.id)) {
                add(id, {}, "has an id that is not letters, digits, '_' and '-'");
                continue;
            }
            if (!byId.emplace(node.id, &node).second) {
                add(id, {}, "has an id another node has");
                continue;
            }
            const NodeSpec *spec = nodeSpec(node.type);
            if (!spec) {
                add(id, {}, "has the unknown type \"" + node.type + "\"");
                continue;
            }
            bool fieldsOk = true;
            for (const Json::Member &m : node.fields.asObject()) {
                const FieldSpec *f = fieldSpec(*spec, m.key);
                if (!f) {
                    add(id, m.key, "is not a field of " + spec->id);
                    fieldsOk = false;
                } else if (auto why = fieldProblem(*f, m.value)) {
                    add(id, m.key, *why);
                    fieldsOk = false;
                }
            }
            if (!fieldsOk)
                continue;
            if (auto why = listProblem(node))
                add(id, node.type == "values.choose" ? "values" : "colours", *why);
            if (node.type == "groups.input" || node.type == "groups.output") {
                if (!inGroup)
                    add(id, {}, "belongs inside a group");
                if ((node.type == "groups.input" ? ++inputs : ++outputs) > 1)
                    add(id, {}, "is a second " + spec->id + " in one group");
            }
            if (node.type == "lua.code" && nodeField(node, "code").asString().empty())
                add(id, "code", "is empty");
            auto resolved = nodePorts(node);
            if (!resolved) {
                GraphProblem p = resolved.error();
                p.node = prefix + p.node;
                problems.push_back(std::move(p));
                continue;
            }
            if (node.type == "lua.code")
                for (const auto &list : {resolved->inputs, resolved->outputs})
                    for (const PortSpec &p : list)
                        if (std::ranges::find(kLuaTypes, portTypeName(p.type)) == std::end(kLuaTypes))
                            add(id, p.name, "is of a type Lua cannot take");
            if (node.type == "groups.group") {
                const std::size_t before = problems.size();
                Json inner = nodeField(node, "graph");
                Json whole = Json::object({{"format", Json(kNodeGraphFormat)}});
                for (const Json::Member &m : inner.asObject())
                    whole[m.key] = m.value;
                auto graphInside = graphFromJson(whole);
                if (!graphInside) {
                    GraphProblem p = graphInside.error();
                    p.node = p.node.empty() ? id : id + "/" + p.node;
                    problems.push_back(std::move(p));
                } else {
                    check(*graphInside, id + "/", true);
                }
                if (problems.size() != before)
                    continue;
            }
            ports.emplace(node.id, std::move(*resolved));
        }

        std::set<std::pair<std::string, std::string>> fed;
        for (const GraphWire &w : graph.wires) {
            const std::string to = prefix + w.to.node;
            const auto fromNode = ports.find(w.from.node);
            const auto toNode = ports.find(w.to.node);
            if (!byId.contains(w.from.node)) {
                add(to, w.to.port, "is wired from the unknown node \"" + prefix + w.from.node + "\"");
                continue;
            }
            if (!byId.contains(w.to.node)) {
                add(prefix + w.from.node, w.from.port, "is wired to the unknown node \"" + to + "\"");
                continue;
            }
            if (fromNode == ports.end() || toNode == ports.end()) {
                fed.emplace(w.to.node, w.to.port);
                continue;
            }
            const auto out = std::ranges::find(fromNode->second.outputs, w.from.port, &PortSpec::name);
            const auto in = std::ranges::find(toNode->second.inputs, w.to.port, &PortSpec::name);
            if (out == fromNode->second.outputs.end()) {
                add(prefix + w.from.node, w.from.port, "is not an output");
                continue;
            }
            if (in == toNode->second.inputs.end()) {
                add(to, w.to.port, "is not an input");
                continue;
            }
            if (!fed.emplace(w.to.node, w.to.port).second)
                add(to, w.to.port, "has a second wire");
            if (!portTypesConnect(out->type, in->type))
                add(to, w.to.port,
                    "takes " + std::string(portTypeName(in->type)) + ", not " + std::string(portTypeName(out->type)));
        }
        for (const GraphNode &node : graph.nodes)
            if (std::ranges::find(kRequiredUnits, node.type) != std::end(kRequiredUnits) && ports.contains(node.id) &&
                !fed.contains({node.id, "units"}))
                add(prefix + node.id, "units", "needs units wired in");
    }
};

constexpr std::string_view kDomainNodes[] = {"units.for_each", "units.repeat", "units.frames"};

bool isDomainNode(const GraphNode &node)
{
    return std::ranges::find(kDomainNodes, node.type) != std::end(kDomainNodes);
}

struct Flow {
    const NodeGraph &graph;
    std::vector<GraphProblem> &problems;
    std::map<std::string, const GraphNode *> byId;

    std::optional<GraphEnd> feeding(const std::string &node, std::string_view port) const
    {
        for (const GraphWire &w : graph.wires)
            if (w.to.node == node && w.to.port == port)
                return w.from;
        return std::nullopt;
    }

    std::set<std::string> chainFrom(const std::string &node) const
    {
        std::set<std::string> chain;
        for (std::optional<GraphEnd> at = feeding(node, "units"); at && chain.insert(at->node).second;)
            at = feeding(at->node, "units");
        return chain;
    }

    bool cycles()
    {
        std::map<std::string, int> state;
        std::function<bool(const std::string &)> visit = [&](const std::string &id) {
            int &s = state[id];
            if (s == 1) {
                problems.push_back({id, {}, "is wired into a loop"});
                return true;
            }
            if (s == 2)
                return false;
            s = 1;
            for (const GraphWire &w : graph.wires)
                if (w.to.node == id && visit(w.from.node))
                    return true;
            state[id] = 2;
            return false;
        };
        for (const GraphNode &node : graph.nodes)
            if (visit(node.id))
                return true;
        return false;
    }

    void reads(const std::string &consumer, std::string_view port, const std::set<std::string> &domain,
               std::set<std::pair<std::string, std::string>> &seen)
    {
        const auto from = feeding(consumer, port);
        if (!from || from->port == "units" || !seen.emplace(consumer, std::string(port)).second)
            return;
        const GraphNode *source = byId.at(from->node);
        if (isDomainNode(*source)) {
            if (!domain.contains(source->id))
                problems.push_back({consumer, std::string(port),
                                    "reads " + source->id + "." + from->port + " outside its units"});
            return;
        }
        if (source->type == "text.join")
            return;
        if (auto ports = nodePorts(*source))
            for (const PortSpec &in : ports->inputs)
                reads(source->id, in.name, domain, seen);
    }

    void scopes()
    {
        for (const GraphNode &node : graph.nodes) {
            const bool own = node.type == "output.emit" || node.type == "text.join";
            if (!own && node.type != "units.repeat" && node.type != "units.frames")
                continue;
            std::set<std::string> domain = chainFrom(node.id);
            const auto ports = nodePorts(node);
            std::set<std::pair<std::string, std::string>> seen;
            for (const PortSpec &in : ports->inputs)
                if (in.name != "units")
                    reads(node.id, in.name, domain, seen);
        }
    }
};

}

std::vector<GraphProblem> validateGraph(const NodeGraph &graph)
{
    std::vector<GraphProblem> problems;
    if (graph.format < 1 || graph.format > kNodeGraphFormat)
        problems.push_back({{}, {}, "the graph format " + std::to_string(graph.format) + " is not one this HikariSub reads"});
    Level{problems}.check(graph, {}, false);
    if (!problems.empty())
        return problems;
    auto flat = flattenGraph(graph);
    if (!flat) {
        problems.push_back(flat.error());
        return problems;
    }
    Flow flow{*flat, problems, {}};
    for (const GraphNode &node : flat->nodes)
        flow.byId.emplace(node.id, &node);
    if (flow.cycles())
        return problems;
    flow.scopes();
    if (std::ranges::none_of(flat->nodes, [](const GraphNode &n) { return n.type == "output.emit"; }))
        problems.push_back({{}, {}, "the graph has no Emit node, so it writes no Line"});
    return problems;
}

}
