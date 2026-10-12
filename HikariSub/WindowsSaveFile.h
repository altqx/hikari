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

#ifdef _WIN32

#include <stddef.h>
#include <stdio.h>
#include <string>
#include <vector>

enum class FileAccess;

// The volume serial number and the 128-bit file id FileIdInfo gives, which
// tell one file from another on a volume; on a volume or a Windows without
// it, the 64-bit file index in its low half.
struct WindowsFileId
{
	unsigned long long volume = 0;
	unsigned char index[16] = {};
	// A volume that has no file ids, as some network redirectors, gives all
	// zeros or all ones: that tells no file from another.
	bool Known() const
	{
		bool zeros = true, ones = true;
		for (unsigned char byte : index) {
			zeros = zeros && byte == 0;
			ones = ones && byte == 0xFF;
		}
		return !zeros && !ones;
	}
	bool operator==(const WindowsFileId &other) const
	{
		if (volume != other.volume) { return false; }
		for (int i = 0; i < 16; ++i) {
			if (index[i] != other.index[i]) { return false; }
		}
		return true;
	}
};

// One save of a file on Windows: Open makes a temporary file next to the
// file, Write appends to it and Commit puts it in place. The temporary file
// replaces the file with ReplaceFileW, which keeps the file's attributes and
// ACLs; a file with several hard links, or one ReplaceFileW cannot replace
// because another program has it open, gets the new contents copied into it
// instead, with a backup of the old contents put back when that fails. Unlike
// wxTempFile, a failed replace is never followed by a copy over the file
// without a backup. A file that is not committed keeps its contents; a
// private one may keep the private DACL Commit gave it, see Open.
// Open notes which file is at the name, looked at without following a link
// there, and refuses a file whose volume gives no file id; Commit saves only
// while the name still leads to that file, and the copy writes only into it.
// The target's folder is kept open meanwhile, without sharing deletes, so it
// cannot be renamed, removed or replaced; Commit also checks that the folder's
// path still leads to it. The temporary files and the old file a replace moves
// aside are deleted through a handle checked to be on them, never by a name
// looked up again. A new file's temporary file is moved to its name through
// the handle it was written through. ReplaceFileW takes the temporary file
// by its name once that handle is closed: a program that may delete or rename
// files in the folder could put another file there in that moment, as it
// could put one at the file's name right after the save. The private folders
// let no other user do either.
// Every file a save puts contents into, the temporary file and a rewrite's
// backup, is open to no other program while it is written, and lets in no
// one the saved file keeps out: it is made with a DACL that lets in only the
// user, SYSTEM and Administrators, or given that DACL before anything is
// written into it. Where a volume refuses both, as some network shares do, it
// keeps its folder's ACEs only when those let in no one the file that is
// there does not; a new file's temporary file always has them, as the new file
// gets them. A save that cannot make such a file is not made, and the file
// keeps its contents.
class WindowsSaveFile
{
public:
	WindowsSaveFile() = default;
	WindowsSaveFile(const WindowsSaveFile &) = delete;
	WindowsSaveFile &operator=(const WindowsSaveFile &) = delete;
	~WindowsSaveFile() { Discard(); }
	// With privateFile, the saved file lets in only this user, SYSTEM and
	// Administrators: a new one is made so, and one that is there is given
	// that DACL in Commit before the new contents are in it, or is not
	// saved when it cannot be given it or another user owns it. It keeps
	// that DACL when the save fails after it, which leaves no one out who
	// should be let in. The temporary files are made so too, or not at all.
	// On a volume that keeps no ACLs, as FAT, every user may open every
	// file, and a private file is as private as the volume lets it be, as on
	// POSIX. MakePrivateFolder refuses such a folder: a caller whose file
	// must be kept from other users saves only into a folder it accepted.
	bool Open(const std::wstring &target, bool privateFile = false);
	bool IsOpened() const { return file != nullptr; }
	bool Write(const void *data, size_t size);
	bool Commit();
	void Discard();
	// After a save failed half way and the old contents could not be put
	// back, they are kept under this name.
	const std::wstring &KeptBackup() const { return keptBackup; }
	// whether Commit wrote into the file or put another one at its name,
	// also when it then failed, see SaveFile::TargetTouched; the private DACL
	// Commit gives a file before that is not counted, as it changes neither
	// the contents nor the time of the last write
	bool TargetTouched() const { return touched; }
	// whether Open or Commit failed because a temporary file in the folder
	// would have let in someone the file keeps out, see Open
	bool TempNotPrivate() const { return tempNotPrivate; }

private:
	bool Replace();
	bool Rewrite();
	// moves the temporary file of a new file to its name, see Commit
	bool MoveIntoPlace();
	// who may open the temporary files: see Open
	FileAccess TempAccess() const;
	// makes a temporary file next to the target, see MakeTempFileNear
	std::wstring MakeTemp(void **handle, WindowsFileId *id);
	// gives the private file that is there the DACL of a new one
	bool NarrowSaved();
	// whether the path of the target's folder still leads to the folder
	// Open kept open
	bool FolderUnchanged() const;
	void CloseFile();

