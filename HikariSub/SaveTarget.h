//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#ifndef _WIN32

#include "SaveFile.h"

#include <functional>

// The file a save to path replaces. A save writes a temporary file and renames
// it over the saved path; renaming it over a symbolic link would turn the link
// into a plain file and leave its target as it was, so the target is replaced
// instead. When path resolves, target is set to the target's absolute path,
// with no link in it, and the folder and file there. A path that does not exist
// yet and is not a link is saved under its own name in its folder, resolved
// the same way. Returns false for a link that leads to no file, which is not
// replaced either: a file is not made at a name the user never gave, the
// pattern of a link planted to make another user's program create a file.
// Returns false as well for a link the kernel would not follow, such as
// another user's link in a sticky world-writable folder like /tmp under
// fs.protected_symlinks: realpath reads links without following them, so the
// kernel's check is made by opening the path. The file that open finds must
// be the one at the resolved path, opened without following a link in any of
// its parts, so a link changed between the two looks is refused too.
// afterResolve is for tests and runs between them.
bool ResolveSaveTarget(const char *path, SaveTarget *target, const std::function<void()> &afterResolve = nullptr);

#endif
