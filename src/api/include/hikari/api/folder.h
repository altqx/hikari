#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
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

// Never sent to AI callers, whatever a user's or a Project's ai.exclude says.
inline constexpr std::string_view kAiExcludeDefaults[] = {".hikari/credentials.toml", ".env*", "*.key", ".git/",
                                                         ".hikari/project.local.toml"};

// `*` and `?` stay inside one part of a '/'-separated path, `**` crosses parts; ASCII letters match either case.
bool globMatches(std::string_view pattern, std::string_view text);

// Whether `relative` ('/'-separated, inside a folder) or a folder above it matches one of `globs`, gitignore-style:
// a glob with a '/' before its end is anchored at the folder, one without matches a part's name at any depth, and a
// trailing '/' matches folders only.
bool excludedPath(std::string_view relative, bool folder, std::span<const std::string> globs);

// What `real`, a folder inside `root`, holds: at most kMaxFolderEntries, without names that aren't
// UTF-8 or links that lead outside `root`. nullopt: `real` is no folder.
std::optional<FolderEntries> folderEntries(const std::filesystem::path &root, const std::filesystem::path &real);

}