	// the temporary file, open from Open on, shared with nobody; a HANDLE,
	// nullptr when it is closed
	void *file = nullptr;
	std::wstring target;
	// the target's folder, its path and its id, kept open from Open on when
	// it can be opened; a HANDLE
	void *folder = nullptr;
	bool folderOpened = false;
	std::wstring folderPath;
	WindowsFileId folderId;
	// a file was at target when Open ran, and its id
	bool targetExisted = false;
	WindowsFileId targetId;
	// The security descriptor of the file at target when Open ran, self
	// relative: the temporary files let in no one its DACL keeps out. Empty
	// for a file that keeps nobody out, as one on a volume that keeps no
	// ACLs. Not known when it could not be read, and the temporary files are
	// then private.
	bool targetSecurityKnown = false;
	std::vector<unsigned char> targetSecurity;
	std::wstring temp;
	WindowsFileId tempId;
	std::wstring keptBackup;
	bool touched = false;
	bool tempNotPrivate = false;
	bool privateSave = false;
};

// The Windows part of MakePrivateFolder, for the folder at the absolute
// path folder, whose parent is there: see MakePrivateFolder.
bool MakePrivateFolderOnWindows(const std::wstring &folder);

// The Windows part of OpenPrivateFile: a file or link at path is deleted and
// the file made anew with the DACL MakePrivateFolder gives, or not at all.
FILE *OpenPrivateFileOnWindows(const std::wstring &path);

// What the tests make a volume refuse, as a network share that refuses them
// does; the application is built with nothing refused. securityAtCreate: a
// file or folder made with a security descriptor is refused, so it is made
// with its folder's ACEs. securityChange: giving a file or folder the
// private DACL fails.
struct WindowsSecurityRefusals
{
	bool securityAtCreate = false;
	bool securityChange = false;
};

#ifdef HIKARISUB_TESTING
// Only in the tests, which define HIKARISUB_TESTING.

// Whether the DACL of the security descriptor descriptor lets in no one, for
// reading a file or for giving themselves the right to, that the DACL of
// reference keeps out, apart from the user, SYSTEM and Administrators; both
// are SECURITY_DESCRIPTORs. What is not looked into, as a callback ACE, is
// taken to let more in. A temporary file that keeps its folder's ACEs is kept
// only when it lets in no more than the file it saves, see WindowsSaveFile.
bool DescriptorLetsInNoMoreThan(const void *descriptor, const void *reference);

// Makes the steps that keep a file or folder private refuse what refusals
// says, for the whole process; the tests run on one thread.
void SetWindowsSecurityRefusalsForTesting(const WindowsSecurityRefusals &refusals);
#endif

#endif
