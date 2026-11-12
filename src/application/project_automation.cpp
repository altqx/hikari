#include "hikari/application/project_automation.h"

#include "hikari/application/automation_services.h"

#include <algorithm>

namespace hikari::application {

namespace {

std::string automationFolder(std::string root, const char *leaf, bool windows)
{
    const char separator = windows ? '\\' : '/';
    for (const char *name : {".hikari", "automation", leaf}) {
        if (!root.empty() && root.back() != separator)
            root += separator;
        root += name;
    }
    return root;
}

}

std::string projectAutoloadFolder(const std::string &root, bool windows)
{
    return automationFolder(root, "autoload", windows);
}

std::string projectIncludeFolder(const std::string &root, bool windows)
{
    return automationFolder(root, "include", windows);
}

AutoloadPlan planAutoload(const std::vector<FolderListing> &base, const std::vector<WindowScripts> &windows,
                          bool windowsPaths)
{
    AutoloadPlan plan;
    const auto addToLoad = [&](const std::vector<std::string> &scripts) {
        for (const auto &script : scripts)
            if (std::ranges::find(plan.load, script) == plan.load.end())
                plan.load.push_back(script);
    };
    addToLoad(chooseAutoloadScripts(base, windowsPaths));
    for (const auto &window : windows) {
        const bool hasProject = !window.project.root.empty();
        const FolderListing project{projectAutoloadFolder(window.project.root, windowsPaths), window.projectNames};
        if (hasProject && !window.project.trusted) {
            plan.shown[window.window] = chooseAutoloadScripts(base, windowsPaths);
            plan.locked[window.window] = chooseAutoloadScripts({project}, windowsPaths);
            continue;
        }
        std::vector<FolderListing> tiers = base;
        if (hasProject)
            tiers.push_back(project);
        plan.shown[window.window] = chooseAutoloadScripts(tiers, windowsPaths);
        addToLoad(plan.shown[window.window]);
    }
    return plan;
}

std::vector<std::string> includeFoldersFor(const std::string &script, const std::vector<std::string> &base,
                                           const std::vector<std::string> &trustedRoots, bool windows)
{
    std::vector<std::string> folders;
    for (const auto &root : trustedRoots)
        if (scriptInFolder(script, root, windows)) {
            folders.push_back(projectIncludeFolder(root, windows));
            break;
        }
    folders.insert(folders.end(), base.begin(), base.end());
    return folders;
}

}
