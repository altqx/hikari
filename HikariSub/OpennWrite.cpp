//  Copyright (c) 2016 - 2026, Marcin Drob
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


#include "OpennWrite.h"
#include "config.h"
#include <wx/filename.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/log.h>
#include "LogHandler.h"
#include <uchardet.h>
#include <limits>
#include <vector>

//the file a save writes, see ResolveSaveTarget
struct ResolvedSave
{
	wxString path;
#ifndef _WIN32
	SaveTarget found;
#endif
};

#ifndef _WIN32
#include "SaveTarget.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

//the file a save to fileName writes, see ResolveSaveTarget
static bool ResolveSaveTarget(const wxString &fileName, ResolvedSave *target)
{
	target->path = fileName;
	if (!ResolveSaveTarget(fileName.fn_str(), &target->found)){ return false; }
	target->path = wxString(target->found.path.c_str(), *wxConvFileName);
	return true;
}

//the bytes wxFile::Write(text, conv) writes
static bool WriteText(SaveFile &file, const wxString &text, const wxMBConv &conv)
{
	if (text.empty()){ return true; }
	const wxCharBuffer bytes = text.mb_str(conv);
	if (!bytes.length()){ return false; }
	return file.Write(bytes.data(), bytes.length());
}

static bool OpenSaveFile(SaveFile &file, const ResolvedSave &target)
{
	return file.Open(target.found);
}

static void LogSaveFailure(const SaveFile &file)
{
	HikariLog(_("Cannot save to file."));
	if (!file.KeptBackup().empty()){
		HikariLog(wxString::Format(_("The old contents of the file are kept in %s."),
			wxString(file.KeptBackup().c_str(), *wxConvFileName)));
	}
}
#else
// wx's wrapper for windows.h, it keeps min and max from becoming macros
#include <wx/msw/wrapwin.h>
#include <sddl.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <random>
#include <string>

static bool WriteText(WindowsSaveFile &file, const wxString &text, const wxMBConv &conv)
{
	return file.Write(text, conv);
}

static void LogSaveFailure(const WindowsSaveFile &file)
{
	HikariLog(_("Cannot save to file."));
	if (!file.KeptBackup().empty()){
		HikariLog(wxString::Format(_("The old contents of the file are kept in %s."), file.KeptBackup()));
	}
}

