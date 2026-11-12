#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::api {

inline constexpr std::size_t kMaxFolderEntries = 4096;

// Where a path a caller names inside a granted folder leads, or why it may not go there (English).
// `relative` is '/'-separated and "" is the folder itself. A name Windows cannot hold, an absolute
// path, `..`, and a link that leads outside the folder are refused.
std::expected<std::filesystem::path, std::string> pathInside(const std::filesystem::path &folder, std::string_view relative);

struct FolderEntry {
    std::string name;
    bool folder = false;
};

struct FolderEntries {
    std::vector<FolderEntry> entries; // in name order
    bool truncated = false;
};

// What `real`, a folder inside `root`, holds: at most kMaxFolderEntries, without names that aren't
// UTF-8 or links that lead outside `root`. nullopt: `real` is no folder.
std::optional<FolderEntries> folderEntries(const std::filesystem::path &root, const std::filesystem::path &real);

}
