#pragma once

#include "hikari/application/plugin_content.h"
#include "hikari/plugins/manifest.h"
#include "hikari/plugins/package.h"
#include "hikari/plugins/registry.h"

#include <filesystem>
#include <string>

namespace hikari::app {

// What each content pack file must pass at install: the reader its built-in counterpart uses.
plugins::ContentFileChecks contentFileChecks();

// A plugin's content shows by the same rule as its nodes run: turned on, not revoked, and signed by its pinned key.
bool pluginContentShows(const plugins::InstalledPlugin &entry, const plugins::Manifest &manifest);

// Adds the manifest's content pack files, under the package at `root`.
void addPluginContent(application::PluginContent &into, const std::string &id, const plugins::Manifest &manifest,
                      const std::filesystem::path &root);

}
