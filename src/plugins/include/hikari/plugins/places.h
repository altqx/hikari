#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace hikari::plugins {

// Where a path a plugin names inside one of its folders leads, or why it may not go there
// (English). `relative` is '/'-separated and "" is the folder itself. A name Windows cannot hold,
// an absolute path, `..`, and a link that leads outside the folder are refused.
std::expected<std::filesystem::path, std::string> pathInside(const std::filesystem::path &folder, std::string_view relative);

}
