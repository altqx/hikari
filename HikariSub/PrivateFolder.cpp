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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

// The flags each folder on a path is opened with while it is walked: no link
// is followed, and on Linux the folder is opened as a path only, so a folder
// the user may go through but not list, such as another user's 0711 home,
// can be walked through too.
#ifdef O_PATH
const int walkFlags = O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
#else
const int walkFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
#endif

// whether the folder fd is open on is the user's and lets nobody else in
bool IsPrivateFolder(int fd)
{
	struct stat folderStat;
	return fstat(fd, &folderStat) == 0 && S_ISDIR(folderStat.st_mode) && folderStat.st_uid == geteuid() &&
		(folderStat.st_mode & 077) == 0;
}

// Whether no other user can rename or remove what is in the folder of
// folderStat, nor put something else at its names: root's or the user's, and
// writable by no one else, or sticky, so that what is in it may be renamed
// only by its own owner. A folder with an ACL that lets someone else write
// has the group write bit, as the ACL's mask.
bool KeepsItsNames(const struct stat &folderStat)
{
	return S_ISDIR(folderStat.st_mode) && (folderStat.st_uid == 0 || folderStat.st_uid == geteuid()) &&
		(!(folderStat.st_mode & (S_IWGRP | S_IWOTH)) || (folderStat.st_mode & S_ISVTX));
}

// Opens the folder at the absolute path, walking down from / one name at a
// time without following a link, when it and every folder above it keeps its
// names, see KeepsItsNames. Once it is open, no other user can then rename
// a folder on the way or put another in its place, so the path keeps leading
// to the folder that was checked. -1 otherwise, as for a path with a link.
int OpenSteadyFolder(const std::string &path)
{
	if (path.empty() || path[0] != '/')
		return -1;
	std::vector<std::string> names;
	for (size_t start = 1; start <= path.size();) {
		size_t end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		std::string name = path.substr(start, end - start);
		if (name == "..")
			return -1;
		if (!name.empty() && name != ".")
			names.push_back(name);
		start = end + 1;
	}
	int fd = open("/", walkFlags);
	for (size_t i = 0; fd >= 0; ++i) {
		struct stat folderStat;
		if (fstat(fd, &folderStat) != 0 || !KeepsItsNames(folderStat)) {
			close(fd);
			return -1;
		}
		if (i == names.size())
			return fd;
		int next = openat(fd, names[i].c_str(), walkFlags);
		close(fd);
		fd = next;
	}
	return -1;
}

// the path with no link, no "." or ".." and no trailing slash in it; empty
// when there is nothing at it
std::string RealPath(const std::string &path)
{
	char *real = realpath(path.c_str(), nullptr);
	if (!real)
		return std::string();
	std::string resolved = real;
	free(real);
	return resolved;
}

// Makes the folder name in the folder at, or narrows the one there, as
// MakePrivateFolderAt does, and returns it open, or -1 when it is not then a
// folder only the user may open.
int MakePrivateFolderIn(int at, const std::string &name)
{
	if (mkdirat(at, name.c_str(), 0700) != 0 && errno != EEXIST)
		return -1;
	// a folder made before, or by an older version, with the umask's mode;
	// O_NOFOLLOW refuses a link at the name, and only the owner may chmod
	int fd = openat(at, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return -1;
	struct stat folderStat;
	if (fstat(fd, &folderStat) == 0 && folderStat.st_uid == geteuid() && (folderStat.st_mode & 077))
		(void)!fchmod(fd, folderStat.st_mode & 07700);
	if (!IsPrivateFolder(fd)) {
		close(fd);
		return -1;
	}
	return fd;
}

// the path of the socket called name in folder, a path with no link in it;
// empty when it is longer than a socket address holds, as it would then be
// cut short and the socket made at another name, possibly in the folder above
std::string SocketPathIn(const std::string &folder, const std::string &name)
{
	if (folder.empty() || folder.size() + 1 + name.size() >= sizeof(sockaddr_un{}.sun_path))
		return std::string();
	return folder + (folder.back() == '/' ? "" : "/") + name;
}

// The path of the socket called name in the runtime folder at path, as the
// login makes $XDG_RUNTIME_DIR, with no link in it, when the folder is
// private and no other user can rename it or a folder above it; it is not
// narrowed, as it is not this program's.
std::string SocketInRuntimeFolder(const std::string &path, const std::string &name)
{
	if (path.empty() || path[0] != '/')
		return std::string();
	std::string folder = RealPath(path);
	int fd = OpenSteadyFolder(folder);
	bool usable = fd >= 0 && IsPrivateFolder(fd);
	if (fd >= 0)
		close(fd);
	return usable ? SocketPathIn(folder, name) : std::string();
}

// The path of the socket called name in the folder called folderName in
// parent, with no link in it, the folder made or narrowed, when it is private
// and no other user can rename it or a folder above it. A socket path too
// long for a socket address is refused before the folder is made. With
// makeParent, parent is made with mode 0700 when it is not there and the
// folder above it keeps its names, as a new user's cache folder.
std::string SocketInPrivateFolderIn(const std::string &parent, const std::string &folderName,
	const std::string &name, bool makeParent)
{
	if (parent.empty() || parent[0] != '/')
		return std::string();
	std::string folder = RealPath(parent);
	if (folder.empty() && makeParent) {
		std::string trimmed = parent;
		while (trimmed.size() > 1 && trimmed.back() == '/')
			trimmed.pop_back();
		size_t slash = trimmed.rfind('/');
		std::string last = trimmed.substr(slash + 1);
		int aboveFd = last.empty() || last == "." || last == ".." ? -1 :
			OpenSteadyFolder(RealPath(slash ? trimmed.substr(0, slash) : std::string("/")));
		if (aboveFd < 0)
			return std::string();
		bool made = mkdirat(aboveFd, last.c_str(), 0700) == 0 || errno == EEXIST;
		close(aboveFd);
		if (made)
			folder = RealPath(parent);
	}
	if (folder.empty())
		return std::string();
	std::string socket = SocketPathIn(folder + (folder.back() == '/' ? "" : "/") + folderName, name);
	if (socket.empty())
		return std::string();
	int parentFd = OpenSteadyFolder(folder);
	if (parentFd < 0)
		return std::string();
	int fd = MakePrivateFolderIn(parentFd, folderName);
	close(parentFd);
	if (fd < 0)
		return std::string();
	close(fd);
	return socket;
}

// the host's name with only letters, digits, '.', '-' and '_' in it
std::string SafeHostName()
{
	char host[256] = {};
	if (gethostname(host, sizeof(host) - 1) != 0 || !host[0])
		return "localhost";
	std::string safe;
	for (const char *c = host; *c; ++c)
		safe += isalnum(static_cast<unsigned char>(*c)) || *c == '.' || *c == '-' || *c == '_' ? *c : '_';
	return safe;
}

// Opens the folder the socket at path is in when it is private and no other
// user can rename it or a folder above it, and gives the socket's name.
int OpenSocketFolder(const std::string &path, std::string *name)
{
	size_t slash = path.rfind('/');
	if (slash == std::string::npos || slash + 1 >= path.size())
		return -1;
	*name = path.substr(slash + 1);
	int fd = OpenSteadyFolder(slash ? path.substr(0, slash) : std::string("/"));
	if (fd >= 0 && !IsPrivateFolder(fd)) {
		close(fd);
		return -1;
	}
	return fd;
}

// whether the name in the folder at is a socket of the user's, with no
// permission for others when ownerOnly
bool IsUsersSocket(int at, const std::string &name, bool ownerOnly)
{
	struct stat socketStat;
	return fstatat(at, name.c_str(), &socketStat, AT_SYMLINK_NOFOLLOW) == 0 && S_ISSOCK(socketStat.st_mode) &&
		socketStat.st_uid == geteuid() && (!ownerOnly || (socketStat.st_mode & 077) == 0);
}

} // namespace

