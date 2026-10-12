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

#include <stdlib.h>
#include <sys/stat.h>
#include <string>

// The file a save to path replaces. A save writes a temporary file and renames
// it over the saved path; renaming it over a symbolic link would turn the link
// into a plain file and leave its target as it was, so the target is replaced
// instead. When path resolves, resolved is set to the target's absolute path.
// A path that does not exist yet and is not a link leaves resolved empty and is
// saved under its own name. Returns false for a link that leads to no file,
// which is not replaced either.
inline bool ResolveSaveTarget(const char *path, std::string *resolved)
{
	resolved->clear();
	char *target = realpath(path, nullptr);
	if (target) {
		*resolved = target;
		free(target);
		return true;
	}
	struct stat linkStat;
	return lstat(path, &linkStat) != 0 || !S_ISLNK(linkStat.st_mode);
}

#endif
