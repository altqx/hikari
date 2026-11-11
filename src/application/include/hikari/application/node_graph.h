#pragma once

#include "hikari/application/node_catalogue.h"
#include "hikari/core/document.h"
#include "hikari/json/json.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// The newest graph format this HikariSub reads; it grows add-only, as the Hikari API does.
inline constexpr int kNodeGraphFormat = 2;

// Letters, digits, '_' and '-'.
bool isNodeId(std::string_view id);

struct GraphNode {
    std::string id; // isNodeId
    std::string type;
    json::Json fields = json::Json::object();
    // Where the canvas draws it; the engine ignores it.
    std::optional<std::pair<double, double>> at;
};

struct GraphEnd {
    std::string node, port;
};

struct GraphWire {
    GraphEnd from, to;
};

// Karaoke: the karaoke Lines of the graph Line's Style. Lines: the Lines a run names (format 2).
enum class GraphTarget { Karaoke, Lines };

struct NodeGraph {
    int format = 1;
    std::int64_t seed = 0;
    // The karaoke Lines of every Style, not only the graph Line's.
    bool allStyles = false;
    GraphTarget target = GraphTarget::Karaoke;
    std::vector<GraphNode> nodes;
    std::vector<GraphWire> wires;
};

// Empty `node`: the graph as a whole.
struct GraphProblem {
    std::string node;
    std::string port; // a port or field name, or empty
    std::string message;
    friend bool operator==(const GraphProblem &, const GraphProblem &) = default;
};

// The format a graph is written in: 2 once it targets Lines.
int formatOf(const NodeGraph &graph);
json::Json graphJson(const NodeGraph &graph);
std::expected<NodeGraph, GraphProblem> graphFromJson(const json::Json &json);
std::string graphText(const NodeGraph &graph);
std::expected<NodeGraph, GraphProblem> parseGraph(std::string_view text);

// A node's ports, those of Choose, Lua and groups resolved from its fields.
struct NodePorts {
    std::vector<PortSpec> inputs, outputs;
};
std::expected<NodePorts, GraphProblem> nodePorts(const GraphNode &node);

// The field's value, or its catalogue fallback; null for an unknown field.
json::Json nodeField(const GraphNode &node, std::string_view name);

// Every group's nodes in place of the group, ids prefixed "<group id>/", and their wires joined
// through the groups' inputs and outputs.
std::expected<NodeGraph, GraphProblem> flattenGraph(const NodeGraph &graph);

// Everything wrong with the graph, each at its node; empty when it can run.
std::vector<GraphProblem> validateGraph(const NodeGraph &graph);

// A graph is stored as a Comment whose Effect is "graph <name>" and whose text is its JSON.
// Its Style picks the karaoke Lines it applies to, as a template Line's does.
std::optional<std::u8string> graphNameOf(const core::LineRecord &line);
// Not empty, no comma, control character or character a file name cannot hold, no space at
// either end nor a dot at the end, and no "karaoke" or "Karaoke": the templater would take Lines
// with such an Effect for karaoke Lines.
bool isGraphName(std::u8string_view name);
// "fx <name>": the Effect of the Lines a graph generates.
std::u8string generatedEffect(std::u8string_view graphName);
bool isGeneratedBy(const core::LineRecord &line, std::u8string_view graphName);
core::LineRecord graphLine(std::u8string_view name, std::u8string_view style, const NodeGraph &graph);

struct DocumentGraph {
    core::LineId line;
    std::u8string name;
    std::u8string style;
    std::expected<NodeGraph, GraphProblem> graph;
};
std::vector<DocumentGraph> documentGraphs(const core::Document &document);

}