bool MakePrivateFolderAt(const std::string &path)
{
	std::string folder = path;
	while (folder.size() > 1 && folder.back() == '/')
		folder.pop_back();
	if (folder.empty())
		return false;
	int fd = MakePrivateFolderIn(AT_FDCWD, folder);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

std::string UserRuntimeSocketPath(const std::string &name)
{
	return UserRuntimeSocketPathIn(name, "/run/user/" + std::to_string(static_cast<unsigned long>(geteuid())),
		"/tmp");
}

std::string UserRuntimeSocketPathIn(const std::string &name, const std::string &loginRuntime,
	const std::string &temp)
{
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	std::string socket = SocketInRuntimeFolder(runtime ? runtime : "", name);
	if (!socket.empty())
		return socket;
	// the folder the login makes for $XDG_RUNTIME_DIR, so that a program
	// started without it, as from su or cron, finds the same socket
	socket = SocketInRuntimeFolder(loginRuntime, name);
	if (!socket.empty())
		return socket;
	// $TMPDIR, and /tmp when $TMPDIR cannot be used, as when it is too long
	const std::string own = "hikarisub-" + std::to_string(static_cast<unsigned long>(geteuid()));
	const char *tempEnv = getenv("TMPDIR");
	if (tempEnv && tempEnv[0] == '/') {
		socket = SocketInPrivateFolderIn(tempEnv, own, name, false);
		if (!socket.empty())
			return socket;
	}
	socket = SocketInPrivateFolderIn(temp, own, name, false);
	if (!socket.empty())
		return socket;
	// the home folder may be shared by several hosts, which each have a
	// program of their own running
	const char *cache = getenv("XDG_CACHE_HOME");
	const char *home = getenv("HOME");
	std::string cacheFolder = cache && cache[0] == '/' ? std::string(cache) :
		home && home[0] == '/' ? std::string(home) + "/.cache" : std::string();
	return SocketInPrivateFolderIn(cacheFolder, "hikarisub-" + SafeHostName(), name, true);
}

bool IsUserRuntimeSocket(const std::string &path)
{
	std::string name;
	int fd = OpenSocketFolder(path, &name);
	if (fd < 0)
		return false;
	bool usable = IsUsersSocket(fd, name, true);
	close(fd);
	return usable;
}

bool NarrowUserRuntimeSocket(const std::string &path)
{
	std::string name;
	int fd = OpenSocketFolder(path, &name);
	if (fd < 0)
		return false;
	// no one else may put anything at the name in the private folder between
	// the check and the chmod
	bool narrowed = IsUsersSocket(fd, name, false) && fchmodat(fd, name.c_str(), 0600, 0) == 0 &&
		IsUsersSocket(fd, name, true);
	close(fd);
	return narrowed;
}

FILE *OpenPrivateFileAt(const std::string &path)
{
	// O_NONBLOCK keeps a FIFO at the name from holding the open, and
	// O_NOCTTY a terminal from becoming this program's; anything but a
	// regular file is then refused
	int fd = open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC, 0600);
	if (fd < 0)
		return nullptr;
	// a file that was there is narrowed first and only then emptied; one with
	// another name too, as a hard link to another of the user's files put at
	// this name, or another user's file, is left as it is
	struct stat fileStat;
	if (fstat(fd, &fileStat) != 0 || !S_ISREG(fileStat.st_mode) || fileStat.st_nlink != 1 ||
		fileStat.st_uid != geteuid() || ((fileStat.st_mode & 07077) && fchmod(fd, 0600) != 0) ||
		ftruncate(fd, 0) != 0 ||
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
