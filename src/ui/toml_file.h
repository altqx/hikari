#pragma once

// CF1 (#488): an owned TOML file's text (owned_file.h): `version = N` at the
// top, then its tables. toml++ (MIT) parses and writes it; comments are not
// kept when HikariSub writes a file. An adapter: the application layer
// depends on core only (docs/architecture.md).

#include "hikari/application/owned_file.h"

#include <toml++/toml.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace hikari::ui {

using application::ParseFailure;

// The file's table without its `version`, or where it fails: a syntax error,
// or a version that is missing, not an integer or above `supported`.
std::variant<toml::table, ParseFailure> parseTomlFile(std::string_view text, std::int64_t supported);
// `version = <version>`, then `table` as toml++ writes it, strings in double
// quotes.
std::string formatTomlFile(const toml::table &table, std::int64_t version);
// A node's text, strings in double quotes.
std::string formatToml(const toml::node &node);

} // namespace hikari::ui
