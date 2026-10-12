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
#include "SaveTarget.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <map>
#include <random>
#include <vector>
#ifdef __linux__
#include <sys/syscall.h>
#include <sys/xattr.h>
#if defined(SYS_openat2) && __has_include(<linux/openat2.h>)
#include <linux/openat2.h>
#define HIKARISUB_HAVE_OPENAT2 1
#endif
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
// Every file a save writes contents into is made with mode 0600, so no other
// user can open it while it is written; see SaveFile::TakeFinalMetadata.
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

// The mode every file a save writes contents into is made with: the user's
// own, which no other user can open.
const mode_t privateMode = 0600;

// The umask of this process, read without changing it, as another thread may
// be making a file meanwhile; 077 when it cannot be read.
mode_t CurrentUmask()
{
#ifdef __linux__
	FILE *status = fopen("/proc/self/status", "re");
	if (status) {
		char line[256];
		unsigned mask = 0;
		bool found = false;
		while (!found && fgets(line, sizeof(line), status))
			found = sscanf(line, "Umask: %o", &mask) == 1;
		fclose(status);
		if (found)
			return static_cast<mode_t>(mask & 0777);
	}
#endif
	return 077;
}

// The temporary folder TMPDIR names, or /tmp, as an absolute path with no
// link in it, so that a later change of the current folder does not move it;
// empty when it does not resolve.
std::string TempFolder()
{
	const char *folder = getenv("TMPDIR");
	char *resolved = realpath((folder && *folder) ? folder : "/tmp", nullptr);
	if (!resolved)
		return std::string();
	std::string path = resolved;
	free(resolved);
	return path;
}

