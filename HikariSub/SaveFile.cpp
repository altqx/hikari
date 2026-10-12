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

#include "SaveFile.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <random>
#include <vector>
#ifdef __linux__
#include <sys/xattr.h>
#endif

namespace {

// The flags the target is opened with to pin it: without following a link at
// its name, and on Linux as a path only, which neither reads nor writes it and
// so opens a FIFO or a device without the effects of opening them.
#ifdef O_PATH
const int pinFlags = O_PATH | O_NOFOLLOW | O_CLOEXEC;
const int folderFlags = O_PATH | O_DIRECTORY | O_CLOEXEC;
#else
const int pinFlags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC | O_NOCTTY;
const int folderFlags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
#endif

// Makes a new file named prefix and six random letters, relative to at,
// created as a new file is, so the umask and a folder's default ACL apply to
// mode. O_EXCL makes a link or any other file at the name fail the open.
int MakeTempFile(int at, const std::string &prefix, mode_t mode, std::string *name)
{
	static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	thread_local std::mt19937_64 random{ std::random_device{}() };
	for (int attempt = 0; attempt < 100; ++attempt) {
		std::string candidate = prefix;
		for (int i = 0; i < 6; ++i)
			candidate += letters[random() % (sizeof(letters) - 1)];
		int fd = openat(at, candidate.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY, mode);
		if (fd >= 0) {
			*name = candidate;
			return fd;
		}
		if (errno != EEXIST)
			return -1;
	}
	return -1;
}

// The folder path is in, without a trailing slash.
std::string FolderOf(const std::string &path)
{
	size_t slash = path.rfind('/');
	return slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
}

// The last part of path, after its last slash.
std::string NameOf(const std::string &path)
{
	size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// MakeTempFile with prefix, or with a short name in the same folder when
// prefix and the six letters make a name longer than the file system takes.
int MakeTempFileNear(int at, const std::string &prefix, const char *shortName, mode_t mode, std::string *name,
	std::string *usedPrefix)
{
	*usedPrefix = prefix;
	int fd = MakeTempFile(at, prefix, mode, name);
	if (fd < 0 && errno == ENAMETOOLONG) {
		std::string folder = FolderOf(prefix);
		*usedPrefix = folder == "." ? shortName : (folder == "/" ? "" : folder) + "/" + shortName;
		fd = MakeTempFile(at, *usedPrefix, mode, name);
	}
	return fd;
}

std::string TempFolder()
{
	const char *folder = getenv("TMPDIR");
	return (folder && *folder) ? folder : "/tmp";
}

// The name under /proc that opens the very file fd is open on, whatever the
// names that lead to it are now.
std::string ProcPathOf(int fd)
{
	return "/proc/self/fd/" + std::to_string(fd);
}

// Whether the file fd is open on can be written, as access(2) says for it.
bool CanWrite(int fd)
{
	if (access(ProcPathOf(fd).c_str(), W_OK) == 0)
		return true;
#ifdef AT_EMPTY_PATH
	// without /proc
	if (errno == ENOENT)
		return faccessat(fd, "", W_OK, AT_EMPTY_PATH) == 0;
#endif
	return false;
}

bool SameFile(const struct stat &a, const struct stat &b)
{
	return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

bool WriteAll(int fd, const char *data, size_t size, SaveFile::Writer writer)
{
	while (size) {
		ssize_t written = writer ? writer(fd, data, size) : write(fd, data, size);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (written == 0) {
			errno = EIO;
			return false;
		}
		data += written;
		size -= static_cast<size_t>(written);
	}
	return true;
}

// Copies all of from, from its start, to the current offset of to.
bool CopyAll(int from, int to, SaveFile::Writer writer)
{
	std::vector<char> buffer(1 << 16);
	off_t offset = 0;
	for (;;) {
		ssize_t got = pread(from, buffer.data(), buffer.size(), offset);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (got == 0)
			return true;
		if (!WriteAll(to, buffer.data(), static_cast<size_t>(got), writer))
			return false;
		offset += got;
	}
}

// Writes the entries of the folder at, or of the folder name is in when at is
// AT_FDCWD, to disk after a rename or a new file in it.
void SyncFolder(int at, const std::string &name)
{
	int fd = at == AT_FDCWD ? open(FolderOf(name).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)
		: openat(at, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
}

// Renames from over to, both in the folder at, failing when to is taken.
int RenameNoReplace(int at, const char *from, const char *to)
{
#if defined(__linux__) && defined(RENAME_NOREPLACE)
	if (renameat2(at, from, at, to, RENAME_NOREPLACE) == 0)
		return 0;
	if (errno != EINVAL && errno != ENOSYS)
		return -1;
#endif
	// A file system without RENAME_NOREPLACE: a file put at the name in the
	// meantime is renamed over, which writes nothing into it.
	return renameat(at, from, at, to);
}

#ifdef __linux__
// Reads one list or value of extended attributes, from path or else from fd.
template <typename Read>
bool ReadAttributeBuffer(Read read, std::string *out)
{
	for (int attempt = 0; attempt < 4; ++attempt) {
		ssize_t size = read(nullptr, 0);
		if (size < 0)
			return false;
		out->resize(static_cast<size_t>(size));
		if (size == 0)
			return true;
		ssize_t got = read(out->data(), out->size());
		if (got >= 0) {
			out->resize(static_cast<size_t>(got));
			return true;
		}
		if (errno != ERANGE)
			return false;
	}
	return false;
}

bool ReadAttributes(const char *path, int fd, std::map<std::string, std::string> *attributes)
{
	std::string names;
	bool listed = ReadAttributeBuffer([&](char *buffer, size_t size) {
		return path ? listxattr(path, buffer, size) : flistxattr(fd, buffer, size);
	}, &names);
	if (!listed)
		return errno == ENOTSUP;
	for (size_t start = 0; start < names.size();) {
		size_t end = names.find('\0', start);
		if (end == std::string::npos)
			end = names.size();
		std::string name = names.substr(start, end - start);
		start = end + 1;
		if (name.empty())
			continue;
		std::string value;
		bool read = ReadAttributeBuffer([&](char *buffer, size_t size) {
			return path ? getxattr(path, name.c_str(), buffer, size) : fgetxattr(fd, name.c_str(), buffer, size);
		}, &value);
		if (!read)
			return false;
		(*attributes)[name] = value;
	}
	return true;
}

// The attributes of the file targetFd is open on, which may be an O_PATH
// descriptor that the f*xattr calls do not take, and of the temporary file.
bool SameExtendedAttributes(int targetFd, int tempFd)
{
	std::map<std::string, std::string> targetAttributes, tempAttributes;
	bool read = ReadAttributes(ProcPathOf(targetFd).c_str(), -1, &targetAttributes);
	if (!read && errno == ENOENT) {
		// without /proc
		targetAttributes.clear();
		read = ReadAttributes(nullptr, targetFd, &targetAttributes);
	}
	return read && ReadAttributes(nullptr, tempFd, &tempAttributes) && targetAttributes == tempAttributes;
}
// Whether /proc/locks lists a lock of any kind (flock, POSIX, OFD, lease) on
// an inode with this number. The device in /proc/locks is the super block's,
// which on btrfs is not the st_dev of a file in a subvolume, so only the
// inode number is matched; a lock on a file elsewhere with the same number
// makes a save rewrite the file, which is safe.
bool InodeIsLocked(ino_t inode)
{
	FILE *locks = fopen("/proc/locks", "re");
	if (!locks)
		return false;
	bool locked = false;
	char *line = nullptr;
	size_t size = 0;
	while (!locked && getline(&line, &size, locks) > 0) {
		// "1: FLOCK  ADVISORY  WRITE 1234 08:01:131 0 EOF"; a waiting lock
		// has "->" after the number
		char *save = nullptr;
		for (char *word = strtok_r(line, " \t\n", &save); word; word = strtok_r(nullptr, " \t\n", &save)) {
			unsigned major, minor;
			unsigned long long number;
			char after;
			if (sscanf(word, "%x:%x:%llu%c", &major, &minor, &number, &after) == 3) {
				locked = number == static_cast<unsigned long long>(inode);
				break;
			}
		}
	}
	free(line);
	fclose(locks);
	return locked;
}
#else
// The application is built for Linux and Windows only.
bool SameExtendedAttributes(int, int) { return true; }
bool InodeIsLocked(ino_t) { return false; }
#endif

} // namespace

SaveMethod ChooseSaveMethod(const SaveTargetFacts &facts)
{
	if (facts.changed)
		return SaveMethod::Refuse;
	if (!facts.exists)
		return facts.tempMade ? SaveMethod::Replace : SaveMethod::Refuse;
	if (!facts.writable)
		return SaveMethod::Refuse;
	if (S_ISFIFO(facts.type) || S_ISCHR(facts.type))
		return SaveMethod::WriteThrough;
	if (!S_ISREG(facts.type))
		return SaveMethod::Refuse;
	if (!facts.tempMade)
		return SaveMethod::Rewrite;
	bool renameKeepsTheFile = facts.links == 1 && !facts.lockedByOthers && facts.uid == facts.tempUid &&
		facts.gid == facts.tempGid && facts.mode == facts.tempMode && facts.device == facts.tempDevice &&
		facts.sameExtendedAttributes;
	return renameKeepsTheFile ? SaveMethod::Replace : SaveMethod::Rewrite;
}

SaveFile::~SaveFile()
{
	Discard();
}

bool SaveFile::TargetUnchanged() const
{
	if (folderFd < 0)
		return false;
	struct stat now;
	if (fstatat(folderFd, name.c_str(), &now, AT_SYMLINK_NOFOLLOW) != 0)
		return pinFd < 0 && errno == ENOENT;
	// pinFd keeps the inode, so its number cannot be given to another file
	return pinFd >= 0 && SameFile(now, pinned) && (now.st_mode & S_IFMT) == (pinned.st_mode & S_IFMT);
}

int SaveFile::ReopenTarget(int flags) const
{
	if (pinFd < 0) {
		errno = ENOENT;
		return -1;
	}
	int fd = open(ProcPathOf(pinFd).c_str(), flags | O_CLOEXEC | O_NOCTTY);
	if (fd < 0 && errno == ENOENT) {
		// without /proc: the name once more, which must still be the file
		fd = openat(folderFd, name.c_str(), flags | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY);
	}
	if (fd < 0)
		return -1;
	struct stat now;
	if (fstat(fd, &now) != 0 || !SameFile(now, pinned)) {
		close(fd);
		errno = ESTALE;
		return -1;
	}
	return fd;
}

std::string SaveFile::PathOf(int at, const std::string &file) const
{
	if (at == AT_FDCWD)
		return file;
	std::string folder = FolderOf(target);
	return (folder == "/" ? "" : folder) + "/" + file;
}

SaveTargetFacts SaveFile::ReadFacts()
{
	SaveTargetFacts facts;
	facts.changed = !TargetUnchanged();
	struct stat targetStat;
	if (pinFd >= 0) {
		facts.exists = true;
		if (fstat(pinFd, &targetStat) == 0) {
			facts.type = targetStat.st_mode & S_IFMT;
			facts.writable = CanWrite(pinFd);
			facts.links = targetStat.st_nlink;
			facts.uid = targetStat.st_uid;
			facts.gid = targetStat.st_gid;
			facts.mode = targetStat.st_mode & 07777;
			facts.device = targetStat.st_dev;
			facts.lockedByOthers = S_ISREG(targetStat.st_mode) && InodeIsLocked(targetStat.st_ino);
		}
		else {
			// a target that cannot be looked at is not saved to
			facts.writable = false;
		}
	}
	if (facts.changed)
		return facts;

	if (stageNextToTarget && stageFd >= 0) {
		if (facts.exists && S_ISREG(facts.type)) {
			// The group first, since changing it clears the set-group-ID bit.
			// A group or mode that cannot be set shows in the facts.
			(void)!fchown(stageFd, static_cast<uid_t>(-1), targetStat.st_gid);
			(void)!fchmod(stageFd, facts.mode);
		}
		struct stat tempStat;
		if (fstat(stageFd, &tempStat) == 0) {
			facts.tempMade = true;
			facts.tempUid = tempStat.st_uid;
			facts.tempGid = tempStat.st_gid;
			facts.tempMode = tempStat.st_mode & 07777;
			facts.tempDevice = tempStat.st_dev;
			facts.sameExtendedAttributes = !facts.exists || SameExtendedAttributes(pinFd, stageFd);
		}
	}
	return facts;
}

bool SaveFile::Open(const std::string &path)
{
	Discard();
	target = path;
	failed = false;
	keptBackup.clear();
	method = SaveMethod::Refuse;
	stageNextToTarget = false;

	// The folder and the file are looked up here, once. Everything later
	// reaches them through these descriptors, so a name another program
	// changes in the meantime cannot lead the save to another file; a link
	// the user saved through was resolved before, see ResolveSaveTarget, and
	// a link at the resolved name now is refused.
	name = NameOf(path);
	if (name.empty() || name == "." || name == "..")
		return false;
	folderFd = open(FolderOf(path).c_str(), folderFlags);
	if (folderFd < 0)
		return false;
	pinFd = openat(folderFd, name.c_str(), pinFlags);
	bool exists = pinFd >= 0;
	if (exists && (fstat(pinFd, &pinned) != 0 || S_ISLNK(pinned.st_mode) || !CanWrite(pinFd))) {
		// a file that cannot be written is not replaced either
		Discard();
		return false;
	}
	if (!exists && errno != ENOENT) {
		Discard();
		return false;
	}
	if (!exists || S_ISREG(pinned.st_mode)) {
		stageFd = MakeTempFileNear(folderFd, name + ".", ".hikarisub-save.", 0666, &stageName, &tempPrefix);
		stageNextToTarget = stageFd >= 0;
		if (stageNextToTarget)
			stageAt = folderFd;
	}

	method = ChooseSaveMethod(ReadFacts());
	if (method == SaveMethod::Refuse) {
		Discard();
		return false;
	}
	if (stageFd < 0) {
		// The target's folder takes no temporary file, or the target is no
		// regular file: the new contents wait in the temporary folder.
		tempPrefix = TempFolder() + "/hikarisub-save.";
		stageAt = AT_FDCWD;
		stageFd = MakeTempFile(AT_FDCWD, tempPrefix, 0600, &stageName);
		if (stageFd < 0) {
			Discard();
			return false;
		}
	}
	return true;
}

bool SaveFile::Write(const void *data, size_t size)
{
	if (stageFd < 0 || failed)
		return false;
	if (!WriteAll(stageFd, static_cast<const char *>(data), size, nullptr))
		failed = true;
	return !failed;
}

bool SaveFile::Commit()
{
	if (stageFd < 0)
		return false;
	bool saved = false;
	if (!failed) {
		// While the contents were written, another program may have linked,
		// locked, chmodded or removed the target, or put another file, a link
		// or a folder at its name. The method is picked again from the file
		// the save started with, read through the descriptor Open keeps; a
		// name that no longer leads to that file, or a new file's name that
		// is taken now, refuses the save, which then writes nothing.
		method = ChooseSaveMethod(ReadFacts());
		if (beforeWrite)
			beforeWrite();
		switch (method) {
		case SaveMethod::Replace: saved = Replace(); break;
		case SaveMethod::Rewrite: saved = Rewrite(); break;
		case SaveMethod::WriteThrough: saved = WriteThrough(); break;
		case SaveMethod::Refuse: break;
		}
	}
	Discard();
	return saved;
}

void SaveFile::Discard()
{
	if (stageFd >= 0) {
		close(stageFd);
		stageFd = -1;
	}
	if (!stageName.empty()) {
		unlinkat(stageAt, stageName.c_str(), 0);
		stageName.clear();
	}
	stageAt = AT_FDCWD;
	if (pinFd >= 0) {
		close(pinFd);
		pinFd = -1;
	}
	if (folderFd >= 0) {
		close(folderFd);
		folderFd = -1;
	}
}

bool SaveFile::Replace()
{
	// the temporary file is in the target's folder, see ChooseSaveMethod
	if (fsync(stageFd) != 0)
		return false;
	// A rename puts the new file under the name and writes nothing into what
	// was there: a file or link put at the name after the check above only
	// loses that name. A new file's name taken in the meantime fails it.
	int renamed = pinFd >= 0 ? renameat(folderFd, stageName.c_str(), folderFd, name.c_str())
		: RenameNoReplace(folderFd, stageName.c_str(), name.c_str());
	if (renamed == 0) {
		stageName.clear();
		SyncFolder(folderFd, name);
		return true;
	}
	// A file mounted over the path, or a folder that lets only a file's owner
	// replace it, still takes the contents in place.
	int error = errno;
	if ((error == EBUSY || error == EXDEV || error == EPERM || error == EACCES) && pinFd >= 0 &&
		S_ISREG(pinned.st_mode) && TargetUnchanged()) {
		method = SaveMethod::Rewrite;
		return Rewrite();
	}
	return false;
}

bool SaveFile::Rewrite()
{
	// the new contents are on disk before the target is touched
	struct stat stageStat;
	if (fsync(stageFd) != 0 || fstat(stageFd, &stageStat) != 0)
		return false;
	// the file the save started with, not what its name leads to now
	int out = ReopenTarget(O_RDWR);
	if (out < 0)
		return false;
	struct stat outStat;
	if (fstat(out, &outStat) != 0 || !S_ISREG(outStat.st_mode)) {
		close(out);
		return false;
	}

	// a backup of the old contents, also on disk before the target is touched
	// and its name in its folder, so the backup outlasts a crash
	std::string backupName, backupPrefix;
	int backup = MakeTempFileNear(stageAt, tempPrefix + "old.", ".hikarisub-old.", 0600, &backupName, &backupPrefix);
	struct stat backupStat;
	bool saved = backup >= 0 && CopyAll(out, backup, nullptr) && fsync(backup) == 0 &&
		fstat(backup, &backupStat) == 0;
	if (saved)
		SyncFolder(stageAt, backupName);
	if (saved) {
		saved = lseek(out, 0, SEEK_SET) == 0 && CopyAll(stageFd, out, targetWriter) &&
			ftruncate(out, stageStat.st_size) == 0 && fsync(out) == 0;
		if (!saved) {
			bool restored = lseek(out, 0, SEEK_SET) == 0 && CopyAll(backup, out, nullptr) &&
				ftruncate(out, backupStat.st_size) == 0 && fsync(out) == 0;
			if (!restored) {
				keptBackup = PathOf(stageAt, backupName);
				backupName.clear();
			}
		}
	}
	if (backup >= 0)
		close(backup);
	if (!backupName.empty())
		unlinkat(stageAt, backupName.c_str(), 0);
	close(out);
	return saved;
}

bool SaveFile::WriteThrough()
{
	// Without O_NONBLOCK, opening a FIFO that no program reads waits for a
	// reader, which would hang the save; with it the open fails with ENXIO.
	int out = ReopenTarget(O_WRONLY | O_NONBLOCK);
	if (out < 0)
		return false;
	// The writes block again as a plain write does: a reader that is there
	// takes the contents at its own pace.
	struct stat outStat;
	int flags = fcntl(out, F_GETFL);
	bool saved = fstat(out, &outStat) == 0 && (S_ISFIFO(outStat.st_mode) || S_ISCHR(outStat.st_mode)) &&
		flags != -1 && fcntl(out, F_SETFL, flags & ~O_NONBLOCK) == 0 && CopyAll(stageFd, out, targetWriter);
	if (close(out) != 0)
		saved = false;
	return saved;
}

#endif
