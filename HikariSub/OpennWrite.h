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
// The volume serial number and file index GetFileInformationByHandle gives,
// which tell one file from another on a volume.
struct WindowsFileId
{
	unsigned long volume = 0;
	unsigned long indexHigh = 0;
	unsigned long indexLow = 0;
	bool operator==(const WindowsFileId &other) const
	{
		return volume == other.volume && indexHigh == other.indexHigh && indexLow == other.indexLow;
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
// there; Commit saves only while the name still leads to that file, and the
// copy writes only into it.
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

private:
	bool Replace();
	bool Rewrite();

	wxFile file;
	wxString target;
	// a file was at target when Open ran, and its id
	bool targetExisted = false;
	WindowsFileId targetId;
	wxString temp;
	wxString keptBackup;
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