// path and name joined with one slash
std::string Join(const std::string &folder, const std::string &name)
{
	return (folder == "/" ? "" : folder) + "/" + name;
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

// Writes all of data to fd. With waitMs of 0 or more, fd does not block, and
// a write that would block waits for fd to take more; when it takes nothing
// for waitMs, the write fails with ETIMEDOUT.
bool WriteAll(int fd, const char *data, size_t size, SaveFile::Writer writer, int waitMs = -1)
{
	while (size) {
		ssize_t written = writer ? writer(fd, data, size) : write(fd, data, size);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			if ((errno == EAGAIN || errno == EWOULDBLOCK) && waitMs >= 0) {
				struct pollfd wait = { fd, POLLOUT, 0 };
				int ready;
				do
					ready = poll(&wait, 1, waitMs);
				while (ready < 0 && errno == EINTR);
				if (ready == 0)
					errno = ETIMEDOUT;
				if (ready <= 0)
					return false;
				continue;
			}
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

// Copies all of from, from its start, to the current offset of to; see
// WriteAll for waitMs.
bool CopyAll(int from, int to, SaveFile::Writer writer, int waitMs = -1)
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
		if (!WriteAll(to, buffer.data(), static_cast<size_t>(got), writer, waitMs))
			return false;
		offset += got;
	}
}

// Writes the entries of the folder at to disk after a rename or a new file in
// it.
void SyncFolder(int at)
{
	int fd = openat(at, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
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
	// Without RENAME_NOREPLACE: the file gets the name as a second link,
	// which fails on a name that is taken, and loses the temporary name.
	if (linkat(at, from, at, to, 0) == 0) {
		unlinkat(at, from, 0);
		return 0;
	}
	if (errno == EEXIST)
		return -1;
	// A file system without either: a file put at the name in the meantime
	// is renamed over, which writes nothing into it.
	return renameat(at, from, at, to);
}

// Swaps the names from and to, both in the folder at: 1 when they were
// swapped, 0 when the file system cannot do that and -1 on another failure.
int ExchangeNames(int at, const char *from, const char *to)
{
#if defined(__linux__) && defined(RENAME_EXCHANGE)
	if (renameat2(at, from, at, to, RENAME_EXCHANGE) == 0)
		return 1;
	return errno == EINVAL || errno == ENOSYS ? 0 : -1;
#else
	(void)at;
	(void)from;
	(void)to;
	return 0;
#endif
}

// Keeps SIGPIPE from the thread while it lives: a write into a FIFO whose
// reader went away then fails with EPIPE instead of ending the program, as no
// handler or ignore of SIGPIPE is set. A SIGPIPE raised meanwhile is taken off
// before the thread's mask is put back.
class SigpipeHeld
{
public:
	SigpipeHeld()
	{
		sigemptyset(&pipeOnly);
		sigaddset(&pipeOnly, SIGPIPE);
		sigset_t pending;
		wasPending = sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;
		held = pthread_sigmask(SIG_BLOCK, &pipeOnly, &old) == 0;
	}
	~SigpipeHeld()
	{
		if (!held)
			return;
		sigset_t pending;
		if (!wasPending && sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
			struct timespec now = {};
			while (sigtimedwait(&pipeOnly, nullptr, &now) < 0 && errno == EINTR) {
			}
		}
		pthread_sigmask(SIG_SETMASK, &old, nullptr);
	}
	SigpipeHeld(const SigpipeHeld &) = delete;
	SigpipeHeld &operator=(const SigpipeHeld &) = delete;

private:
	sigset_t pipeOnly;
	sigset_t old;
	bool wasPending = false;
	bool held = false;
};

#ifdef __linux__
const char aclAttribute[] = "system.posix_acl_access";

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

// An access ACL, as the posix_acl_access attribute holds it, without the
// permissions a chmod sets: the owner's, the others' and the mask's, or the
// owning group's in an ACL without a mask. An ACL of only the owner, the
// owning group and others says no more than the mode and is empty.
std::string AclWithoutMode(const std::string &value)
{
	// a little-endian 32-bit version, then entries of a 16-bit tag, 16-bit
	// permissions and a 32-bit id
	const unsigned userObj = 0x01, groupObj = 0x04, mask = 0x10, other = 0x20;
	if (value.size() < 4 || (value.size() - 4) % 8 != 0)
		return value;
	auto tagAt = [&](size_t entry) {
		return static_cast<unsigned>(static_cast<unsigned char>(value[entry])) |
			static_cast<unsigned>(static_cast<unsigned char>(value[entry + 1])) << 8;
	};
	// an ACL with named users or groups has a mask
	bool hasMask = false;
	for (size_t entry = 4; entry < value.size(); entry += 8)
		hasMask = hasMask || tagAt(entry) == mask;
	if (!hasMask)
		return std::string();
	std::string out = value;
	for (size_t entry = 4; entry < out.size(); entry += 8) {
		unsigned tag = tagAt(entry);
		if (tag == userObj || tag == other || tag == mask || (tag == groupObj && !hasMask))
			out[entry + 2] = out[entry + 3] = 0;
	}
	return out;
}

// The attributes of fd, or of the file at path, as a save compares them: the
// access ACL without what the mode says, see AclWithoutMode.
bool ReadComparedAttributes(const char *path, int fd, std::map<std::string, std::string> *attributes)
{
	if (!ReadAttributes(path, fd, attributes))
		return false;
	auto acl = attributes->find(aclAttribute);
	if (acl != attributes->end()) {
		acl->second = AclWithoutMode(acl->second);
		if (acl->second.empty())
			attributes->erase(acl);
	}
	return true;
}

// The attributes of the file targetFd is open on, which may be an O_PATH
// descriptor that the f*xattr calls do not take, and of the temporary file,
// apart from the permissions in their ACLs that the temporary file takes with
// the target's mode.
bool SameExtendedAttributes(int targetFd, int tempFd)
{
	std::map<std::string, std::string> targetAttributes, tempAttributes;
	bool read = ReadComparedAttributes(ProcPathOf(targetFd).c_str(), -1, &targetAttributes);
	if (!read && errno == ENOENT) {
		// without /proc
		targetAttributes.clear();
		read = ReadComparedAttributes(nullptr, targetFd, &targetAttributes);
	}
	return read && ReadComparedAttributes(nullptr, tempFd, &tempAttributes) && targetAttributes == tempAttributes;
}

// The access ACL of the file fd is open on into acl; false when it has none
// or it cannot be read, with errno ENODATA for none.
bool ReadAcl(int fd, std::string *acl)
{
	return ReadAttributeBuffer([&](char *buffer, size_t size) {
		return fgetxattr(fd, aclAttribute, buffer, size);
	}, acl);
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
bool ReadAcl(int, std::string *) { errno = ENOTSUP; return false; }
bool InodeIsLocked(ino_t) { return false; }
#endif

// Whether this process is in group gid, and so may give its files that
// group and keep their set-group-ID bit.
bool InGroup(gid_t gid)
{
	if (geteuid() == 0 || getegid() == gid)
		return true;
	int count = getgroups(0, nullptr);
	if (count <= 0)
		return false;
	std::vector<gid_t> groups(static_cast<size_t>(count));
	count = getgroups(count, groups.data());
	for (int i = 0; i < count; ++i)
		if (groups[static_cast<size_t>(i)] == gid)
			return true;
	return false;
}

} // namespace

int OpenFolderWithoutLinks(const char *path)
{
#ifdef HIKARISUB_HAVE_OPENAT2
	struct open_how how = {};
	how.flags = folderFlags;
	how.resolve = RESOLVE_NO_SYMLINKS;
	int fd;
	do
		fd = static_cast<int>(syscall(SYS_openat2, AT_FDCWD, path, &how, sizeof(how)));
	while (fd < 0 && errno == EINTR);
	// ENOSYS before Linux 5.6; EPERM where a seccomp filter denies it
	if (fd >= 0 || (errno != ENOSYS && errno != EPERM && errno != EINVAL && errno != E2BIG))
		return fd;
#endif
	return open(path, folderFlags);
}

bool ResolveSaveTarget(const char *path, SaveTarget *target, const std::function<void()> &afterResolve)
{
	*target = SaveTarget();
	char *real = realpath(path, nullptr);
	std::string resolved, name;
	struct stat opened;
	bool exists = real != nullptr;
	if (exists) {
		resolved = real;
		free(real);
		name = NameOf(resolved);
		if (afterResolve)
			afterResolve();
		// the kernel's checks, such as fs.protected_symlinks, are made by
		// opening the path the user gave
		int followed = open(path, O_PATH | O_CLOEXEC);
		bool seen = followed >= 0 && fstat(followed, &opened) == 0;
		if (followed >= 0)
			close(followed);
		if (!seen)
			return false;
		resolved = FolderOf(resolved);
	}
	else {
		// No file at path. A link there leads to no file and is refused, and
		// so is a path that cannot be looked at.
		struct stat linkStat;
		if (lstat(path, &linkStat) == 0 || errno != ENOENT)
			return false;
		// A new file: its folder is resolved as a file is, and the name kept.
		std::string given = path;
		name = NameOf(given);
		if (name.empty() || name == "." || name == "..")
			return false;
		std::string folder = FolderOf(given);
		real = realpath(folder.c_str(), nullptr);
		if (!real)
			return false;
		resolved = real;
		free(real);
		if (afterResolve)
			afterResolve();
		int followed = open(folder.c_str(), folderFlags);
		bool seen = followed >= 0 && fstat(followed, &opened) == 0;
		if (followed >= 0)
			close(followed);
		if (!seen)
			return false;
	}
	// resolved is now the folder: what was opened must be found again there,
	// with no link followed on the way. Where links cannot be refused on the
	// way, the folder must at least be no link itself.
	int folder = OpenFolderWithoutLinks(resolved.c_str());
	if (folder < 0)
		return false;
	struct stat folderStat, folderName, named;
	bool same = fstat(folder, &folderStat) == 0 && lstat(resolved.c_str(), &folderName) == 0 &&
		SameFile(folderStat, folderName) &&
		(exists ? fstatat(folder, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 && SameFile(opened, named)
			: SameFile(opened, folderStat));
	close(folder);
	if (!same)
		return false;
	target->path = Join(resolved, name);
	target->folderDevice = folderStat.st_dev;
	target->folderInode = folderStat.st_ino;
	target->exists = exists;
	if (exists) {
		target->device = named.st_dev;
		target->inode = named.st_ino;
	}
	return true;
}

SaveMethod ChooseSaveMethod(const SaveTargetFacts &facts)
{
	if (facts.changed)
		return SaveMethod::Refuse;
	if (!facts.exists)
		return facts.tempMade ? SaveMethod::Replace : SaveMethod::Refuse;
	if (!facts.writable || facts.othersFileInSharedFolder)
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
	// Without /proc this fails, and so does the save: opening the name once
	// more would open whatever is at the name now, a device or a FIFO too,
	// before it could be checked.
	int fd = open(ProcPathOf(pinFd).c_str(), flags | O_CLOEXEC | O_NOCTTY);
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

std::string SaveFile::PathOf(const std::string &file) const
{
	return Join(stageFolder, file);
}

SaveTargetFacts SaveFile::ReadFacts(bool takeFinalMetadata)
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
			// a private file keeps no permission of its group or others
			facts.mode = targetStat.st_mode & (privateFile ? 07700 : 07777);
			facts.device = targetStat.st_dev;
			facts.lockedByOthers = S_ISREG(targetStat.st_mode) && InodeIsLocked(targetStat.st_ino);
			struct stat folderStat;
			facts.othersFileInSharedFolder = fstat(folderFd, &folderStat) != 0 ||
				((folderStat.st_mode & (S_ISVTX | S_IWOTH)) == (S_ISVTX | S_IWOTH) &&
				targetStat.st_uid != geteuid() && targetStat.st_uid != folderStat.st_uid);
		}
		else {
			// a target that cannot be looked at is not saved to
			facts.writable = false;
		}
	}
	if (facts.changed)
		return facts;

	if (stageNextToTarget && stageFd >= 0) {
		struct stat tempStat;
		if (fstat(stageFd, &tempStat) != 0)
			return facts;
		// The temporary file is renamed over a target only when it is the same
		// apart from its contents, so it takes the target's group and mode, or
		// a new file's mode, before the rename; until then it stays 0600. It
		// takes the target's only when it can be renamed over it: a writable
		// file of the user's with one link, no lock and the same extended
		// attributes; so never the set-user-ID bit or the open mode of another
		// user's file, nor the mode of a file whose other links or ACL keep
		// readers out that the mode lets in.
		bool takesTargets = facts.exists && S_ISREG(facts.type) && facts.writable && !facts.othersFileInSharedFolder &&
			facts.uid == tempStat.st_uid && facts.links == 1 && !facts.lockedByOthers;
		facts.sameExtendedAttributes = !facts.exists || SameExtendedAttributes(pinFd, stageFd);
		takesTargets = takesTargets && facts.sameExtendedAttributes;
		if (takeFinalMetadata && (!facts.exists || takesTargets)) {
			if (facts.exists)
				TakeTargetMetadata(targetStat.st_gid, facts.mode);
			else if (!privateFile)
				TakeNewFileMetadata();
			if (fstat(stageFd, &tempStat) != 0)
				return facts;
		}
		facts.tempMade = true;
		facts.tempUid = tempStat.st_uid;
		facts.tempGid = tempStat.st_gid;
		facts.tempMode = tempStat.st_mode & 07777;
		facts.tempDevice = tempStat.st_dev;
		if (!takeFinalMetadata && takesTargets) {
			// what the temporary file would get: the group of a file can be
			// changed to a group its owner is in, and a chmod by a user outside
			// the file's group drops the set-group-ID bit
			bool canTakeGroup = tempStat.st_gid == targetStat.st_gid || InGroup(targetStat.st_gid);
			facts.tempGid = canTakeGroup ? targetStat.st_gid : tempStat.st_gid;
			facts.tempMode = InGroup(facts.tempGid) ? facts.mode : facts.mode & ~static_cast<mode_t>(S_ISGID);
		}
	}
	return facts;
}

void SaveFile::TakeTargetMetadata(gid_t gid, mode_t mode)
{
	// The group first, while the file is still 0600: with another group, the
	// target's group bits would open it to that group. A group that cannot be
	// set leaves the file 0600, and the facts then pick a rewrite.
	struct stat now;
	if (fstat(stageFd, &now) != 0)
		return;
	if (now.st_gid != gid &&
		(fchown(stageFd, static_cast<uid_t>(-1), gid) != 0 || fstat(stageFd, &now) != 0 || now.st_gid != gid))
		return;
	tookFinalMetadata = true;
	(void)!fchmod(stageFd, mode);
}

void SaveFile::TakeNewFileMetadata()
{
	// A new file gets what a file made in its folder with mode 0666 gets, as
	// before the contents were made private: the umask, or a default ACL of
	// the folder, applies. That is learned from such a file, made empty and
	// removed again; without it, the umask applies.
	mode_t mode = 0666 & ~CurrentUmask();
	std::string acl;
	bool hasAcl = false, learned = false;
	std::string probeName;
	int probe = MakeTempFile(stageAt, tempPrefix, 0666, &probeName);
	if (probe >= 0) {
		unlinkat(stageAt, probeName.c_str(), 0);
		struct stat probeStat;
		if (fstat(probe, &probeStat) == 0) {
			hasAcl = ReadAcl(probe, &acl);
			learned = hasAcl || errno == ENODATA || errno == ENOTSUP;
			if (learned)
				mode = probeStat.st_mode & 07777;
		}
		close(probe);
	}
	tookFinalMetadata = true;
#ifdef __linux__
	// Setting the ACL sets the mode it holds too, in one step from the
	// private file to the new file's. A new file without an ACL has none,
	// also when the folder's default ACL left one on the private file.
	std::string stageAcl;
	if (learned && hasAcl)
		(void)!fsetxattr(stageFd, aclAttribute, acl.data(), acl.size(), 0);
	else if (learned && ReadAcl(stageFd, &stageAcl))
		(void)!fremovexattr(stageFd, aclAttribute);
#endif
	(void)!fchmod(stageFd, mode);
}

bool SaveFile::Open(const SaveTarget &resolved)
{
	Discard();
	const std::string &path = resolved.path;
	target = path;
	failed = false;
	touched = false;
	tookFinalMetadata = false;
	keptBackup.clear();
	method = SaveMethod::Refuse;
	stageNextToTarget = false;

	// The folder and the file are looked up here, once, and must be the ones
	// ResolveSaveTarget found. Everything later reaches them through these
	// descriptors, so a name another program changes in the meantime cannot
	// lead the save to another folder or file; a link the user saved through
	// was resolved before, and a link at the resolved path now, in any of its
	// parts, is refused.
	name = NameOf(path);
	if (name.empty() || name == "." || name == "..")
		return false;
	folderFd = OpenFolderWithoutLinks(FolderOf(path).c_str());
	struct stat folderStat;
	if (folderFd < 0 || fstat(folderFd, &folderStat) != 0 || folderStat.st_dev != resolved.folderDevice ||
		folderStat.st_ino != resolved.folderInode) {
		Discard();
		return false;
	}
	pinFd = openat(folderFd, name.c_str(), pinFlags);
	bool exists = pinFd >= 0;
	if (!exists && errno != ENOENT) {
		Discard();
		return false;
	}
	// a file where ResolveSaveTarget found none, or another file than it found
	if (exists != resolved.exists ||
		(exists && (fstat(pinFd, &pinned) != 0 || S_ISLNK(pinned.st_mode) || pinned.st_dev != resolved.device ||
		pinned.st_ino != resolved.inode || !CanWrite(pinFd)))) {
		// a file that cannot be written is not replaced either
		Discard();
		return false;
	}
	if (!exists || S_ISREG(pinned.st_mode)) {
		stageFd = MakeTempFileNear(folderFd, name + ".", ".hikarisub-save.", privateMode, &stageName, &tempPrefix);
		stageNextToTarget = stageFd >= 0;
		if (stageNextToTarget) {
			stageAt = folderFd;
			stageFolder = FolderOf(path);
		}
	}

	method = ChooseSaveMethod(ReadFacts(false));
	if (method == SaveMethod::Refuse) {
		Discard();
		return false;
	}
	if (stageFd < 0) {
		// The target's folder takes no temporary file, or the target is no
		// regular file: the new contents wait in the temporary folder, which
		// is opened now, so that its files are where they were made for the
		// rest of the save.
		stageFolder = TempFolder();
		tempFolderFd = stageFolder.empty() ? -1 : open(stageFolder.c_str(), folderFlags);
		tempPrefix = "hikarisub-save.";
		stageAt = tempFolderFd;
		stageFd = tempFolderFd < 0 ? -1 : MakeTempFile(stageAt, tempPrefix, privateMode, &stageName);
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
		// is taken now, refuses the save, which then writes nothing. Only now,
		// with all of the contents written, does the temporary file take the
		// group and mode it is renamed with.
		method = ChooseSaveMethod(ReadFacts(true));
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
	stageAt = -1;
	stageFolder.clear();
	if (tempFolderFd >= 0) {
		close(tempFolderFd);
		tempFolderFd = -1;
	}
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
	// A new file's name taken since the check above fails the rename. An
	// existing file swaps names with the new file, and then what was at the
	// name is checked, see FinishExchange: whatever another program put there
	// since the check gets its name back. A file system that cannot swap
	// names gets a plain rename, which writes nothing into what was there; a
	// file put at the name in that moment loses the name.
	int renamed;
	if (pinFd < 0) {
		renamed = RenameNoReplace(folderFd, stageName.c_str(), name.c_str());
	}
	else {
		int exchanged = ExchangeNames(folderFd, stageName.c_str(), name.c_str());
		if (exchanged > 0)
			return FinishExchange();
		renamed = exchanged == 0 ? renameat(folderFd, stageName.c_str(), folderFd, name.c_str()) : -1;
	}
	if (renamed == 0) {
		touched = true;
		stageName.clear();
		SyncFolder(folderFd);
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

bool SaveFile::FinishExchange()
{
	// The new file is at the name, and what was there at stageName. It must
	// be the file the save started with, still with one link and no lock: a
	// link made or a lock taken since the check means a rewrite instead.
	struct stat old;
	bool same = fstatat(folderFd, stageName.c_str(), &old, AT_SYMLINK_NOFOLLOW) == 0 && SameFile(old, pinned) &&
		S_ISREG(old.st_mode);
	if (same && old.st_nlink == 1 && !InodeIsLocked(old.st_ino)) {
		touched = true;
		unlinkat(folderFd, stageName.c_str(), 0);
		stageName.clear();
		SyncFolder(folderFd);
		return true;
	}
	if (ExchangeNames(folderFd, stageName.c_str(), name.c_str()) <= 0) {
		// The names cannot be swapped back: the new contents stay at the
		// name, and what was there is kept, not removed with the temporary
		// file.
		touched = true;
		keptBackup = PathOf(stageName);
		stageName.clear();
		return false;
	}
	SyncFolder(folderFd);
	if (!same || !TargetUnchanged())
		return false;
	method = SaveMethod::Rewrite;
	return Rewrite();
}

bool SaveFile::Rewrite()
{
	// The contents go into the target, which keeps its own mode; the
	// temporary file is the user's own again when it took the target's mode
	// for a rename that was not made or was taken back.
	if (tookFinalMetadata) {
		(void)!fchmod(stageFd, privateMode);
		tookFinalMetadata = false;
	}
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
	// a private file loses the permissions of its group and others before
	// the new contents are in it
	if (privateFile && (outStat.st_mode & 077) && fchmod(out, outStat.st_mode & 07700) != 0) {
		close(out);
		return false;
	}

	// a backup of the old contents, also on disk before the target is touched
	// and its name in its folder, so the backup outlasts a crash
	std::string backupName, backupPrefix;
	int backup = MakeTempFileNear(stageAt, tempPrefix + "old.", ".hikarisub-old.", privateMode, &backupName,
		&backupPrefix);
	struct stat backupStat;
	bool saved = backup >= 0 && CopyAll(out, backup, nullptr) && fsync(backup) == 0 &&
		fstat(backup, &backupStat) == 0;
	if (saved)
		SyncFolder(stageAt);
	if (saved) {
		touched = true;
		saved = lseek(out, 0, SEEK_SET) == 0 && CopyAll(stageFd, out, targetWriter) &&
			ftruncate(out, stageStat.st_size) == 0 && fsync(out) == 0;
		if (!saved) {
			bool restored = lseek(out, 0, SEEK_SET) == 0 && CopyAll(backup, out, nullptr) &&
				ftruncate(out, backupStat.st_size) == 0 && fsync(out) == 0;
			if (!restored) {
				keptBackup = PathOf(backupName);
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
	// The writes stay non-blocking: a reader that is there takes the
	// contents at its own pace, and one that takes nothing for stallTimeout
	// fails the save instead of holding the program. A reader that goes away
	// fails it too, with EPIPE and no SIGPIPE.
	struct stat outStat;
	bool saved = fstat(out, &outStat) == 0 && (S_ISFIFO(outStat.st_mode) || S_ISCHR(outStat.st_mode));
	if (saved) {
		SigpipeHeld noSigpipe;
		touched = true;
		saved = CopyAll(stageFd, out, targetWriter, stallTimeout);
	}
	if (close(out) != 0)
		saved = false;
	return saved;
}

#endif
