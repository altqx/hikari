#pragma once

#include "hikari/json/json.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Number, Time and Alpha are all numbers and connect to one another; Any takes the type a
// node's fields give it.
enum class PortType { Number, Text, Colour, Alpha, Time, Point, Tags, Drawing, Bool, Units, Any };

std::string_view portTypeName(PortType type);
std::optional<PortType> portTypeNamed(std::string_view name);
bool portTypesConnect(PortType from, PortType to);

struct PortSpec {
    std::string name;
    PortType type = PortType::Number;
};

// A field holds a typed constant. An input port with a field of its name reads that field
// while no wire reaches it.
enum class FieldKind { Number, Integer, Text, Bool, Colour, Alpha, Time, Point, Choice, List, Ports, Graph };

struct FieldSpec {
    std::string name;
    FieldKind kind = FieldKind::Number;
    json::Json fallback; // null: the node's own default, which `summary` names
    std::string summary;
    std::vector<std::string> choices;
    std::optional<double> minimum, maximum;
};

struct NodeSpec {
    std::string id;
    std::string category;
    std::string summary;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<FieldSpec> fields;
    int since = 1;
    // The ports depend on the node's fields (Choose, Lua, groups).
    bool dynamicPorts = false;
};

const std::vector<NodeSpec> &nodeCatalogue();
// A built-in node, or an available plugin node.
const NodeSpec *nodeSpec(std::string_view id);

// A node a plugin's manifest declares. Its NodeSpec's id is pluginNodeType(plugin, node) and
// its category the plugin's id; its code is the plugin's, never a Document's.
struct PluginNode {
    std::string plugin;
    std::string pluginName;
    std::string title;
    std::string code;
    NodeSpec spec;
};
std::string pluginNodeType(std::string_view plugin, std::string_view node);
// The plugin id of a plugin node type ("plugin:<id>/<node>"), or nothing for another type.
std::optional<std::string> pluginOfNodeType(std::string_view type);

// Replaces every plugin's nodes. `unavailable` names installed plugins whose nodes don't run
// (turned off, revoked, or signed by a key the user hasn't trusted), by id.
void setPluginNodes(std::vector<PluginNode> nodes, std::map<std::string, std::string> unavailable);
std::vector<PluginNode> pluginNodes();
const PluginNode *pluginNode(std::string_view type);
// Why a plugin node type can't run: its plugin is unavailable or not installed.
std::optional<std::string> pluginNodeProblem(std::string_view type);
const FieldSpec *fieldSpec(const NodeSpec &spec, std::string_view name);

// The JSON Schema of a node's fields.
json::Json nodeFieldsSchema(const NodeSpec &spec);

}
