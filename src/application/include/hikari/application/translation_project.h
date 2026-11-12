#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace hikari::application {

// The Script Info key that points a file outside a Project at its Honya project folder, relative to the file.
inline constexpr std::u8string_view kHonyaProjectKey = u8"Honya Project";

// Where a Document's Honya project folder is (ADR 0030): in a Project, its root or the subfolder `glossary` names
// (`translation.glossary`); outside one, the folder `pointer` names beside `subtitle`. Nullopt when neither applies or
// the folder named would leave the Project.
std::optional<std::filesystem::path> honyaFolder(const std::optional<std::filesystem::path> &projectRoot,
                                                 std::u8string_view glossary,
                                                 const std::optional<std::filesystem::path> &subtitle,
                                                 std::u8string_view pointer);

}
