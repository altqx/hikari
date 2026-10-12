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

#ifndef _WIN32

#include "PrivateFolder.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// whether the folder fd is open on is the user's and lets nobody else in
bool IsPrivateFolder(int fd)
{
	struct stat folderStat;
	return fstat(fd, &folderStat) == 0 && S_ISDIR(folderStat.st_mode) && folderStat.st_uid == geteuid() &&
		(folderStat.st_mode & 077) == 0;
}

} // namespace

bool MakePrivateFolderAt(const std::string &path)
{
	std::string folder = path;
	while (folder.size() > 1 && folder.back() == '/')
		folder.pop_back();
	if (folder.empty())
		return false;
	if (mkdir(folder.c_str(), 0700) != 0 && errno != EEXIST)
		return false;
	// a folder made before, or by an older version, with the umask's mode;
	// O_NOFOLLOW refuses a link at the name, and only the owner may chmod
	int fd = open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return false;
	struct stat folderStat;
	if (fstat(fd, &folderStat) == 0 && folderStat.st_uid == geteuid() && (folderStat.st_mode & 077))
		(void)!fchmod(fd, folderStat.st_mode & 07700);
	bool made = IsPrivateFolder(fd);
	close(fd);
	return made;
}

FILE *OpenPrivateFileAt(const std::string &path)
{
	// O_NONBLOCK keeps a FIFO at the name from holding the open; anything but
	// a regular file is then refused
	int fd = open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
	if (fd < 0)
		return nullptr;
	// a file that was there is narrowed first and only then emptied
	struct stat fileStat;
	if (fstat(fd, &fileStat) != 0 || !S_ISREG(fileStat.st_mode) ||
		((fileStat.st_mode & 07077) && fchmod(fd, 0600) != 0) || ftruncate(fd, 0) != 0 ||
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK) != 0) {
		close(fd);
		return nullptr;
	}
	FILE *file = fdopen(fd, "w+b");
	if (!file)
		close(fd);
	return file;
}

#endif
