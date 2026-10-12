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

#include <stddef.h>
#include <sys/types.h>
#include <string>

// How a save puts its new contents into the file it saves to.
enum class SaveMethod {
	// Nothing is written: the target is a folder, a socket or a block device,
	// a file that cannot be written, or a new file that cannot be made.
	Refuse,
	// A temporary file next to the target is renamed over it. This is atomic,
	// but it gives the path a new inode, so it is used only when the new file
	// is the same as the old one apart from its contents.
	Replace,
	// The new contents are copied into the target's own inode, and a backup
	// of the old contents is copied back when that fails.
	Rewrite,
	// The new contents are written straight into a FIFO or character device.
	// A FIFO that no program reads fails the save instead of waiting for one.
	WriteThrough,
};

// What a save knows about the file it saves to and about the temporary file
// it made next to it, after giving that file the target's group and mode.
struct SaveTargetFacts {
	bool exists = false;
	mode_t type = 0;  // the target's S_IFMT bits
	bool writable = true;
	nlink_t links = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	mode_t mode = 0;  // the target's permission bits, 07777
	dev_t device = 0;
	bool tempMade = false;
	uid_t tempUid = 0;
	gid_t tempGid = 0;
	mode_t tempMode = 0;
	dev_t tempDevice = 0;
	// the temporary file has the same extended attributes, and so ACLs and
	// security labels, as the target
	bool sameExtendedAttributes = true;
};

// A rename is used only for a new file or a regular file with one link that
// the renamed file would match in owner, group, permission bits, extended
// attributes and file system; any other regular file is rewritten in place.
// A file that cannot be written is refused.
SaveMethod ChooseSaveMethod(const SaveTargetFacts &facts);

// One save of a file on POSIX: Open picks the method and makes the
// temporary file, Write appends to it and Commit puts it in place. Commit
// looks at the target again and picks the method anew for the file that is
// there then. A file that is not committed is left as it was; the destructor
// removes the temporary file.
class SaveFile
{
public:
	// writes into the target during a Rewrite or WriteThrough, as write(2)
	using Writer = ssize_t (*)(int fd, const void *data, size_t size);

	SaveFile() = default;
	SaveFile(const SaveFile &) = delete;
	SaveFile &operator=(const SaveFile &) = delete;
	~SaveFile();

	// target is the resolved path, see ResolveSaveTarget
	bool Open(const std::string &target);
	bool IsOpened() const { return stageFd >= 0; }
	bool Write(const void *data, size_t size);
	bool Commit();
	void Discard();
	SaveMethod Method() const { return method; }
	// After a Rewrite failed and the old contents could not be put back
	// either, the backup with the old contents is kept at this path.
	const std::string &KeptBackup() const { return keptBackup; }
	// for tests: replaces write(2) for the target
	void SetTargetWriter(Writer writer) { targetWriter = writer; }

private:
	// the facts about the target and the temporary file as they are now
	SaveTargetFacts ReadFacts();
	bool Replace();
	bool Rewrite();
	bool WriteThrough();

	std::string target;
	std::string stagePath;
	// where the temporary files of this save are made, with a name prefix
	std::string tempPrefix;
	std::string keptBackup;
	int stageFd = -1;
	// the temporary file is in the target's folder and can be renamed over it
	bool stageNextToTarget = false;
	bool failed = false;
	SaveMethod method = SaveMethod::Refuse;
	Writer targetWriter = nullptr;
};

#endif
