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

// The path of the socket called name for files that live as long as the
// program runs, such as the socket a second instance passes files to the
// first through, in a folder only the user may open that no other user can
// rename or put another folder in the place of. The folders tried are
// $XDG_RUNTIME_DIR, as the login makes it, used as it is; hikarisub-<uid> in
// the temporary folder ($TMPDIR, or /tmp); and hikarisub-<host> in
// $XDG_CACHE_HOME, or ~/.cache; the last two made or narrowed as
// MakePrivateFolderAt does. A folder is used only when it, and every folder
// above it, is root's or the user's and lets no one else write in it unless
// it is sticky, so that no other user can rename any folder on the way after
// it is checked; and only when the socket's path fits a socket address, which
// would otherwise be cut short. The path is given with no link in it. Empty
// when there is no such folder, as when another user made that name first.
std::string UserRuntimeSocketPath(const std::string &name);

// Whether the socket at path, as UserRuntimeSocketPath gives it, is the
// user's own socket that no one else may open, in a folder that is still
// private and that no other user can rename, so that a second instance
// does not pass its files to a socket that is not the user's.
bool IsUserRuntimeSocket(const std::string &path);

// For the program that made the socket at path, as UserRuntimeSocketPath
// gives it: narrows it to mode 0600 when it is a socket of the user's in a
// folder that is still private and that no other user can rename, and
// returns whether it is.
bool NarrowUserRuntimeSocket(const std::string &path);

// Opens the file at path for reading and writing, new or emptied, so that
// only the user may open it: a new file is made 0600, and an existing one is
// narrowed to 0600 before it is emptied. A link at path is not followed, and
// another user's file, a file with another name too, as a hard link, or
// what is no regular file, is not opened. nullptr on a failure.
FILE *OpenPrivateFileAt(const std::string &path);

#endif
