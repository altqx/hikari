#pragma once

#include "hikari/application/node_graph.h"

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace hikari::application {

inline constexpr std::string_view kNodeGraphFolder = "node-effects";

// The user's saved graphs, one `<name>.json` each, in the user folder's node-effects folder.
class NodeGraphLibrary {
public:
    explicit NodeGraphLibrary(std::filesystem::path folder);
    std::vector<std::u8string> names() const;
    std::expected<NodeGraph, GraphProblem> load(std::u8string_view name) const;
    // Replaces a graph of the same name.
    bool save(std::u8string_view name, const NodeGraph &graph) const;
    bool remove(std::u8string_view name) const;

private:
    std::filesystem::path fileOf(std::u8string_view name) const;
    std::filesystem::path m_folder;
};

}