//The path a save to fileName writes. ReplaceFileW on a symbolic link
//replaces the link with a plain file and leaves its target as it was, so a
//link, or a junction, is followed to the file it leads to. A link that leads
//to no file is refused, as on POSIX. Other reparse points, such as cloud
//placeholders, are saved under their own name.
static bool ResolveSaveTarget(const wxString &fileName, ResolvedSave *resolvedSave)
{
	wxString *target = &resolvedSave->path;
	*target = fileName;
	WIN32_FIND_DATAW found;
	HANDLE find = ::FindFirstFileW(fileName.wc_str(), &found);
	if (find == INVALID_HANDLE_VALUE) { return true; }
	::FindClose(find);
	if (!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !IsReparseTagNameSurrogate(found.dwReserved0)) {
		return true;
	}
	HANDLE handle = ::CreateFileW(fileName.wc_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	std::vector<wchar_t> path(MAX_PATH + 1);
	DWORD length = ::GetFinalPathNameByHandleW(handle, path.data(), static_cast<DWORD>(path.size()),
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (length >= path.size()) {
		path.resize(length + 1);
		length = ::GetFinalPathNameByHandleW(handle, path.data(), static_cast<DWORD>(path.size()),
			FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	}
	::CloseHandle(handle);
	if (length == 0 || length >= path.size()) { return false; }
	//"\\?\C:\dir\file" or "\\?\UNC\server\share\file"
	wxString resolved(path.data(), length);
	wxString rest;
	if (resolved.StartsWith(L"\\\\?\\UNC\\", &rest)) { resolved = L"\\\\" + rest; }
	else if (resolved.StartsWith(L"\\\\?\\", &rest)) { resolved = rest; }
	*target = resolved;
	return true;
}

// Opens the file at path itself, not what a link or junction there leads to,
// for its attributes only; such a handle keeps no other program from
// reading, writing, renaming or deleting the file.
static HANDLE OpenNoFollow(const wxString &path)
{
	return ::CreateFileW(path.wc_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

// The id of the file handle is open on, the number of names it has, and
// whether it is a file that is saved to: no folder, and no link or junction,
// which ResolveSaveTarget has followed before.
static bool ReadFileFacts(HANDLE handle, WindowsFileId *id, DWORD *links, bool *savable)
{
	BY_HANDLE_FILE_INFORMATION info;
	if (!::GetFileInformationByHandle(handle, &info)) { return false; }
	*id = WindowsFileId();
	FILE_ID_INFO idInfo;
	if (::GetFileInformationByHandleEx(handle, FileIdInfo, &idInfo, sizeof(idInfo))) {
		//128 bits, which ReFS needs to tell its files apart
		static_assert(sizeof(idInfo.FileId.Identifier) == sizeof(id->index), "a 128-bit file id");
		id->volume = idInfo.VolumeSerialNumber;
		memcpy(id->index, idInfo.FileId.Identifier, sizeof(id->index));
	}
	else {
		//before Windows 8, or a file system without 128-bit ids: the 64-bit
		//index, in the low half as NTFS puts it in a 128-bit id
		id->volume = info.dwVolumeSerialNumber;
		unsigned long long index = (static_cast<unsigned long long>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
		for (int i = 0; i < 8; ++i) { id->index[i] = static_cast<unsigned char>(index >> (8 * i)); }
	}
	if (links) { *links = info.nNumberOfLinks; }
	if (savable) {
		FILE_ATTRIBUTE_TAG_INFO tag;
		*savable = !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			(!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
			(::GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) &&
			!IsReparseTagNameSurrogate(tag.ReparseTag)));
	}
	return true;
}

enum class NameLeadsTo { Nothing, TheSameFile, AnotherFile };

// What path leads to, looked at without following a link there: nothing, the
// file with id when existed, or another file; links is set to the number of
// names of the file it leads to. A file whose id is not known is never the
// same file.
static NameLeadsTo LookAtName(const wxString &path, bool existed, const WindowsFileId &id, DWORD *links)
{
	*links = 0;
	HANDLE handle = OpenNoFollow(path);
	if (handle == INVALID_HANDLE_VALUE) {
		return ::GetLastError() == ERROR_FILE_NOT_FOUND ? NameLeadsTo::Nothing : NameLeadsTo::AnotherFile;
	}
	WindowsFileId now;
	bool same = ReadFileFacts(handle, &now, links, nullptr) && existed && id.Known() && now == id;
	::CloseHandle(handle);
	return same ? NameLeadsTo::TheSameFile : NameLeadsTo::AnotherFile;
}

// Opens the file with id at path, as the save saw it before, with access and
// share into out. The name is opened without following a link there and
// must lead to that file; ReOpenFile then opens the very file that look
// found, so a link, junction or other file put at the name since is never
// read or written.
static bool OpenSameFile(const wxString &path, const WindowsFileId &id, DWORD access, DWORD share, wxFile *out)
{
	if (!id.Known()) { return false; }
	HANDLE seen = OpenNoFollow(path);
	if (seen == INVALID_HANDLE_VALUE) { return false; }
	WindowsFileId now;
	HANDLE handle = INVALID_HANDLE_VALUE;
	if (ReadFileFacts(seen, &now, nullptr, nullptr) && now == id) {
		handle = ::ReOpenFile(seen, access, share, 0);
	}
	::CloseHandle(seen);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	if (!ReadFileFacts(handle, &now, nullptr, nullptr) || !(now == id)) {
		::CloseHandle(handle);
		return false;
	}
	int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_BINARY);
	if (fd == -1) {
		::CloseHandle(handle);
		return false;
	}
	out->Attach(fd);
	return true;
}

// Marks the file handle is open on to be deleted when its last handle is
// closed. The handle needs DELETE access; no name is looked up.
static bool DeleteByHandle(HANDLE handle)
{
	FILE_DISPOSITION_INFO info = { TRUE };
	return ::SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info)) != 0;
}

// Deletes the file at path only when it is the file with id: the name is
// opened without following a link there, and the file that handle is on is
// deleted through it, so a file another program put at the name is kept.
static bool DeleteSameFile(const wxString &path, const WindowsFileId &id)
{
	if (!id.Known()) { return false; }
	HANDLE handle = ::CreateFileW(path.wc_str(), DELETE | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	WindowsFileId now;
	bool deleted = ReadFileFacts(handle, &now, nullptr, nullptr) && now == id && DeleteByHandle(handle);
	::CloseHandle(handle);
	return deleted;
}

// The handle a wxFile is open on, or INVALID_HANDLE_VALUE.
static HANDLE HandleOf(const wxFile &file)
{
	return file.IsOpened() ? reinterpret_cast<HANDLE>(_get_osfhandle(file.fd())) : INVALID_HANDLE_VALUE;
}

// A security descriptor whose DACL lets only the user this process runs as
// open a file, and takes no ACE from the file's folder; nullptr when it cannot
// be made. LocalFree frees it.
static PSECURITY_DESCRIPTOR OwnerOnlySecurity()
{
	HANDLE token = nullptr;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) { return nullptr; }
	DWORD size = 0;
	::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
	std::vector<BYTE> user(size);
	bool known = size != 0 && ::GetTokenInformation(token, TokenUser, user.data(), size, &size);
	::CloseHandle(token);
	LPWSTR sid = nullptr;
	if (!known || !::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &sid)) {
		return nullptr;
	}
	//protected, so no ACE of the folder is inherited
	std::wstring sddl = L"D:P(A;;FA;;;" + std::wstring(sid) + L")";
	::LocalFree(sid);
	PSECURITY_DESCRIPTOR security = nullptr;
	if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &security, nullptr)) {
		return nullptr;
	}
	return security;
}

