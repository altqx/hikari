#include "hikari/app/plugin_content.h"

#include "hikari/app/plugin_nodes.h"

#include "workspace_layout.h"

namespace hikari::app {

namespace {

std::u8string u8(const std::string &s)
{
    return {s.begin(), s.end()};
}

void add(std::vector<application::PluginContentFile> &into, const std::vector<plugins::ContentFile> &files,
         const std::string &id, const plugins::Manifest &manifest, const std::filesystem::path &root)
{
    for (const plugins::ContentFile &file : files) {
        auto path = root / std::filesystem::path(u8(file.path));
        into.push_back({id, u8(manifest.name), u8(file.title), path.make_preferred()});
    }
}

std::optional<std::string> layoutProblem(std::span<const std::byte> bytes)
{
    QString problem;
    const QByteArray text(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
    if (ui::WorkspaceArrangement::parse(text, &problem))
        return std::nullopt;
    return "isn\xE2\x80\x99t a layout HikariSub can apply: " + problem.toStdString();
}

}

plugins::ContentFileChecks contentFileChecks()
{
    return {application::styleCatalogueProblem, application::karaokePresetProblem, layoutProblem};
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
    add(into.layouts, manifest.layouts, id, manifest, root);
}

}
