#pragma once

// toml++ does not keep comments, so a file HikariSub writes loses them.

#include "hikari/application/owned_file.h"
#include "hikari/application/settings.h"

#include <toml++/toml.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hikari::ui {

using application::ParseFailure;

std::variant<toml::table, ParseFailure> parseTomlFile(std::string_view text, std::int64_t supported);
std::string formatTomlFile(const toml::table &table, std::int64_t version);
std::string formatToml(const toml::node &node);

std::vector<std::string_view> idSegments(std::string_view id);
std::optional<application::SettingValue> fromToml(const toml::node &node);
void putToml(toml::table &table, std::string_view key, const application::SettingValue &value);
toml::table *tomlParent(toml::table &root, std::string_view id, bool create);
const toml::node *tomlNode(const toml::table &root, std::string_view id);
void eraseToml(toml::table &root, std::string_view id);

// `text` with the dotted `id` set to `value`, or removed when nullopt; every other byte stays, comments
// included. nullopt when the file's layout can't take the change in place.
std::optional<std::string> patchTomlSetting(std::string_view text, std::string_view id,
                                            const std::optional<application::SettingValue> &value);
// patchTomlSetting for any node, such as an inline table.
std::optional<std::string> patchTomlNode(std::string_view text, std::string_view id, const toml::node *value);

}
