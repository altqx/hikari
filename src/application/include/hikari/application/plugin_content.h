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

struct PluginDictionary {
    std::string plugin;
    std::u8string pluginName;
    std::u8string title;
    std::u16string language;
    std::filesystem::path aff;
    std::filesystem::path dic;

    bool operator==(const PluginDictionary &) const = default;
};

struct PluginContent {
    std::vector<PluginContentFile> styleCatalogues;
    std::vector<PluginContentFile> karaokePresets;
    std::vector<PluginDictionary> dictionaries;
    std::vector<PluginContentFile> graphs;
    std::vector<PluginContentFile> layouts;
};

void setPluginContent(PluginContent content);
PluginContent pluginContent();

// Why a plugin's file can't be read as its built-in counterpart is (English, technical), or nothing.
std::optional<std::string> styleCatalogueProblem(std::span<const std::byte> bytes);
std::optional<std::string> karaokePresetProblem(std::span<const std::byte> bytes);
std::optional<std::string> dictionaryAffProblem(std::span<const std::byte> bytes);
std::optional<std::string> dictionaryDicProblem(std::span<const std::byte> bytes);
std::optional<std::string> graphProblem(std::span<const std::byte> bytes);

}
