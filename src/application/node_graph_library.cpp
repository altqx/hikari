#include "hikari/application/node_graph_library.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace hikari::application {

NodeGraphLibrary::NodeGraphLibrary(std::filesystem::path folder) : m_folder(std::move(folder)) {}

std::filesystem::path NodeGraphLibrary::fileOf(std::u8string_view name) const
{
    return m_folder / std::filesystem::path(std::u8string(name) + u8".json");
}

std::vector<std::u8string> NodeGraphLibrary::names() const
{
    std::vector<std::u8string> out;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(m_folder, ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json" &&
            isGraphName(entry.path().stem().u8string()))
            out.push_back(entry.path().stem().u8string());
    std::ranges::sort(out);
    return out;
}

std::expected<NodeGraph, GraphProblem> NodeGraphLibrary::load(std::u8string_view name) const
{
    std::ifstream in(fileOf(name), std::ios::binary);
    if (!isGraphName(name) || !in)
        return std::unexpected(GraphProblem{{}, {}, "there is no saved graph of that name"});
    std::stringstream text;
    text << in.rdbuf();
    return parseGraph(text.str());
}

bool NodeGraphLibrary::save(std::u8string_view name, const NodeGraph &graph) const
{
    if (!isGraphName(name))
        return false;
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    const std::filesystem::path file = fileOf(name);
    std::filesystem::path partial = file;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        out << graphJson(graph).dump(2) << '\n';
        if (!out.flush())
            return false;
    }
    std::filesystem::rename(partial, file, ec);
    if (ec)
        std::filesystem::remove(partial, ec);
    return !ec;
}

bool NodeGraphLibrary::remove(std::u8string_view name) const
{
    std::error_code ec;
    return isGraphName(name) && std::filesystem::remove(fileOf(name), ec);
}

}
