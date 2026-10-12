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
	//atomic. On Windows it replaces filename, and a file whose folder takes
	//no temporary file is not written.
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
	wxTempFile tempFile;
#else
	SaveFile tempFile;
#endif
	bool useTemp = false;
	bool writeFailed = false;
};


