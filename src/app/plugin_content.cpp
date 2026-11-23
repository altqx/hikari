#include "hikari/app/plugin_content.h"

#include "hikari/app/plugin_nodes.h"

namespace hikari::app {

namespace {

std::u8string u8(const std::string &s)
{
    return {s.begin(), s.end()};
}

std::filesystem::path inPackage(const std::filesystem::path &root, const std::string &path)
{
    auto out = root / std::filesystem::path(u8(path));
    return out.make_preferred();
}

void add(std::vector<application::PluginContentFile> &into, const std::vector<plugins::ContentFile> &files,
         const std::string &id, const plugins::Manifest &manifest, const std::filesystem::path &root)
{
    for (const plugins::ContentFile &file : files)
        into.push_back({id, u8(manifest.name), u8(file.title), inPackage(root, file.path)});
}

}

plugins::ContentFileChecks contentFileChecks()
{
    return {application::styleCatalogueProblem, application::karaokePresetProblem, application::dictionaryAffProblem,
            application::dictionaryDicProblem, application::graphProblem};
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
    for (const plugins::Dictionary &dictionary : manifest.dictionaries)
        into.dictionaries.push_back({id, u8(manifest.name), u8(dictionary.title),
                                     std::u16string(dictionary.language.begin(), dictionary.language.end()),
                                     inPackage(root, dictionary.aff), inPackage(root, dictionary.dic)});
    add(into.graphs, manifest.graphs, id, manifest, root);
}

}
