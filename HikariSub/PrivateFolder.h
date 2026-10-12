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

#include <stdio.h>
#include <string>

// Makes the folder at path with mode 0700 when nothing is there, its parent
// being there, or narrows the folder there to its owner's permissions when
// the owner is the user. A link at path, or another user's folder, is left as
// it is. Returns whether path is now a folder only the user may open: no
// link, the user's own, and with no permission for its group or others.
bool MakePrivateFolderAt(const std::string &path);

// A folder only the user may open, for files that live as long as the
// program runs, such as the socket a second instance passes files to the
// first through: $XDG_RUNTIME_DIR when it is such a folder, as the login
// makes it, or else hikarisub-<uid> in the temporary folder ($TMPDIR, or
// /tmp), made or narrowed by MakePrivateFolderAt. Empty when neither is
// such a folder, as when another user made that name first.
std::string UserRuntimeFolder();

// Opens the file at path for reading and writing, new or emptied, so that
// only the user may open it: a new file is made 0600, and an existing one is
// narrowed to 0600 before it is emptied. A link at path is not followed, and
// a file that cannot be narrowed, as another user's, or that is no regular
// file, is not opened. nullptr on a failure.
FILE *OpenPrivateFileAt(const std::string &path);

#endif
