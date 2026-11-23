#include "hikari/plugins/project_plugins.h"

#include <algorithm>

namespace hikari::plugins {

namespace {

bool lists(const std::vector<std::string> &ids, const std::string &id)
{
    return std::ranges::find(ids, id) != ids.end();
}

}

bool pluginOn(const std::string &id, const InstalledPlugin &installed, const ProjectPlugins *project)
{
    if (installed.revoked)
        return false;
    if (project && lists(project->disabled, id))
        return false;
    if (project && lists(project->enabled, id))
        return true;
    return installed.enabled;
}

bool pluginRunsAnywhere(const std::string &id, const InstalledPlugin &installed,
                        const std::vector<const ProjectPlugins *> &windows)
{
    if (windows.empty())
        return pluginOn(id, installed, nullptr);
    return std::ranges::any_of(windows, [&](const ProjectPlugins *project) { return pluginOn(id, installed, project); });
}

bool projectable(const Setting &setting)
{
    const api::Json *flag = setting.definition.find("projectable");
    return !setting.secret() && (!flag || flag->asBool());
}

api::Json projectSettings(const Manifest &manifest, const api::Json &stored, const ProjectPlugins *project)
{
    if (!project)
        return stored;
    const auto set = project->settings.find(manifest.id);
    if (set == project->settings.end() || !set->second.isObject())
        return stored;
    api::Json out = stored.isObject() ? stored : api::Json::object();
    for (const Setting &setting : manifest.settings)
        if (const api::Json *value = set->second.find(setting.id); value && projectable(setting))
            out[setting.id] = *value;
    return out;
}

std::vector<std::string> missingPlugins(const ProjectPlugins &project, const Registry &registry)
{
    std::vector<std::string> missing;
    for (const auto &id : project.enabled)
        if (!registry.contains(id) && !lists(missing, id))
            missing.push_back(id);
    return missing;
}

}
