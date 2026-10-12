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

// Makes a new file named prefix and six random letters, created as a new
// file is, so the umask and a folder's default ACL apply to mode.
int MakeTempFile(const std::string &prefix, mode_t mode, std::string *path)
{
	static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	thread_local std::mt19937_64 random{ std::random_device{}() };
	for (int attempt = 0; attempt < 100; ++attempt) {
		std::string name = prefix;
		for (int i = 0; i < 6; ++i)
			name += letters[random() % (sizeof(letters) - 1)];
		int fd = open(name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOCTTY, mode);
		if (fd >= 0) {
			*path = name;
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

// MakeTempFile with prefix, or with a short name in the same folder when
// prefix and the six letters make a name longer than the file system takes.
int MakeTempFileNear(const std::string &prefix, const char *shortName, mode_t mode, std::string *path,
	std::string *usedPrefix)
{
	*usedPrefix = prefix;
	int fd = MakeTempFile(prefix, mode, path);
	if (fd < 0 && errno == ENAMETOOLONG) {
		std::string folder = FolderOf(prefix);
		*usedPrefix = (folder == "/" ? "" : folder) + "/" + shortName;
		fd = MakeTempFile(*usedPrefix, mode, path);
	}
	return fd;
}

std::string TempFolder()
{
	const char *folder = getenv("TMPDIR");
	return (folder && *folder) ? folder : "/tmp";
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

// Writes the folder's entries to disk after a rename or a new file in it.
void SyncFolderOf(const std::string &path)
{
	int fd = open(FolderOf(path).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
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

bool SameExtendedAttributes(const std::string &target, int tempFd)
{
	std::map<std::string, std::string> targetAttributes, tempAttributes;
	return ReadAttributes(target.c_str(), -1, &targetAttributes) &&
		ReadAttributes(nullptr, tempFd, &tempAttributes) && targetAttributes == tempAttributes;
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
bool SameExtendedAttributes(const std::string &, int) { return true; }
bool InodeIsLocked(ino_t) { return false; }
#endif

} // namespace

SaveMethod ChooseSaveMethod(const SaveTargetFacts &facts)
{
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

SaveTargetFacts SaveFile::ReadFacts()
{
	SaveTargetFacts facts;
	struct stat targetStat;
	if (stat(target.c_str(), &targetStat) == 0) {
		facts.exists = true;
		facts.type = targetStat.st_mode & S_IFMT;
		facts.writable = access(target.c_str(), W_OK) == 0;
		facts.links = targetStat.st_nlink;
		facts.uid = targetStat.st_uid;
		facts.gid = targetStat.st_gid;
		facts.mode = targetStat.st_mode & 07777;
		facts.device = targetStat.st_dev;
		facts.lockedByOthers = S_ISREG(targetStat.st_mode) && InodeIsLocked(targetStat.st_ino);
	}
	else if (errno != ENOENT) {
		// a target that cannot be looked at is not saved to
		facts.exists = true;
		facts.writable = false;
	}

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
			facts.sameExtendedAttributes = !facts.exists || SameExtendedAttributes(target, stageFd);
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

	struct stat targetStat;
	bool exists = stat(path.c_str(), &targetStat) == 0;
	if (!exists && errno != ENOENT)
		return false;
	// a file that cannot be written is not replaced either
	if (exists && access(path.c_str(), W_OK) != 0)
		return false;
	if (!exists || S_ISREG(targetStat.st_mode)) {
		stageFd = MakeTempFileNear(path + ".", ".hikarisub-save.", 0666, &stagePath, &tempPrefix);
		stageNextToTarget = stageFd >= 0;
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
		stageFd = MakeTempFile(tempPrefix, 0600, &stagePath);
		if (stageFd < 0)
			return false;
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
		// locked, replaced, chmodded or removed the target: the method is
		// picked again for the file that is there now.
		method = ChooseSaveMethod(ReadFacts());
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
	if (!stagePath.empty()) {
		unlink(stagePath.c_str());
		stagePath.clear();
	}
}

bool SaveFile::Replace()
{
	if (fsync(stageFd) != 0)
		return false;
	if (rename(stagePath.c_str(), target.c_str()) == 0) {
		stagePath.clear();
		SyncFolderOf(target);
		return true;
	}
	// A file mounted over the path, or a folder that lets only a file's owner
	// replace it, still takes the contents in place.
	int error = errno;
	struct stat targetStat;
	if ((error == EBUSY || error == EXDEV || error == EPERM || error == EACCES) &&
		stat(target.c_str(), &targetStat) == 0 && S_ISREG(targetStat.st_mode)) {
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
	int out = open(target.c_str(), O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (out < 0)
		return false;
	struct stat outStat;
	if (fstat(out, &outStat) != 0 || !S_ISREG(outStat.st_mode)) {
		close(out);
		return false;
	}

	// a backup of the old contents, also on disk before the target is touched
	// and its name in its folder, so the backup outlasts a crash
	std::string backupPath, backupPrefix;
	int backup = MakeTempFileNear(tempPrefix + "old.", ".hikarisub-old.", 0600, &backupPath, &backupPrefix);
	struct stat backupStat;
	bool saved = backup >= 0 && CopyAll(out, backup, nullptr) && fsync(backup) == 0 &&
		fstat(backup, &backupStat) == 0;
	if (saved)
		SyncFolderOf(backupPath);
	if (saved) {
		saved = lseek(out, 0, SEEK_SET) == 0 && CopyAll(stageFd, out, targetWriter) &&
			ftruncate(out, stageStat.st_size) == 0 && fsync(out) == 0;
		if (!saved) {
			bool restored = lseek(out, 0, SEEK_SET) == 0 && CopyAll(backup, out, nullptr) &&
				ftruncate(out, backupStat.st_size) == 0 && fsync(out) == 0;
			if (!restored) {
				keptBackup = backupPath;
				backupPath.clear();
			}
		}
	}
	if (backup >= 0)
		close(backup);
	if (!backupPath.empty())
		unlink(backupPath.c_str());
	close(out);
	return saved;
}

bool SaveFile::WriteThrough()
{
	// Without O_NONBLOCK, opening a FIFO that no program reads waits for a
	// reader, which would hang the save; with it the open fails with ENXIO.
	int out = open(target.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
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
