#include "hikari/app/plugin_nodes.h"

#include <cstdint>
#include <fstream>
#include <sstream>

namespace hikari::app {

namespace {

constexpr std::uintmax_t kMaxCodeBytes = 1 << 20;

application::FieldKind fieldKindOf(application::PortType type)
{
    using application::FieldKind;
    using application::PortType;
    switch (type) {
    case PortType::Time:
        return FieldKind::Time;
    case PortType::Alpha:
        return FieldKind::Alpha;
    case PortType::Text:
    case PortType::Tags:
        return FieldKind::Text;
    case PortType::Colour:
        return FieldKind::Colour;
    case PortType::Point:
        return FieldKind::Point;
    case PortType::Bool:
        return FieldKind::Bool;
    default:
        return FieldKind::Number;
    }
}

std::vector<application::PortSpec> portsOf(const std::vector<plugins::NodePort> &ports)
{
    std::vector<application::PortSpec> out;
    for (const plugins::NodePort &p : ports)
        out.push_back({p.name, application::portTypeNamed(p.type).value_or(application::PortType::Number)});
    return out;
}

std::expected<std::string, std::string> readCode(const std::filesystem::path &file)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error)
        return std::unexpected("can't read " + file.filename().string());
    if (size > kMaxCodeBytes)
        return std::unexpected(file.filename().string() + " is larger than 1 MiB");
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    if (!in.good() && !in.eof())
        return std::unexpected("can't read " + file.filename().string());
    return text.str();
}

}

bool pluginNodesRun(const plugins::InstalledPlugin &entry, const plugins::Manifest &manifest)
{
    if (!entry.enabled || entry.revoked)
        return false;
    return !entry.key || manifest.publicKey == entry.key;
}

std::expected<std::vector<application::PluginNode>, std::string> pluginNodesOf(const plugins::Manifest &manifest,
                                                                               const std::filesystem::path &root)
{
    std::vector<application::PluginNode> out;
    for (const plugins::NodeContribution &node : manifest.nodes) {
        auto code = readCode(root / std::filesystem::path(std::u8string(node.file.begin(), node.file.end())));
        if (!code)
            return std::unexpected("the node \"" + node.id + "\": " + code.error());
        application::PluginNode p;
        p.plugin = manifest.id;
        p.pluginName = manifest.name;
        p.title = node.title;
        p.code = std::move(*code);
        p.spec.id = application::pluginNodeType(manifest.id, node.id);
        p.spec.category = manifest.id;
        p.spec.summary = node.summary;
        p.spec.inputs = portsOf(node.inputs);
        p.spec.outputs = portsOf(node.outputs);
        for (std::size_t i = 0; i < node.inputs.size(); ++i)
            if (!node.inputs[i].fallback.isNull())
                p.spec.fields.push_back({node.inputs[i].name, fieldKindOf(p.spec.inputs[i].type), node.inputs[i].fallback,
                                         "Its value while no wire reaches it.", {}, std::nullopt, std::nullopt});
        out.push_back(std::move(p));
    }
    return out;
}

}
