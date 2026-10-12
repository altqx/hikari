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
#include <stdio.h>
#ifndef _WIN32
#include "SaveFile.h"
#else
#include "WindowsSaveFile.h"
#endif

// Makes the folder at path, or narrows the one there, so that only its user
// may open it, for the application's own folders that hold copies of
// subtitles or of what is in media, such as the autosaves. On POSIX, mode
// 0700 for a folder the user owns that is no link, see MakePrivateFolderAt.
// On Windows, a protected DACL that lets in only the user, SYSTEM and
// Administrators and is passed on to the files in it, for a folder the user,
// SYSTEM or Administrators own that is no link or junction, on a volume that
// keeps ACLs, which FAT does not; a folder another user owns is not used
// even when its DACL lets only this user in, as its owner may change that.
// The folders on the way are made as any other folder. Returns
// whether the folder now lets in only the user, so that it may be used:
// false for a link, another user's folder or a folder that could not be
// made, which another user may have put there and could swap the files in,
// so nothing is written into it or read from it.
bool MakePrivateFolder(const wxString &path);

// Opens a new file at path for reading and writing that only its user may
// open, for the application's own files such as the audio cache. Whatever
// was at path is not followed and is not written into: on POSIX see
// OpenPrivateFileAt; on Windows a file or link there is deleted and the
// file made anew with the DACL MakePrivateFolder gives. nullptr on a
// failure.
FILE *OpenPrivateFile(const wxString &path);

// Copies the file at from to to, a file in one of the application's
// private folders, as a private save to: it is replaced only when all of it
// is written, and only its user may open it. A link at to is not followed.
bool CopyToPrivateFile(const wxString &from, const wxString &to);

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
	//temporary file is not written there. With privateFile, only the user
	//may open the saved file, see SaveFile::SetPrivate and
	//WindowsSaveFile::Open.
	bool FileWrite(const wxString &filename, const wxString &alltext, bool utf = true, bool privateFile = false);
	void PartFileWrite(const wxString &parttext);
	// PartFileWrite then writes to a temporary file and CommitTemp puts it in
	// place of filename as FileWrite does, so a failed save keeps the old file;
	// when filename is a symbolic link its target is saved and the link kept
	bool OpenTemp(const wxString &filename, bool privateFile = false);
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


