#pragma once

#include "hikari/api/json.h"

#include <string>
#include <vector>

namespace hikari::api {

struct NodePort {
    std::string name;
    std::string type;
};

struct NodeType {
    std::string id; // dotted, e.g. "text.split"
    std::string category;
    std::string summary;
    std::vector<NodePort> inputs;
    std::vector<NodePort> outputs;
    Json params; // a JSON Schema object
    int since = 1;
};

// The node-effect editor's node types, which nodes.catalogue lists.
class NodeCatalogue {
public:
    // Ids are unique; a second add of an id is ignored and returns false.
    bool add(NodeType type);
    const std::vector<NodeType> &types() const { return m_types; }

private:
    std::vector<NodeType> m_types;
};

NodeCatalogue &nodeCatalogue();

}
