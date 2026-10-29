#pragma once

// Lists a folder as wxDir does: the platform's order, no sorting. On Windows
// the long name must match too ("x.dicx" matches "*.dic" by its 8.3 name).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::legacy_dir {

enum Flags : int { Files = 1, Dirs = 2, Hidden = 4 };

// Full paths without "." and ".."; empty `filespec` matches every name.
// nullopt when the folder cannot be opened.
std::optional<std::vector<std::filesystem::path>> entries(const std::filesystem::path &folder,
                                                          std::u16string_view filespec, int flags);

// '?' is one code point, '\' quotes the next character, case sensitive;
// with dotSpecial a text starting with '.' never matches.
bool matchWild(std::u32string_view pattern, std::u32string_view text, bool dotSpecial);

} // namespace hikari::application::legacy_dir
