#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hikari::application {

// A content pack file of a turned-on plugin, shown read-only beside its built-in counterpart.
struct PluginContentFile {
    std::string plugin; // the plugin's id
    std::u8string pluginName;
    std::u8string title; // empty for a karaoke preset, whose file holds its title
    std::filesystem::path file;
};

struct PluginContent {
    std::vector<PluginContentFile> styleCatalogues;
    std::vector<PluginContentFile> karaokePresets;
};

void setPluginContent(PluginContent content);
PluginContent pluginContent();

// Why a plugin's file can't be read as its built-in counterpart is (English, technical), or nothing.
std::optional<std::string> styleCatalogueProblem(std::span<const std::byte> bytes);
std::optional<std::string> karaokePresetProblem(std::span<const std::byte> bytes);

}
