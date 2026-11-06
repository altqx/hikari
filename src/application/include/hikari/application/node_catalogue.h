#pragma once

#include "hikari/json/json.h"

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
const NodeSpec *nodeSpec(std::string_view id);
const FieldSpec *fieldSpec(const NodeSpec &spec, std::string_view name);

// The JSON Schema of a node's fields.
json::Json nodeFieldsSchema(const NodeSpec &spec);

}
