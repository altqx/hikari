#include "hikari/app/plugin_content.h"

#include "hikari/app/plugin_nodes.h"

namespace hikari::app {

namespace {

std::u8string u8(const std::string &s)
{
    return {s.begin(), s.end()};
}

void add(std::vector<application::PluginContentFile> &into, const std::vector<plugins::ContentFile> &files,
         const std::string &id, const plugins::Manifest &manifest, const std::filesystem::path &root)
{
    for (const plugins::ContentFile &file : files)
        into.push_back({id, u8(manifest.name), u8(file.title), root / std::filesystem::path(u8(file.path))});
}

}

plugins::ContentFileChecks contentFileChecks()
{
    return {application::styleCatalogueProblem, application::karaokePresetProblem, application::graphProblem};
}

bool pluginContentShows(const plugins::InstalledPlugin &entry, const plugins::Manifest &manifest)
{
    return pluginNodesRun(entry, manifest);
}

void addPluginContent(application::PluginContent &into, const std::string &id, const plugins::Manifest &manifest,
                      const std::filesystem::path &root)
{
    add(into.styleCatalogues, manifest.styles, id, manifest, root);
    add(into.karaokePresets, manifest.karaokePresets, id, manifest, root);
    add(into.graphs, manifest.graphs, id, manifest, root);
}

}
