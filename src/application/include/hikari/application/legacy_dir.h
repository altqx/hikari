#pragma once

// wxDir as the legacy app lists a folder (wxWidgets 3.3.3 at 20d647c4), on
// each platform as that platform's build did (R5-per-platform):
//  - Windows (src/msw/dir.cpp): FindFirstFileW("<folder>\<filespec>", or
//    "*.*" without one) and FindNextFileW, in the order the file system
//    returns the names. The system matches the pattern against the long and
//    the 8.3 short name, so wx keeps an entry only when PathMatchSpecW also
//    matches its long name (CheckFoundMatch): "x.dicx", whose short name
//    X~1.DIC matches "*.dic", is dropped. The type and the hidden check read
//    the entry's own attributes (FILE_ATTRIBUTE_DIRECTORY; HIDDEN or SYSTEM
//    is hidden).
//  - Linux (src/unix/dir.cpp): readdir order; a folder is what stat (links
//    followed) calls one; the name is matched with wxMatchWild, case
//    sensitive, and without wxDIR_HIDDEN a name starting with '.' never
//    matches.
// No sorting on either platform.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::legacy_dir {

// wxDIR_FILES / wxDIR_DIRS / wxDIR_HIDDEN (wx/dir.h).
enum Flags : int { Files = 1, Dirs = 2, Hidden = 4 };

// wxDir::GetFirst / GetNext over `folder` with `filespec` (empty: every
// name) and `flags`: the entries' full paths in the order the platform
// returns them, "." and ".." left out. nullopt when the folder cannot be
// opened (wxDir::IsOpened).
std::optional<std::vector<std::filesystem::path>> entries(const std::filesystem::path &folder,
                                                          std::u16string_view filespec, int flags);

// wxMatchWild(pattern, text, dotSpecial): '*' and '?' (one code point, as
// wchar_t is on Linux), '\' quoting the next character, case sensitive;
// with dotSpecial a text starting with '.' never matches.
bool matchWild(std::u32string_view pattern, std::u32string_view text, bool dotSpecial);

} // namespace hikari::application::legacy_dir
