#include "operations.h"

#include "hikari/api/nodes.h"
#include "hikari/api/reference.h"
#include "hikari/application/node_catalogue.h"
#include "hikari/application/node_graph.h"

#include <array>

namespace hikari::api {

bool NodeCatalogue::add(NodeType type)
{
    for (const NodeType &known : m_types)
        if (known.id == type.id)
            return false;
    m_types.push_back(std::move(type));
    return true;
}

namespace {

std::vector<NodePort> nodePorts(const std::vector<application::PortSpec> &ports)
{
    std::vector<NodePort> out;
    for (const application::PortSpec &port : ports)
        out.push_back({port.name, std::string(application::portTypeName(port.type))});
    return out;
}

NodeCatalogue engineNodes()
{
    NodeCatalogue catalogue;
    for (const application::NodeSpec &spec : application::nodeCatalogue())
        catalogue.add({spec.id, spec.category, spec.summary, nodePorts(spec.inputs), nodePorts(spec.outputs),
                       application::nodeFieldsSchema(spec), spec.since});
    return catalogue;
}


}

Json detail::problemsJson(const std::vector<application::GraphProblem> &problems)
{
    Json out = Json::array();
    for (const application::GraphProblem &p : problems) {
        Json entry = Json::object();
        if (!p.node.empty())
            entry["node"] = Json(p.node);
        if (!p.port.empty())
            entry["port"] = Json(p.port);
        entry["message"] = Json(p.message);
        out.push(std::move(entry));
    }
    return out;
}

NodeCatalogue &nodeCatalogue()
{
    static NodeCatalogue catalogue = engineNodes();
    return catalogue;
}

namespace {

Json portsJson(const std::vector<NodePort> &ports)
{
    Json out = Json::array();
    for (const NodePort &port : ports)
        out.push(Json::object({{"name", Json(port.name)}, {"type", Json(port.type)}}));
    return out;
}

Json nodesJson()
{
    Json out = Json::array();
    for (const NodeType &type : nodeCatalogue().types())
        out.push(Json::object({{"id", Json(type.id)},
                               {"category", Json(type.category)},
                               {"summary", Json(type.summary)},
                               {"inputs", portsJson(type.inputs)},
                               {"outputs", portsJson(type.outputs)},
                               {"params", type.params},
                               {"since", Json(type.since)}}));
    return out;
}

}

Json apiReference()
{
    const Registry &registry = Api::registry();
    Json operations = Json::array();
    for (const Operation &op : registry.operations()) {
        Json entry = Json::object({{"name", Json(op.name)},
                                   {"access", Json(accessName(op.access))},
                                   {"summary", Json(op.summary)},
                                   {"since", Json(op.since)}});
        if (op.deprecatedIn)
            entry["deprecatedIn"] = Json(*op.deprecatedIn);
        entry["params"] = op.params;
        entry["result"] = op.result;
        operations.push(std::move(entry));
    }
    Json events = Json::array();
    for (const EventType &event : registry.events())
        events.push(Json::object({{"name", Json(event.name)},
                                  {"summary", Json(event.summary)},
                                  {"since", Json(event.since)},
                                  {"data", event.data}}));
    return Json::object({{"schema", Json(1)},
                         {"version", Json(kApiVersion)},
                         {"$defs", registry.defs()},
                         {"operations", std::move(operations)},
                         {"events", std::move(events)},
                         {"nodes", nodesJson()}});
}

namespace detail {

void addOperation(Registry &registry, Access access, std::string name, std::string summary, std::string_view params,
                  std::string_view result, Handler handler, int since)
{
    registry.add(Operation{std::move(name), access, std::move(summary), schema(params), schema(result), since, std::nullopt,
                           std::move(handler)});
}

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(std::string_view bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, bytes.size() - i);
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 3; ++k)
            v = v << 8 | (k < n ? static_cast<unsigned char>(bytes[i + k]) : 0u);
        for (std::size_t k = 0; k < 4; ++k)
            out += k <= n ? kBase64[(v >> (18 - 6 * k)) & 63] : '=';
    }
    return out;
}

std::optional<std::string> fromBase64(std::string_view text)
{
    if (text.size() % 4 != 0)
        return std::nullopt;
    std::array<int, 256> value;
    value.fill(-1);
    for (std::size_t i = 0; i < kBase64.size(); ++i)
        value[static_cast<unsigned char>(kBase64[i])] = int(i);
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const int pad = last ? int(text[i + 3] == '=') + int(text[i + 2] == '=' && text[i + 3] == '=') : 0;
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const int digit = k >= 4 - std::size_t(pad) ? 0 : value[static_cast<unsigned char>(text[i + k])];
            if (digit < 0)
                return std::nullopt;
            v = v << 6 | std::uint32_t(digit);
        }
        for (int k = 0; k < 3 - pad; ++k)
            out += char((v >> (16 - 8 * k)) & 0xFF);
    }
    return out;
}

void addNodeOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "nodes.catalogue",
                 "The node types the node-effect editor offers, with their ports and parameters.",
                 R"({"type": "object", "properties": {"category": {"type": "string"}}, "additionalProperties": false})",
                 R"({"type": "object", "properties": {"nodes": {"type": "array", "items": {"type": "object", "properties": {
                     "id": {"type": "string"}, "category": {"type": "string"}, "summary": {"type": "string"},
                     "inputs": {"type": "array", "items": {"$ref": "#/$defs/NodePort"}},
                     "outputs": {"type": "array", "items": {"$ref": "#/$defs/NodePort"}},
                     "params": {"type": "object"}, "since": {"type": "integer", "minimum": 1}},
                     "required": ["id", "category", "summary", "inputs", "outputs", "params", "since"],
                     "additionalProperties": false}}}, "required": ["nodes"], "additionalProperties": false})",
                 now([](const Context &, const Json &params) -> Result {
                     const Json *category = params.find("category");
                     Json nodes = Json::array();
                     for (const Json &node : nodesJson().asArray())
                         if (!category || *node.find("category") == *category)
                             nodes.push(node);
                     return Json::object({{"nodes", std::move(nodes)}});
                 }));
    addOperation(registry, Access::Read, "nodes.validate",
                 "Checks a node-effect graph; each problem names its node and port or field.",
                 R"({"type": "object", "properties": {"graph": {"type": "object"}}, "required": ["graph"],
                     "additionalProperties": false})",
                 R"({"type": "object", "properties": {"valid": {"type": "boolean"}, "problems": {"type": "array",
                     "items": {"type": "object", "properties": {"node": {"type": "string"}, "port": {"type": "string"},
                     "message": {"type": "string"}}, "required": ["message"], "additionalProperties": false}}},
                     "required": ["valid", "problems"], "additionalProperties": false})",
                 now([](const Context &, const Json &params) -> Result {
                     const auto graph = application::graphFromJson(*params.find("graph"));
                     const std::vector<application::GraphProblem> problems =
                         graph ? application::validateGraph(*graph) : std::vector{graph.error()};
                     return Json::object({{"valid", Json(problems.empty())}, {"problems", problemsJson(problems)}});
                 }));
}

}

}
