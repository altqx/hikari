#pragma once

#include "hikari/application/node_catalogue.h"
#include "hikari/plugins/manifest.h"
#include "hikari/plugins/registry.h"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace hikari::app {

// A plugin's nodes run when it is turned on, not revoked, and signed by the key pinned at its
// first signed install, if any.
bool pluginNodesRun(const plugins::InstalledPlugin &entry, const plugins::Manifest &manifest);

// The manifest's nodes, their code read from the package at `root`; or why a file can't be read.
std::expected<std::vector<application::PluginNode>, std::string> pluginNodesOf(const plugins::Manifest &manifest,
                                                                               const std::filesystem::path &root);

}