// Makes a new file in the folder of target and opens it for reading, writing
// and deleting into file; id is set to its id. CREATE_NEW fails on a name that
// is taken, by a link too, so it never opens a file another program put there.
// wxFileName::CreateTempFileName made the file with GetTempFileName and then
// opened the name again without O_EXCL, which followed a link put there in
// between.
// A file made with no security descriptor gets the ACEs its folder passes on
// to new files, not the ACL of the file it saves to, which may let fewer
// users in; with ownerOnly it is made with one that lets only this user open
// it, see OwnerOnlySecurity.
static wxString MakeTempFileNear(const wxString &target, wxFile *file, WindowsFileId *id, bool ownerOnly)
{
	//target is absolute, see Open
	size_t separator = target.find_last_of(L"\\/");
	if (separator == wxString::npos) { return wxString(); }
	wxString folder = target.substr(0, separator + 1);
	SECURITY_ATTRIBUTES attributes = { static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, FALSE };
	if (ownerOnly) {
		attributes.lpSecurityDescriptor = OwnerOnlySecurity();
		//a file that cannot be made private is not made at all
		if (!attributes.lpSecurityDescriptor) { return wxString(); }
	}
	struct SecurityFree {
		void *security;
		~SecurityFree() { if (security) { ::LocalFree(security); } }
	} freeSecurity{ attributes.lpSecurityDescriptor };
	thread_local std::mt19937 random{ std::random_device{}() };
	for (int attempt = 0; attempt < 100; ++attempt) {
		wxString name = folder + wxString::Format(L"hks%08x.tmp", static_cast<unsigned int>(random()));
		HANDLE handle = ::CreateFileW(name.wc_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, ownerOnly ? &attributes : nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error = ::GetLastError();
			if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) { continue; }
			return wxString();
		}
		int fd = ReadFileFacts(handle, id, nullptr, nullptr) ?
			_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_BINARY) : -1;
		if (fd == -1) {
			DeleteByHandle(handle);
			::CloseHandle(handle);
			return wxString();
		}
		file->Attach(fd);
		return name;
	}
	return wxString();
}

// copies all of from, from its start, to the current offset of to
static bool CopyContents(wxFile &from, wxFile &to)
{
	if (from.Seek(0) != 0) { return false; }
	std::vector<char> buffer(1 << 16);
	for (;;) {
		ssize_t got = from.Read(buffer.data(), buffer.size());
		if (got < 0) { return false; }
		if (got == 0) { return true; }
		if (to.Write(buffer.data(), static_cast<size_t>(got)) != static_cast<size_t>(got)) { return false; }
	}
}

