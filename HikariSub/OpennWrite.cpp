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

//the bytes wxFile::Write(text, conv) writes, into a SaveFile or a
//WindowsSaveFile
template <typename File>
static bool WriteText(File &file, const wxString &text, const wxMBConv &conv)
{
	if (text.empty()){ return true; }
	const wxCharBuffer bytes = text.mb_str(conv);
	if (!bytes.length()){ return false; }
	return file.Write(bytes.data(), bytes.length());
}

//the file a save writes, see ResolveSaveTarget
struct ResolvedSave
{
	wxString path;
#ifndef _WIN32
	SaveTarget found;
#endif
};

#ifndef _WIN32
#include "PrivateFolder.h"
#include "SaveTarget.h"
#include <sys/stat.h>

//the file a save to fileName writes, see ResolveSaveTarget
static bool ResolveSaveTarget(const wxString &fileName, ResolvedSave *target)
{
	target->path = fileName;
	if (!ResolveSaveTarget(fileName.fn_str(), &target->found)){ return false; }
	target->path = wxString(target->found.path.c_str(), *wxConvFileName);
	return true;
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

//why a save made no temporary file, when it is that one would let in users
//the file keeps out
static void LogTempNotPrivate(const WindowsSaveFile &file)
{
	if (file.TempNotPrivate()){
		HikariLog(_("A temporary file in its folder would let in users the file keeps out."));
	}
}

static void LogSaveFailure(const WindowsSaveFile &file)
{
	HikariLog(_("Cannot save to file."));
	LogTempNotPrivate(file);
	if (!file.KeptBackup().empty()){
		HikariLog(wxString::Format(_("The old contents of the file are kept in %s."),
			wxString(file.KeptBackup().c_str())));
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


bool MakePrivateFolder(const wxString &path)
{
	wxString folder = path;
	while (folder.length() > 1 && (folder.EndsWith(L"/") || folder.EndsWith(L"\\"))) { folder.RemoveLast(); }
	//the folders on the way, such as the program's, are made as before
	wxString parent = wxFileName(folder).GetPath();
	if (!parent.empty() && !wxFileName::DirExists(parent)) {
		wxFileName::Mkdir(parent, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	}
#ifndef _WIN32
	return MakePrivateFolderAt(std::string(folder.fn_str().data()));
#else
	return MakePrivateFolderOnWindows(folder.ToStdWstring());
#endif
}

FILE *OpenPrivateFile(const wxString &path)
{
#ifndef _WIN32
	return OpenPrivateFileAt(std::string(path.fn_str().data()));
#else
	return OpenPrivateFileOnWindows(path.ToStdWstring());
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
		tempOpened = saveFile.Open(target.path.ToStdWstring(), privateFile);
	}
	//without a temporary file, in a folder that is not writable, the file is
	//not written either: writing it in place truncates it first, and a failed
	//or short write would then lose it
	if (!tempOpened){
		HikariLog(_("Cannot save to file."));
		LogTempNotPrivate(saveFile);
		return false;
	}
	//the destructor removes the temporary file
	if (!WriteText(saveFile, textToWrite, conv) || !saveFile.Commit()){
		LogSaveFailure(saveFile);
		return false;
	}
	return true;
#endif
}

bool CopyToPrivateFile(const wxString &from, const wxString &to)
{
	wxFile source;
	{
		wxLogNull noLog;
		if (!source.Open(from, wxFile::read)){ return false; }
	}
#ifndef _WIN32
	//a link at to, which a save would follow to its target, is refused
	struct stat named;
	if (::lstat(to.fn_str(), &named) == 0 && S_ISLNK(named.st_mode)){ return false; }
	ResolvedSave target;
	if (!ResolveSaveTarget(to, &target)){ return false; }
	SaveFile saveFile;
	saveFile.SetPrivate(true);
	if (!OpenSaveFile(saveFile, target)){ return false; }
#else
	//Open refuses a link or junction at the name
	WindowsSaveFile saveFile;
	{
		wxLogNull noLog;
		if (!saveFile.Open(to.ToStdWstring(), true)){ return false; }
	}
#endif
	std::vector<char> buffer(1 << 16);
	for (;;){
		ssize_t got = source.Read(buffer.data(), buffer.size());
		if (got < 0){ return false; }
		if (got == 0){ break; }
		if (!saveFile.Write(buffer.data(), static_cast<size_t>(got))){ return false; }
	}
	return saveFile.Commit();
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
		!tempFile.Open(target.path.ToStdWstring(), privateFile);
#else
		!OpenSaveFile(tempFile, target);
#endif
	if (writeFailed){
		HikariLog(_("Cannot open file."));
#ifdef _WIN32
		LogTempNotPrivate(tempFile);
#endif
	}
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
