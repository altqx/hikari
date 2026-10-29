#include "toml_file.h"

#include <sstream>

namespace hikari::ui {

using application::checkFileVersion;

std::variant<toml::table, ParseFailure> parseTomlFile(std::string_view text, std::int64_t supported)
{
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error &error) {
        const auto &place = error.source().begin;
        return ParseFailure{static_cast<int>(place.line), static_cast<int>(place.column),
                            std::string(error.description())};
    }
    std::optional<std::int64_t> version;
    if (const auto *node = table.get("version"); node && node->is_integer())
        version = node->value<std::int64_t>();
    else if (node)
        return ParseFailure{static_cast<int>(node->source().begin.line), static_cast<int>(node->source().begin.column),
                            "Its version is not a whole number."};
    if (auto failure = checkFileVersion(version, supported))
        return *failure;
    table.erase("version");
    return table;
}

namespace {

constexpr auto kFlags = toml::toml_formatter::default_flags & ~toml::format_flags::allow_literal_strings;

} // namespace

std::string formatToml(const toml::node &node)
{
    std::ostringstream out;
    node.visit([&](const auto &n) { out << toml::toml_formatter(n, kFlags); });
    return out.str();
}

std::string formatTomlFile(const toml::table &table, std::int64_t version)
{
    std::ostringstream out;
    out << "version = " << version << "\n";
    if (!table.empty())
        out << "\n" << toml::toml_formatter(table, kFlags) << "\n";
    return out.str();
}

} // namespace hikari::ui