// Opens the folder at path for listing, as a save keeps it; with share, the
// modes other programs may open it in meanwhile.
static HANDLE OpenFolder(const wxString &path, DWORD access, DWORD share)
{
	return ::CreateFileW(path.wc_str(), access, share, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
}

bool WindowsSaveFile::Open(const wxString &name, bool privateFile)
{
	Discard();
	keptBackup.clear();
	touched = false;
	//an absolute name puts the temporary file next to the file, see wxTempFile::Open
	wxFileName fn(name);
	if (!fn.IsAbsolute()) { fn.Normalize(wxPATH_NORM_ABSOLUTE); }
	target = fn.GetFullPath();
	size_t separator = target.find_last_of(L"\\/");
	if (separator == wxString::npos) { return false; }
	//The folder stays open until the save ends. Opened for listing and not
	//shared for deleting, it cannot be renamed, removed or replaced with a
	//junction meanwhile. A folder that cannot be listed is saved to without
	//this.
	folderPath = target.substr(0, separator);
	if (folderPath.empty() || folderPath.Last() == L':') { folderPath += L"\\"; }
	HANDLE folderHandle = OpenFolder(folderPath, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE);
	if (folderHandle != INVALID_HANDLE_VALUE) {
		folder = folderHandle;
		folderOpened = true;
		if (!ReadFileFacts(folderHandle, &folderId, nullptr, nullptr)) { folderId = WindowsFileId(); }
	}
	//the file at the name now; Commit saves only while the name leads to it
	targetExisted = false;
	HANDLE handle = OpenNoFollow(target);
	if (handle != INVALID_HANDLE_VALUE) {
		bool savable = false;
		bool known = ReadFileFacts(handle, &targetId, nullptr, &savable);
		::CloseHandle(handle);
		//a folder, or a link or junction at the resolved name, is not saved
		//to, nor a file on a volume that gives no file id: another file put
		//at its name could not be told from it
		if (!known || !savable || !targetId.Known()) {
			Discard();
			return false;
		}
		targetExisted = true;
	}
	else if (::GetLastError() != ERROR_FILE_NOT_FOUND) {
		Discard();
		return false;
	}
	//The new contents of a file that is there are kept from everybody else
	//while they are written: ReplaceFileW gives the new file the old one's
	//DACL when it replaces it, and a rewrite only copies them into the old
	//file. A new file's temporary file is the new file, which keeps the ACL
	//it is made with when it is moved to the name: the ACEs its folder passes
	//on, as a file made there by any other program gets, no more than the
	//saved file has; a private new file keeps the DACL of this user only.
	temp = MakeTempFileNear(target, &file, &tempId, targetExisted || privateFile);
	if (temp.empty()) {
		Discard();
		return false;
	}
	return true;
}

bool WindowsSaveFile::FolderUnchanged() const
{
	if (!folderOpened || !folderId.Known()) { return true; }
	HANDLE handle = OpenFolder(folderPath, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	WindowsFileId now;
	bool same = ReadFileFacts(handle, &now, nullptr, nullptr) && now == folderId;
	::CloseHandle(handle);
	return same;
}

void WindowsSaveFile::Discard()
{
	if (file.IsOpened()) {
		//file is the temporary file: deleted through its own handle
		if (!temp.empty() && DeleteByHandle(HandleOf(file))) { temp.clear(); }
		file.Close();
	}
	if (!temp.empty()) {
		DeleteSameFile(temp, tempId);
		temp.clear();
	}
	if (folderOpened) {
		::CloseHandle(static_cast<HANDLE>(folder));
		folder = nullptr;
		folderOpened = false;
	}
}

bool WindowsSaveFile::Commit()
{
	if (!file.IsOpened()) { return false; }
	wxLogNull noLog;
	//the new contents are on disk before the file is touched
	bool saved = file.Flush();
	//a folder on the way to the file replaced meanwhile: nothing is written
	if (saved && !FolderUnchanged()) { saved = false; }
	if (saved) {
		DWORD links = 0;
		NameLeadsTo now = LookAtName(target, targetExisted, targetId, &links);
		if (!targetExisted) {
			//a new file; one another program made in the meantime is not
			//replaced, and MoveFileExW without MOVEFILE_REPLACE_EXISTING fails
			//on a name made after this look too
			file.Close();
			saved = now == NameLeadsTo::Nothing &&
				::MoveFileExW(temp.wc_str(), target.wc_str(), MOVEFILE_WRITE_THROUGH) != 0;
			if (saved) { temp.clear(); touched = true; }
		}
		else if (now != NameLeadsTo::TheSameFile) {
			//another program removed the file or put another one, a link or a
			//folder at its name: nothing is written
			saved = false;
		}
		else if (links > 1) {
			//a replace would put the new contents under this one name only
			saved = Rewrite();
		}
		else {
			saved = Replace();
		}
	}
	Discard();
	return saved;
}

bool WindowsSaveFile::Replace()
{
	file.Close();
	//The old file is moved to a name of its own while it is replaced, so a
	//replace that stops half way can be undone. The name is made as a new
	//file first, which ReplaceFileW replaces, so it is no name another
	//program could guess and put a file or link at.
	wxFile reserved;
	WindowsFileId reservedId;
	wxString backup = MakeTempFileNear(target, &reserved, &reservedId, true);
	if (backup.empty()) { return false; }
	reserved.Close();
	//Without REPLACEFILE_IGNORE_MERGE_ERRORS, a replace that cannot give the
	//new file the old one's ACLs, streams and attributes fails with both files
	//under their names, and the new contents are then copied into the old
	//file, which keeps all of them.
	if (::ReplaceFileW(target.wc_str(), temp.wc_str(), backup.wc_str(), 0, nullptr, nullptr)) {
		temp.clear();
		touched = true;
		//the old file; a file another program put at the name in the moment
		//before the replace is not the old file and is left at that name
		DeleteSameFile(backup, targetId);
		return true;
	}
	DWORD links = 0;
	if (LookAtName(backup, true, targetId, &links) == NameLeadsTo::TheSameFile) {
		//ERROR_UNABLE_TO_MOVE_REPLACEMENT_2: the old file was moved away and
		//the new one was not moved in; the old one goes back
		if (LookAtName(target, false, targetId, &links) != NameLeadsTo::Nothing ||
			!::MoveFileExW(backup.wc_str(), target.wc_str(), MOVEFILE_WRITE_THROUGH)) {
			keptBackup = backup;
			touched = true;
		}
		return false;
	}
	DeleteSameFile(backup, reservedId);
	//Nothing was changed: another program has the file open without letting
	//it be deleted, or the old file's ACLs, streams or attributes could not be
	//given to the new one. wxRenameFile then copied the new file over it,
	//which truncates it first; the contents are copied in with a backup
	//instead.
	return Rewrite();
}

bool WindowsSaveFile::Rewrite()
{
	//the temporary file Open made, not what its name leads to now
	if (!file.IsOpened() && !OpenSameFile(temp, tempId, GENERIC_READ | DELETE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &file)) {
		return false;
	}
	wxFileOffset newLength = file.Length();
	wxFile out;
	//the file Open found, not what its name leads to now; a program that has
	//the file open without sharing writes keeps it as it is
	if (newLength < 0 || !targetExisted ||
		!OpenSameFile(target, targetId, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &out)) {
		return false;
	}
	wxFileOffset oldLength = out.Length();
	if (oldLength < 0) { return false; }
	//a backup of the old contents, on disk before the file is touched; only
	//this user may open it, as it would otherwise have the ACEs of the folder
	//and not the file's own, which may keep other users out
	wxFile backup;
	WindowsFileId backupId;
	wxString backupName = MakeTempFileNear(target, &backup, &backupId, true);
	if (backupName.empty()) { return false; }
	bool saved = CopyContents(out, backup) && backup.Flush();
	if (saved) {
		touched = true;
		saved = out.Seek(0) == 0 && CopyContents(file, out) && _chsize_s(out.fd(), newLength) == 0 &&
			out.Flush();
		if (!saved) {
			bool restored = out.Seek(0) == 0 && CopyContents(backup, out) &&
				_chsize_s(out.fd(), oldLength) == 0 && out.Flush();
			if (!restored) { keptBackup = backupName; }
		}
	}
	//the backup is deleted through its own handle
	if (keptBackup.empty()) { DeleteByHandle(HandleOf(backup)); }
	backup.Close();
	out.Close();
	return saved;
}

// wx has no Windows converter for these uchardet charsets and would copy the
// bytes as Latin-1, so they are decoded with their Windows code page instead
static unsigned int CharsetCodePage(const wxString &charset)
{
	static const struct { const wchar_t *name; unsigned int codePage; } codePages[] = {
		{ L"UHC", 949 },
		{ L"TIS-620", 874 },
		{ L"IBM852", 852 },
		{ L"IBM855", 855 },
		{ L"IBM862", 862 },
		{ L"IBM865", 865 },
		{ L"IBM866", 866 },
		{ L"MAC-CENTRALEUROPE", 10029 },
		{ L"MAC-CYRILLIC", 10007 },
		{ L"GB18030", 54936 },
		{ L"HZ-GB-2312", 52936 },
		{ L"ISO-2022-CN", 50227 },
		{ L"ISO-2022-KR", 50225 },
	};
	for (const auto &entry : codePages) {
		if (charset == entry.name)
			return IsValidCodePage(entry.codePage) ? entry.codePage : 0;
	}
	return 0;
}

static wxString DecodeCodePage(unsigned int codePage, const char *buf, size_t size)
{
	if (size > static_cast<size_t>(std::numeric_limits<int>::max())) { return wxString(); }
	int length = MultiByteToWideChar(codePage, 0, buf, static_cast<int>(size), nullptr, 0);
	if (length <= 0) { return wxString(); }
	std::vector<wchar_t> wide(static_cast<size_t>(length));
	length = MultiByteToWideChar(codePage, 0, buf, static_cast<int>(size), wide.data(), length);
	if (length <= 0) { return wxString(); }
	return wxString(wide.data(), static_cast<size_t>(length));
}
#endif


void MakePrivateFolder(const wxString &path)
{
	wxString folder = path;
	while (folder.length() > 1 && (folder.EndsWith(L"/") || folder.EndsWith(L"\\"))) { folder.RemoveLast(); }
#ifndef _WIN32
	if (!wxFileName::DirExists(folder)) { wxFileName::Mkdir(folder, 0700, wxPATH_MKDIR_FULL); }
	//a folder made before, or by an older version, with the umask's mode;
	//a link there, or another user's folder, is left as it is
	int fd = ::open(folder.fn_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) { return; }
	struct stat folderStat;
	if (::fstat(fd, &folderStat) == 0 && folderStat.st_uid == ::geteuid() && (folderStat.st_mode & 077)) {
		(void)!::fchmod(fd, folderStat.st_mode & 07700);
	}
	::close(fd);
#else
	if (!wxFileName::DirExists(folder)) { wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL); }
#endif
}

OpenWrite::OpenWrite()
{

}

OpenWrite::OpenWrite(const wxString &fileName, bool clear)
{
	wxFileName fname;
	fname.Assign(fileName);
	if (!fname.DirExists()){ wxFileName::Mkdir(HikariPathDir(fileName), 511, wxPATH_MKDIR_FULL); }
	if (fname.FileExists() && !fname.IsFileReadable()){ return; }
	if (!file.Exists(fileName)){
		if (!file.Create(fileName, false, wxS_DEFAULT)){ HikariLog(_("Cannot create file.")); }
	}
	else{
		if (!file.Open(fileName, (clear) ? wxFile::write : wxFile::write_append, wxS_DEFAULT)){ HikariLog(_("Cannot open file.")); };
	}
	isfirst = clear;
}

OpenWrite::~OpenWrite()
{
	CloseFile();
}

bool OpenWrite::FileOpen(const wxString &filename, wxString *riddenText, bool test)
{

	bool utf8 = true;
	wxMBConv *conv = NULL;
	unsigned int codePage = 0;
	wxFileName fname;
	fname.Assign(filename);

	if (!fname.IsFileReadable()){ return false; }
	//read the bytes in binary mode; a text mode read stops at Ctrl+Z (0x1A) on Windows
	wxFile filetest;
	if (!filetest.Open(filename, wxFile::read, wxS_DEFAULT)) { return false; }
	wxFileOffset fileLength = filetest.Length();
	if (fileLength < 0 || static_cast<unsigned long long>(fileLength) >
		static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
		return false;
	}
	std::vector<char> buff(static_cast<size_t>(fileLength));
	if (!buff.empty() && filetest.Read(buff.data(), buff.size()) != buff.size()) {
		return false;
	}
	filetest.Close();
	if (buff.empty()) { riddenText->clear(); return false; }
	if (test){
		utf8 = buff.size() >= 3 && static_cast<unsigned char>(buff[0]) == 0xEF &&
			static_cast<unsigned char>(buff[1]) == 0xBB &&
			static_cast<unsigned char>(buff[2]) == 0xBF;
		if (!utf8){
			//uchardet only says UTF-16 or UTF-32, and wxCSConv then keeps the
			//BOM and guesses the byte order; wxConvAuto takes both from the BOM
			wxBOM bom = wxConvAuto::DetectBOM(buff.data(), buff.size());
			utf8 = bom == wxBOM_UTF16LE || bom == wxBOM_UTF16BE ||
				bom == wxBOM_UTF32LE || bom == wxBOM_UTF32BE;
		}
		if (!utf8){
			utf8 = IsUTF8withoutBOM(buff.data(), buff.size());
			if (!utf8) {
				wxString result;
				if (CheckCharSet(buff.data(), buff.size(), &result)) {
					result.MakeUpper();
#ifndef _WIN32
					//glibc's SHIFT_JIS turns 5C into a yen sign and 7E into an
					//overline; CP932, what Windows uses, keeps them ASCII
					if (result == L"SHIFT_JIS")
						result = L"CP932";
#endif
					wxCSConv *csconv = new wxCSConv(result);
					if (!csconv->IsOk()){
						delete csconv;
						csconv = NULL;
#ifdef _WIN32
						codePage = CharsetCodePage(result);
#endif
						//without a converter wxCSConv copies the bytes as Latin-1
						if (!codePage)
							return false;
					}
					conv = csconv;
				}
			}
		}
	}
	wxCSConv convl = wxConvLocal;
	if (utf8){
		*riddenText = wxString(buff.data(), wxConvAuto(), buff.size());
	}
#ifdef _WIN32
	else if (codePage){
		*riddenText = DecodeCodePage(codePage, buff.data(), buff.size());
	}
#endif
	else{ *riddenText = wxString(buff.data(), conv ? *conv : convl, buff.size()); }
	if (conv)
		delete conv;
#ifdef _WIN32
	//the text mode read this replaces turned CRLF into LF
	riddenText->Replace(L"\r\n", L"\n");
#endif
	if (riddenText->empty()) return false;
	return true;
}

bool OpenWrite::FileWrite(const wxString &fileName, const wxString &textfile, bool utf, bool privateFile)
{

	wxFileName fname;
	fname.Assign(fileName);
	if (!fname.DirExists()){ wxFileName::Mkdir(HikariPathDir(fileName), 511, wxPATH_MKDIR_FULL); }
	if (fname.FileExists() && !fname.IsFileReadable()){
		HikariLog(_("Cannot read the file."));
		return false;
	}

	wxString textToWrite = textfile;
	if (utf){
		wchar_t bom = 0xFEFF;
		textToWrite.Prepend(wxString(bom));
	}
	const wxMBConv &conv = (utf) ? (const wxMBConv &)wxConvUTF8 : (const wxMBConv &)wxConvLocal;

	//write a temporary file and replace the file with it only when all of it
	//was written, so a failed or short write leaves the file as it was
	ResolvedSave target;
	if (!ResolveSaveTarget(fileName, &target)){
		HikariLog(_("Cannot save to file."));
		return false;
	}
	fname.Assign(target.path);
	//a file that cannot be written must not be replaced either; on POSIX a
	//rename needs only a writable folder and would replace it
	if (fname.FileExists() && !fname.IsFileWritable()){
		HikariLog(_("Cannot save to file."));
		return false;
	}
#ifndef _WIN32
	//SaveFile renames the temporary file over the file only when that keeps
	//the file's links, owner, group, mode and extended attributes, and else
	//rewrites the file in place with a backup it puts back on a failure
	SaveFile saveFile;
	saveFile.SetPrivate(privateFile);
	if (!OpenSaveFile(saveFile, target) || !WriteText(saveFile, textToWrite, conv) || !saveFile.Commit()){
		LogSaveFailure(saveFile);
		return false;
	}
	return true;
#else
	WindowsSaveFile saveFile;
	bool tempOpened = false;
	{
		wxLogNull noLog;
		tempOpened = saveFile.Open(target.path, privateFile);
	}
	//without a temporary file, in a folder that is not writable, the file is
	//not written either: writing it in place truncates it first, and a failed
	//or short write would then lose it
	if (!tempOpened){
		HikariLog(_("Cannot save to file."));
		return false;
	}
	//the destructor removes the temporary file
	if (!saveFile.Write(textToWrite, conv) || !saveFile.Commit()){
		LogSaveFailure(saveFile);
		return false;
	}
	return true;
#endif
}
void OpenWrite::PartFileWrite(const wxString &parttext)
{
	if (useTemp){
		if (writeFailed){ return; }
		wchar_t bom = 0xFEFF;
		if (!WriteText(tempFile, (isfirst) ? wxString(bom) + parttext : parttext, wxMBConvUTF8())){
			HikariLog(_("Cannot save to file."));
			writeFailed = true;
		}
		isfirst = false;
		return;
	}
	if (!file.IsOpened()){ HikariLog(_("The file was not opened.")); return; }
	if (isfirst){
		wchar_t bom = 0xFEFF;
		if (!file.Write(wxString(bom) + parttext/*,wxConvUTF8*/)){ HikariLog(_("Cannot save to file.")); };
		isfirst = false;
		return;
	}
	if (!file.Write(parttext/*,wxConvUTF8*/)){ HikariLog(_("Cannot save to file.")); };
}

bool OpenWrite::OpenTemp(const wxString &fileName, bool privateFile)
{
	wxFileName fname;
	fname.Assign(fileName);
	if (!fname.DirExists()){ wxFileName::Mkdir(HikariPathDir(fileName), 511, wxPATH_MKDIR_FULL); }
	useTemp = true;
	isfirst = true;
	ResolvedSave target;
	if (!ResolveSaveTarget(fileName, &target)){
		writeFailed = true;
		HikariLog(_("Cannot open file."));
		return false;
	}
	fname.Assign(target.path);
	//a file that cannot be written must not be replaced either
#ifndef _WIN32
	tempFile.SetPrivate(privateFile);
#endif
	writeFailed = (fname.FileExists() && (!fname.IsFileReadable() || !fname.IsFileWritable())) ||
#ifdef _WIN32
		!tempFile.Open(target.path, privateFile);
#else
		!OpenSaveFile(tempFile, target);
#endif
	if (writeFailed){ HikariLog(_("Cannot open file.")); }
	return !writeFailed;
}

bool OpenWrite::CommitTemp()
{
	//on failure the temporary file is removed with tempFile
	if (writeFailed || !tempFile.IsOpened()){ return false; }
	if (!tempFile.Commit()){
		writeFailed = true;
		LogSaveFailure(tempFile);
		return false;
	}
	return true;
}

void OpenWrite::CloseFile()
{
	if (file.IsOpened()){ file.Close(); }
}

bool OpenWrite::IsUTF8withoutBOM(char* buf, size_t size)
{
	if (!buf && size != 0) { return false; }
	bool onlySawAscii = true;
	size_t pos = 0;
	auto isContinuation = [](unsigned char ch) { return ch >= 0x80 && ch <= 0xBF; };
	while (pos < size) {
		unsigned char first = static_cast<unsigned char>(buf[pos++]);
		if (first <= 0x7F) { continue; }
		onlySawAscii = false;

		if (first >= 0xC2 && first <= 0xDF) {
			if (pos >= size || !isContinuation(static_cast<unsigned char>(buf[pos++]))) { return false; }
		}
		else if (first >= 0xE0 && first <= 0xEF) {
			if (pos + 1 >= size) { return false; }
			unsigned char second = static_cast<unsigned char>(buf[pos++]);
			unsigned char third = static_cast<unsigned char>(buf[pos++]);
			if (!isContinuation(third) ||
				(first == 0xE0 ? second < 0xA0 || second > 0xBF :
				 first == 0xED ? second < 0x80 || second > 0x9F : !isContinuation(second))) {
				return false;
			}
		}
		else if (first >= 0xF0 && first <= 0xF4) {
			if (pos + 2 >= size) { return false; }
			unsigned char second = static_cast<unsigned char>(buf[pos++]);
			unsigned char third = static_cast<unsigned char>(buf[pos++]);
			unsigned char fourth = static_cast<unsigned char>(buf[pos++]);
			if (!isContinuation(third) || !isContinuation(fourth) ||
				(first == 0xF0 ? second < 0x90 || second > 0xBF :
				 first == 0xF4 ? second < 0x80 || second > 0x8F : !isContinuation(second))) {
				return false;
			}
		}
		else {
			return false;
		}
	}

	return !onlySawAscii;

}

bool OpenWrite::CheckCharSet(char* buf, size_t size, wxString* result)
{
	uchardet_t detector = uchardet_new();
	if (!detector)
		return false;

	if (uchardet_handle_data(detector, buf, size) != 0) {
		uchardet_delete(detector);
		return false;
	}
	uchardet_data_end(detector);
	const char* encoding = uchardet_get_charset(detector);
	bool detected = encoding && *encoding;
	if (detected)
		*result = wxString(encoding);

	uchardet_delete(detector);

	return detected;
}
