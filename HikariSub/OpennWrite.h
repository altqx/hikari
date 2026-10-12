//  Copyright (c) 2016 - 2026, Marcin Drob

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

#include <wx/file.h>
#include <wx/thread.h>
#ifndef _WIN32
#include "SaveFile.h"
#endif

#ifdef _WIN32
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
// without a backup. A file that is not committed is left as it was.
// Open notes which file is at the name, looked at without following a link
// there, and refuses a file whose volume gives no file id; Commit saves only
// while the name still leads to that file, and the copy writes only into it.
// The target's folder is kept open meanwhile, without sharing deletes, so it
// cannot be renamed, removed or replaced; Commit also checks that the folder's
// path still leads to it. The temporary files and the old file a replace moves
// aside are deleted through a handle checked to be on them, never by a name
// looked up again.
class WindowsSaveFile
{
public:
	WindowsSaveFile() = default;
	WindowsSaveFile(const WindowsSaveFile &) = delete;
	WindowsSaveFile &operator=(const WindowsSaveFile &) = delete;
	~WindowsSaveFile() { Discard(); }
	bool Open(const wxString &target);
	bool IsOpened() const { return file.IsOpened(); }
	bool Write(const wxString &text, const wxMBConv &conv) { return file.Write(text, conv); }
	bool Commit();
	void Discard();
	// After a save failed half way and the old contents could not be put
	// back, they are kept under this name.
	const wxString &KeptBackup() const { return keptBackup; }
	// whether Commit wrote into the file or put another one at its name,
	// also when it then failed, see SaveFile::TargetTouched
	bool TargetTouched() const { return touched; }

private:
	bool Replace();
	bool Rewrite();
	// whether the path of the target's folder still leads to the folder
	// Open kept open
	bool FolderUnchanged() const;

	wxFile file;
	wxString target;
	// the target's folder, its path and its id, kept open from Open on when
	// it can be opened; a HANDLE
	void *folder = nullptr;
	bool folderOpened = false;
	wxString folderPath;
	WindowsFileId folderId;
	// a file was at target when Open ran, and its id
	bool targetExisted = false;
	WindowsFileId targetId;
	wxString temp;
	WindowsFileId tempId;
	wxString keptBackup;
	bool touched = false;
};
#endif

class OpenWrite
{
public:
	OpenWrite();
	OpenWrite(const wxString &filename, bool clear = true);
	~OpenWrite();
	void CloseFile();
	bool FileOpen(const wxString &filename, wxString *riddenText, bool test = true);
	//writes a temporary file and puts it in place of filename only when all
	//of it was written; returns false and keeps the file as it was when it
	//cannot be written, also when it is read-only. On POSIX see SaveFile for
	//how it is put in place: a file whose folder takes no temporary file is
	//rewritten in place with a backup of its old contents, which is not
	//atomic. On Windows see WindowsSaveFile; a file whose folder takes no
	//temporary file is not written there.
	bool FileWrite(const wxString &filename, const wxString &alltext, bool utf = true);
	void PartFileWrite(const wxString &parttext);
	// PartFileWrite then writes to a temporary file and CommitTemp puts it in
	// place of filename as FileWrite does, so a failed save keeps the old file;
	// when filename is a symbolic link its target is saved and the link kept
	bool OpenTemp(const wxString &filename);
	bool CommitTemp();
	// after CommitTemp: whether it changed the file, or what its name leads
	// to, also when it failed
	bool TempTargetTouched() const { return useTemp && tempFile.TargetTouched(); }
	bool IsUTF8withoutBOM(char* buf, size_t size);
	bool CheckCharSet(char* buf, size_t size, wxString *result);
	wxFile file;
private:

	bool isfirst;
#ifdef _WIN32
	WindowsSaveFile tempFile;
#else
	SaveFile tempFile;
#endif
	bool useTemp = false;
	bool writeFailed = false;
};


