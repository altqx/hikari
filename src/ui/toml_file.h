#pragma once

// toml++ does not keep comments, so a file HikariSub writes loses them.

#include "hikari/application/owned_file.h"

#include <toml++/toml.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace hikari::ui {

using application::ParseFailure;

std::variant<toml::table, ParseFailure> parseTomlFile(std::string_view text, std::int64_t supported);
std::string formatTomlFile(const toml::table &table, std::int64_t version);
std::string formatToml(const toml::node &node);

}
