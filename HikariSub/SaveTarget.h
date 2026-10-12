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

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <functional>
#include <string>

// The file a save to path replaces. A save writes a temporary file and renames
// it over the saved path; renaming it over a symbolic link would turn the link
// into a plain file and leave its target as it was, so the target is replaced
// instead. When path resolves, resolved is set to the target's absolute path.
// A path that does not exist yet and is not a link leaves resolved empty and is
// saved under its own name. Returns false for a link that leads to no file,
// which is not replaced either: a file is not made at a name the user never
// gave, the pattern of a link planted to make another user's program create
// a file. Returns false as well for a link the kernel would not follow, such
// as another user's link in a sticky world-writable folder like /tmp under
// fs.protected_symlinks: realpath reads links without following them, so the
// kernel's check is made by opening the path. The file that open finds must
// be the one at the resolved path, so a link changed between the two looks
// is refused too. afterResolve is for tests and runs between them.
inline bool ResolveSaveTarget(const char *path, std::string *resolved,
	const std::function<void()> &afterResolve = nullptr)
{
	resolved->clear();
	char *target = realpath(path, nullptr);
	if (afterResolve)
		afterResolve();
	if (target) {
#ifdef O_PATH
		int followed = open(path, O_PATH | O_CLOEXEC);
		struct stat opened, named;
		bool same = followed >= 0 && fstat(followed, &opened) == 0 && lstat(target, &named) == 0 &&
			opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
		if (followed >= 0)
			close(followed);
		if (!same) {
			free(target);
			return false;
		}
#endif
		*resolved = target;
		free(target);
		return true;
	}
	struct stat linkStat;
	return lstat(path, &linkStat) != 0 || !S_ISLNK(linkStat.st_mode);
}

#endif
