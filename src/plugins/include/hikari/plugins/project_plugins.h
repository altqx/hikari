#pragma once

#include "hikari/api/json.h"
#include "hikari/plugins/registry.h"

#include <map>
#include <string>
#include <vector>

namespace hikari::plugins {

// A trusted Project's [plugins] table; an untrusted Project has none.
struct ProjectPlugins {
    std::vector<std::string> enabled, disabled;
    std::map<std::string, api::Json> settings; // [plugins."<id>"], by plugin id
};

// The Project's enabled/disabled overrides the user's switch; a revoked plugin stays off.
bool pluginOn(const std::string &id, const InstalledPlugin &installed, const ProjectPlugins *project);
// On in at least one window, or by the user's switch when no window has a Project.
bool pluginRunsAnywhere(const std::string &id, const InstalledPlugin &installed,
                        const std::vector<const ProjectPlugins *> &windows);
// The user's values with the Project's set over them, key by key.
api::Json projectSettings(const std::string &id, const api::Json &stored, const ProjectPlugins *project);
// Ids the Project enables that aren't installed, in its order.
std::vector<std::string> missingPlugins(const ProjectPlugins &project, const Registry &registry);

}
