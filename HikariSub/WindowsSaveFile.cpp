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

#ifdef _WIN32

#include "WindowsSaveFile.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <random>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif

namespace {

#ifdef HIKARISUB_TESTING
WindowsSecurityRefusals refusals;
#else
//the application never refuses what a volume does not
constexpr WindowsSecurityRefusals refusals{};
#endif

const DWORD shareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

// Opens the file at path itself, not what a link or junction there leads to,
// for its attributes, and with access more; such a handle without access
// keeps no other program from reading, writing, renaming or deleting the
// file.
HANDLE OpenNoFollow(const std::wstring &path, DWORD access = 0)
{
	return ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | access, shareAll, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

// The id of the file handle is open on, the number of names it has, and
// whether it is a file that is saved to: no folder, and no link or junction,
// which ResolveSaveTarget has followed before.
bool ReadFileFacts(HANDLE handle, WindowsFileId *id, DWORD *links, bool *savable)
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
NameLeadsTo LookAtName(const std::wstring &path, bool existed, const WindowsFileId &id, DWORD *links)
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
// share. The name is opened without following a link there and must lead to
// that file; ReOpenFile then opens the very file that look found, so a link,
// junction or other file put at the name since is never read or written.
HANDLE OpenSameHandle(const std::wstring &path, const WindowsFileId &id, DWORD access, DWORD share)
{
	if (!id.Known()) { return INVALID_HANDLE_VALUE; }
	HANDLE seen = OpenNoFollow(path);
	if (seen == INVALID_HANDLE_VALUE) { return INVALID_HANDLE_VALUE; }
	WindowsFileId now;
	HANDLE handle = INVALID_HANDLE_VALUE;
	if (ReadFileFacts(seen, &now, nullptr, nullptr) && now == id) {
		handle = ::ReOpenFile(seen, access, share, 0);
	}
	::CloseHandle(seen);
	if (handle == INVALID_HANDLE_VALUE) { return INVALID_HANDLE_VALUE; }
	if (!ReadFileFacts(handle, &now, nullptr, nullptr) || !(now == id)) {
		::CloseHandle(handle);
		return INVALID_HANDLE_VALUE;
	}
	return handle;
}

// Marks the file handle is open on to be deleted when its last handle is
// closed. The handle needs DELETE access; no name is looked up.
bool DeleteByHandle(HANDLE handle)
{
	FILE_DISPOSITION_INFO info = { TRUE };
	return ::SetFileInformationByHandle(handle, FileDispositionInfo, &info, sizeof(info)) != 0;
}

// Deletes the file at path only when it is the file with id: the name is
// opened without following a link there, and the file that handle is on is
// deleted through it, so a file another program put at the name is kept.
bool DeleteSameFile(const std::wstring &path, const WindowsFileId &id)
{
	if (!id.Known()) { return false; }
	HANDLE handle = OpenNoFollow(path, DELETE);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	WindowsFileId now;
	bool deleted = ReadFileFacts(handle, &now, nullptr, nullptr) && now == id && DeleteByHandle(handle);
	::CloseHandle(handle);
	return deleted;
}

// Moves the file handle is open on, with DELETE, to the absolute path to,
// through that handle: a file another program put at its old name meanwhile
// is not the one moved. A path that is taken fails, also one taken after a
// look at it.
bool RenameOpenFile(HANDLE handle, const std::wstring &to)
{
	size_t pathBytes = to.size() * sizeof(wchar_t);
	//zeros, which end the path too
	std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + pathBytes);
	FILE_RENAME_INFO *info = reinterpret_cast<FILE_RENAME_INFO *>(buffer.data());
	info->ReplaceIfExists = FALSE;
	info->RootDirectory = nullptr;
	info->FileNameLength = static_cast<DWORD>(pathBytes);
	memcpy(info->FileName, to.data(), pathBytes);
	return ::SetFileInformationByHandle(handle, FileRenameInfo, info, static_cast<DWORD>(buffer.size())) != 0;
}

// writes all of data at the file's offset
bool WriteAll(HANDLE handle, const void *data, size_t size)
{
	const char *bytes = static_cast<const char *>(data);
	while (size > 0) {
		DWORD chunk = size > (1u << 30) ? (1u << 30) : static_cast<DWORD>(size);
		DWORD written = 0;
		if (!::WriteFile(handle, bytes, chunk, &written, nullptr) || written == 0) { return false; }
		bytes += written;
		size -= written;
	}
	return true;
}

bool SeekToStart(HANDLE handle)
{
	LARGE_INTEGER start;
	start.QuadPart = 0;
	return ::SetFilePointerEx(handle, start, nullptr, FILE_BEGIN) != 0;
}

// the length of the file handle is open on, or -1
long long LengthOf(HANDLE handle)
{
	LARGE_INTEGER size;
	return ::GetFileSizeEx(handle, &size) ? size.QuadPart : -1;
}

// cuts the file handle is open on, with write access, to length, or makes it
// that long with zeros
bool SetLength(HANDLE handle, long long length)
{
	FILE_END_OF_FILE_INFO info;
	info.EndOfFile.QuadPart = length;
	return ::SetFileInformationByHandle(handle, FileEndOfFileInfo, &info, sizeof(info)) != 0;
}

// copies all of from, from its start, to the current offset of to
bool CopyContents(HANDLE from, HANDLE to)
{
	if (!SeekToStart(from)) { return false; }
	std::vector<char> buffer(1 << 16);
	for (;;) {
		DWORD got = 0;
		if (!::ReadFile(from, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) { return false; }
		if (got == 0) { return true; }
		if (!WriteAll(to, buffer.data(), got)) { return false; }
	}
}

// Opens the folder at path for listing, as a save keeps it; with share, the
// modes other programs may open it in meanwhile.
HANDLE OpenFolder(const std::wstring &path, DWORD access, DWORD share)
{
	return ::CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
}

// The user this process runs as, and the owner it gives what it makes, as
// TokenUser and TokenOwner give them; read once.
class ProcessUser
{
public:
	static const ProcessUser &Get()
	{
		static const ProcessUser user;
		return user;
	}
	// nullptr when it could not be read
	PSID User() const { return user.empty() ? nullptr : reinterpret_cast<const TOKEN_USER *>(user.data())->User.Sid; }
	PSID Owner() const { return owner.empty() ? nullptr : reinterpret_cast<const TOKEN_OWNER *>(owner.data())->Owner; }
	// whether sid is the user or the owner it gives what it makes
	bool Is(PSID sid) const
	{
		return sid && ((User() && ::EqualSid(sid, User())) || (Owner() && ::EqualSid(sid, Owner())));
	}

private:
	ProcessUser()
	{
		HANDLE token = nullptr;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) { return; }
		user = Read(token, TokenUser);
		owner = Read(token, TokenOwner);
		::CloseHandle(token);
	}
	static std::vector<BYTE> Read(HANDLE token, TOKEN_INFORMATION_CLASS kind)
	{
		DWORD size = 0;
		::GetTokenInformation(token, kind, nullptr, 0, &size);
		std::vector<BYTE> info(size);
		if (size == 0 || !::GetTokenInformation(token, kind, info.data(), size, &size)) { info.clear(); }
		return info;
	}
	std::vector<BYTE> user;
	std::vector<BYTE> owner;
};

// A security descriptor with a protected DACL, so that it takes no ACE from
// a folder, that gives full access to the user this process runs as, to
// SYSTEM and to Administrators, as Windows gives a user's own profile
// folder, and to nobody else. With inheritable, files and folders made in a
// folder that has it inherit the same ACEs. Get is nullptr when it cannot be
// made.
class PrivateSecurity
{
public:
	explicit PrivateSecurity(bool inheritable)
	{
		LPWSTR sid = nullptr;
		PSID user = ProcessUser::Get().User();
		if (!user || !::ConvertSidToStringSidW(user, &sid)) { return; }
		std::wstring flags = inheritable ? L"OICI" : L"";
		std::wstring sddl = L"D:P(A;" + flags + L";FA;;;" + std::wstring(sid) + L")(A;" + flags + L";FA;;;SY)(A;" +
			flags + L";FA;;;BA)";
		::LocalFree(sid);
		if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor,
			nullptr)) {
			descriptor = nullptr;
		}
	}
	~PrivateSecurity() { if (descriptor) { ::LocalFree(descriptor); } }
	PrivateSecurity(const PrivateSecurity &) = delete;
	PrivateSecurity &operator=(const PrivateSecurity &) = delete;
	PSECURITY_DESCRIPTOR Get() const { return descriptor; }
	PACL Dacl() const
	{
		BOOL present = FALSE, defaulted = FALSE;
		PACL dacl = nullptr;
		if (!descriptor || !::GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present) {
			return nullptr;
		}
		return dacl;
	}

private:
	PSECURITY_DESCRIPTOR descriptor = nullptr;
};

// whether sid is one a private DACL lets in: the user this process runs as,
// SYSTEM or Administrators
bool IsPrivateSid(PSID sid)
{
	PSID user = ProcessUser::Get().User();
	return (user && ::EqualSid(sid, user)) || ::IsWellKnownSid(sid, WinLocalSystemSid) ||
		::IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
}

// Whether dacl lets in, or passes on to what is made in a folder, nobody but
// the user this process runs as, SYSTEM and Administrators. A null DACL lets
// everybody in.
bool IsPrivateDacl(PACL dacl)
{
	if (!dacl || !ProcessUser::Get().User()) { return false; }
	for (DWORD i = 0; i < dacl->AceCount; ++i) {
		void *ace = nullptr;
		if (!::GetAce(dacl, i, &ace)) { return false; }
		BYTE type = static_cast<ACE_HEADER *>(ace)->AceType;
		if (type == ACCESS_DENIED_ACE_TYPE) { continue; }
		//an object or callback ACE is not looked into
		if (type != ACCESS_ALLOWED_ACE_TYPE) { return false; }
		if (!IsPrivateSid(&static_cast<ACCESS_ALLOWED_ACE *>(ace)->SidStart)) { return false; }
	}
	return true;
}

// The ways an ACE's mask lets someone at a file's contents: reading them, or
// changing the DACL or the owner so as to be let in to read them.
const DWORD readsContents = 1;
const DWORD changesDacl = 2;
const DWORD takesOwnership = 4;

DWORD ReachOf(ACCESS_MASK mask)
{
	DWORD reach = 0;
	if (mask & (FILE_READ_DATA | FILE_EXECUTE | GENERIC_READ | GENERIC_EXECUTE | GENERIC_ALL | MAXIMUM_ALLOWED)) {
		reach |= readsContents;
	}
	if (mask & (WRITE_DAC | GENERIC_ALL | MAXIMUM_ALLOWED)) { reach |= changesDacl; }
	if (mask & (WRITE_OWNER | GENERIC_ALL | MAXIMUM_ALLOWED)) { reach |= takesOwnership; }
	return reach;
}

// The reach the ACEs of type in dacl that apply to the file itself give sid,
// and with everyone, the ones that give it to Everyone too.
DWORD ReachIn(PACL dacl, PSID sid, BYTE type, bool everyone)
{
	DWORD reach = 0;
	for (DWORD i = 0; i < dacl->AceCount; ++i) {
		void *ace = nullptr;
		if (!::GetAce(dacl, i, &ace)) { continue; }
		const ACE_HEADER *header = static_cast<ACE_HEADER *>(ace);
		if (header->AceType != type || (header->AceFlags & INHERIT_ONLY_ACE)) { continue; }
		//ACCESS_ALLOWED_ACE and ACCESS_DENIED_ACE are laid out alike
		PSID aceSid = &static_cast<ACCESS_ALLOWED_ACE *>(ace)->SidStart;
		if (::EqualSid(aceSid, sid) || (everyone && ::IsWellKnownSid(aceSid, WinWorldSid))) {
			reach |= ReachOf(static_cast<ACCESS_ALLOWED_ACE *>(ace)->Mask);
		}
	}
	return reach;
}

bool IsAllowType(BYTE type)
{
	return type == ACCESS_ALLOWED_ACE_TYPE || type == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
		type == ACCESS_ALLOWED_CALLBACK_ACE_TYPE || type == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE ||
		type == ACCESS_ALLOWED_COMPOUND_ACE_TYPE;
}

bool IsDenyType(BYTE type)
{
	return type == ACCESS_DENIED_ACE_TYPE || type == ACCESS_DENIED_OBJECT_ACE_TYPE ||
		type == ACCESS_DENIED_CALLBACK_ACE_TYPE || type == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE;
}

// Whether dacl lets in no one reference keeps out, see
// DescriptorLetsInNoMoreThan; a null DACL lets everybody in. Only plain
// allow ACEs of reference are counted, which keeps it as narrow as it can
// be; a deny ACE of reference must be in dacl too; an allow ACE of dacl other
// than a plain one, which is not looked into, lets in too much.
bool DaclLetsInNoMoreThan(PACL dacl, PACL reference)
{
	if (!reference) { return true; }
	if (!dacl) { return false; }
	for (DWORD i = 0; i < reference->AceCount; ++i) {
		void *ace = nullptr;
		if (!::GetAce(reference, i, &ace)) { return false; }
		const ACE_HEADER *header = static_cast<ACE_HEADER *>(ace);
		if ((header->AceFlags & INHERIT_ONLY_ACE) || IsAllowType(header->AceType)) { continue; }
		if (header->AceType != ACCESS_DENIED_ACE_TYPE) { return false; }
		const ACCESS_DENIED_ACE *deny = static_cast<ACCESS_DENIED_ACE *>(ace);
		PSID sid = const_cast<DWORD *>(&deny->SidStart);
		if (ReachOf(deny->Mask) & ~ReachIn(dacl, sid, ACCESS_DENIED_ACE_TYPE, false)) { return false; }
	}
	for (DWORD i = 0; i < dacl->AceCount; ++i) {
		void *ace = nullptr;
		if (!::GetAce(dacl, i, &ace)) { return false; }
		const ACE_HEADER *header = static_cast<ACE_HEADER *>(ace);
		if ((header->AceFlags & INHERIT_ONLY_ACE) || IsDenyType(header->AceType)) { continue; }
		if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { return false; }
		const ACCESS_ALLOWED_ACE *allow = static_cast<ACCESS_ALLOWED_ACE *>(ace);
		PSID sid = const_cast<DWORD *>(&allow->SidStart);
		if (IsPrivateSid(sid) || ::IsWellKnownSid(sid, WinCreatorOwnerRightsSid)) { continue; }
		if (ReachOf(allow->Mask) & ~ReachIn(reference, sid, ACCESS_ALLOWED_ACE_TYPE, true)) { return false; }
	}
	return true;
}

// The DACL of descriptor into dacl, nullptr for one that has none or a null
// one, which lets everybody in; false when it cannot be read.
bool DaclOf(PSECURITY_DESCRIPTOR descriptor, PACL *dacl)
{
	BOOL present = FALSE, defaulted = FALSE;
	*dacl = nullptr;
	if (!descriptor || !::GetSecurityDescriptorDacl(descriptor, &present, dacl, &defaulted)) { return false; }
	if (!present) { *dacl = nullptr; }
	return true;
}

// Whether the volume of the file handle is open on keeps ACLs, as NTFS and
// ReFS do and FAT does not; a volume that does not say is taken to.
bool KeepsAcls(HANDLE handle)
{
	DWORD flags = 0;
	if (!::GetVolumeInformationByHandleW(handle, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0)) { return true; }
	return (flags & FILE_PERSISTENT_ACLS) != 0;
}

// Whom the file or folder a handle is open on lets in, see ReadPrivacy.
struct Privacy
{
	// its DACL lets nobody else in, see IsPrivateDacl
	bool narrow = false;
	// its DACL is protected, so it takes no ACE from its folder
	bool protectedDacl = false;
	// Its owner, who may read and change its DACL whatever the DACL says, is
	// the user, the owner this process gives what it makes, SYSTEM or
	// Administrators, all of whom a private DACL lets in.
	bool trustedOwner = false;
};

// Privacy of the file or folder handle is open on, with READ_CONTROL; all
// false when it cannot be read.
Privacy ReadPrivacy(HANDLE handle)
{
	Privacy privacy;
	PSID owner = nullptr;
	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR current = nullptr;
	if (::GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
		nullptr, &dacl, nullptr, &current) != ERROR_SUCCESS) {
		return privacy;
	}
	SECURITY_DESCRIPTOR_CONTROL control = 0;
	DWORD revision = 0;
	privacy.narrow = IsPrivateDacl(dacl);
	privacy.protectedDacl = ::GetSecurityDescriptorControl(current, &control, &revision) &&
		(control & SE_DACL_PROTECTED);
	privacy.trustedOwner = owner && (ProcessUser::Get().Is(owner) || IsPrivateSid(owner));
	::LocalFree(current);
	return privacy;
}

// Gives the file or folder handle is open on, with WRITE_DAC, the DACL of
// PrivateSecurity; a folder gets the inheritable one, and SetSecurityInfo
// passes it on to the files and folders in it that inherit their ACEs.
bool GivePrivateDacl(HANDLE handle, bool folder)
{
	if (refusals.securityChange) {
		::SetLastError(ERROR_ACCESS_DENIED);
		return false;
	}
	PrivateSecurity security(folder);
	PACL dacl = security.Dacl();
	return dacl && ::SetSecurityInfo(handle, SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl,
		nullptr) == ERROR_SUCCESS;
}

// Gives the folder handle is open on, with READ_CONTROL and WRITE_DAC, the
// inheritable DACL of PrivateSecurity unless the DACL it has lets nobody else
// in already and is protected. An unprotected DACL takes the ACEs of the
// folder above again when that folder's ACL changes, and passes them on to
// the files in it. Returns whether it lets nobody else in now, read again
// after the change; a volume that does not keep the protection is taken as
// it is then.
// A folder whose owner is not trusted, see Privacy, is left as it is and is
// not private, whatever its DACL says: its owner could give itself the right
// to list, delete or replace the files in it at any time. On POSIX,
// MakePrivateFolderAt uses only a folder the user owns.
// One on a volume that keeps no ACLs, as FAT, lets in every user of the
// computer, and is not private: on POSIX a folder whose mode keeps others out
// is used, and a FAT mount's mode does not.
bool NarrowFolder(HANDLE handle)
{
	if (!KeepsAcls(handle)) { return false; }
	Privacy now = ReadPrivacy(handle);
	if (!now.trustedOwner) { return false; }
	if (now.narrow && now.protectedDacl) { return true; }
	return GivePrivateDacl(handle, true) && ReadPrivacy(handle).narrow;
}

// Gives the file readable is open on, with READ_CONTROL, the DACL of
// PrivateSecurity unless it lets nobody else in already: through readable
// when changesDacl says it has WRITE_DAC too, or else through a handle of its
// own with WRITE_DAC only, as a file whose owner is another user may let this
// user write it but not change its DACL. READ_CONTROL and WRITE_DAC are no
// access a share mode keeps out, so this works on a file open shared with
// nobody. Returns whether it lets nobody else in now, read again after the
// change. With kept, the file was there before the save, and one whose owner
// is not trusted, see Privacy, is not private whatever its DACL says; a file
// this process has just made is its own, whatever owner a network share
// reports for it.
// A file in a private folder takes its ACEs from that folder, whose DACL is
// protected, so its own need not be. One on a volume that keeps no ACLs is
// taken to be private, as on POSIX, where the 0600 a save makes a file with
// is not checked: on such a volume only MakePrivateFolder refuses, and each
// private file that holds more than a session's paths is in a folder it made.
bool NarrowFile(HANDLE readable, bool kept, bool changesDacl = false)
{
	if (!KeepsAcls(readable)) { return true; }
	Privacy now = ReadPrivacy(readable);
	if (kept && !now.trustedOwner) { return false; }
	if (now.narrow) { return true; }
	bool narrowed = false;
	if (changesDacl) {
		narrowed = GivePrivateDacl(readable, false);
	}
	else {
		HANDLE writable = ::ReOpenFile(readable, WRITE_DAC, shareAll, 0);
		if (writable == INVALID_HANDLE_VALUE) { return false; }
		narrowed = GivePrivateDacl(writable, false);
		::CloseHandle(writable);
	}
	return narrowed && ReadPrivacy(readable).narrow;
}

// Whether the file handle is open on, with READ_CONTROL, lets in no one the
// DACL of the security descriptor reference keeps out; an empty reference
// keeps nobody out.
bool LetsInNoMoreThan(HANDLE handle, const std::vector<unsigned char> &reference)
{
	if (reference.empty()) { return true; }
	PACL referenceDacl = nullptr;
	if (!DaclOf(const_cast<unsigned char *>(reference.data()), &referenceDacl)) { return false; }
	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR current = nullptr;
	bool narrow = ::GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
		nullptr, &current) == ERROR_SUCCESS && DaclLetsInNoMoreThan(dacl, referenceDacl);
	if (current) { ::LocalFree(current); }
	return narrow;
}

// The security descriptor of the file handle is open on, with READ_CONTROL,
// self relative, into security: empty for one that keeps nobody out, on a
// volume that keeps no ACLs or with a null DACL. False when it cannot be read.
bool ReadSecurity(HANDLE handle, std::vector<unsigned char> *security)
{
	security->clear();
	if (!KeepsAcls(handle)) { return true; }
	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR current = nullptr;
	if (::GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
		&current) != ERROR_SUCCESS) {
		return false;
	}
	bool read = true;
	if (dacl) {
		SECURITY_DESCRIPTOR_CONTROL control = 0;
		DWORD revision = 0;
		if (!::GetSecurityDescriptorControl(current, &control, &revision)) { read = false; }
		else if (control & SE_SELF_RELATIVE) {
			const unsigned char *bytes = static_cast<const unsigned char *>(current);
			security->assign(bytes, bytes + ::GetSecurityDescriptorLength(current));
		}
		else {
			DWORD length = 0;
			::MakeSelfRelativeSD(current, nullptr, &length);
			security->resize(length);
			read = length > 0 && ::MakeSelfRelativeSD(current, security->data(), &length);
		}
	}
	::LocalFree(current);
	if (!read) { security->clear(); }
	return read;
}

} // namespace

// who may open a file a save makes
enum class FileAccess {
	// whom its folder passes its ACEs on to
	Folder,
	// only the user, SYSTEM and Administrators where the file can be made
	// so, or else whom its folder passes its ACEs on to when the file that
	// is saved lets them in too
	NoWiderThanTarget,
	// only them, or the file is not made
	Private,
};

namespace {

// Makes a new file at name and opens it for reading, writing and deleting,
// shared with share; CREATE_NEW fails on a name that is taken, by a link
// too. Unless access is Folder, it is made with PrivateSecurity. A volume
// that refuses a security descriptor, as some network shares do, makes it
// with its folder's ACEs, and it is then given the DACL before anything is
// written into it; one that takes it but makes the file with other ACEs is
// caught by the same check. A file that lets others in then is kept only for
// NoWiderThanTarget, when they are let in to the file that is saved too, see
// LetsInNoMoreThan; otherwise it is deleted again and not made, and
// notPrivate is set.
// The file is opened with WRITE_DAC too, which a volume gives the user who
// makes a file, so its DACL is read, given and read again through this
// handle; GENERIC_READ takes READ_CONTROL in. A volume that refuses that
// makes it without, and the DACL is given through a handle ReOpenFile opens.
// INVALID_HANDLE_VALUE on a failure, with GetLastError set.
HANDLE MakeFile(const std::wstring &name, DWORD share, FileAccess access, const std::vector<unsigned char> &target,
	bool *notPrivate = nullptr)
{
	if (notPrivate) { *notPrivate = false; }
	const DWORD rights = GENERIC_READ | GENERIC_WRITE | DELETE;
	DWORD asked = access == FileAccess::Folder ? rights : rights | WRITE_DAC;
	HANDLE handle = INVALID_HANDLE_VALUE;
	if (access != FileAccess::Folder && !refusals.securityAtCreate) {
		PrivateSecurity security(false);
		SECURITY_ATTRIBUTES attributes = { static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), security.Get(), FALSE };
		if (security.Get()) {
			handle = ::CreateFileW(name.c_str(), asked, share, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
				nullptr);
			if (handle == INVALID_HANDLE_VALUE) {
				DWORD error = ::GetLastError();
				if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) { return INVALID_HANDLE_VALUE; }
			}
		}
	}
	if (handle == INVALID_HANDLE_VALUE) {
		handle = ::CreateFileW(name.c_str(), asked, share, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE && asked != rights && ::GetLastError() == ERROR_ACCESS_DENIED) {
			asked = rights;
			handle = ::CreateFileW(name.c_str(), asked, share, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		}
	}
	if (handle == INVALID_HANDLE_VALUE || access == FileAccess::Folder) { return handle; }
	if (NarrowFile(handle, false, (asked & WRITE_DAC) != 0) ||
		(access == FileAccess::NoWiderThanTarget && LetsInNoMoreThan(handle, target))) {
		return handle;
	}
	DeleteByHandle(handle);
	::CloseHandle(handle);
	if (notPrivate) { *notPrivate = true; }
	::SetLastError(ERROR_ACCESS_DENIED);
	return INVALID_HANDLE_VALUE;
}

// Makes a new file in the folder of target with MakeFile and opens it into
// file, shared with no other program, so that one that could open it before
// it was given its DACL cannot keep it open to read what is written into it
// later; id is set to its id. It is never a file another program put there.
// wxFileName::CreateTempFileName made the file with GetTempFileName and then
// opened the name again without O_EXCL, which followed a link put there in
// between.
std::wstring MakeTempFileNear(const std::wstring &target, HANDLE *file, WindowsFileId *id, FileAccess access,
	const std::vector<unsigned char> &targetSecurity, bool *notPrivate)
{
	//target is absolute, see Open
	size_t separator = target.find_last_of(L"\\/");
	if (separator == std::wstring::npos) { return std::wstring(); }
	std::wstring folder = target.substr(0, separator + 1);
	thread_local std::mt19937 random{ std::random_device{}() };
	static const wchar_t digits[] = L"0123456789abcdef";
	for (int attempt = 0; attempt < 100; ++attempt) {
		unsigned int value = static_cast<unsigned int>(random());
		std::wstring name = folder + L"hks";
		for (int shift = 28; shift >= 0; shift -= 4) { name += digits[(value >> shift) & 0xF]; }
		name += L".tmp";
		HANDLE handle = MakeFile(name, 0, access, targetSecurity, notPrivate);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error = ::GetLastError();
			if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) { continue; }
			return std::wstring();
		}
		if (!ReadFileFacts(handle, id, nullptr, nullptr)) {
			DeleteByHandle(handle);
			::CloseHandle(handle);
			return std::wstring();
		}
		*file = handle;
		return name;
	}
	return std::wstring();
}

} // namespace

#ifdef HIKARISUB_TESTING
void SetWindowsSecurityRefusalsForTesting(const WindowsSecurityRefusals &refused)
{
	refusals = refused;
}

bool DescriptorLetsInNoMoreThan(const void *descriptor, const void *reference)
{
	PACL dacl = nullptr, referenceDacl = nullptr;
	return DaclOf(const_cast<void *>(descriptor), &dacl) && DaclOf(const_cast<void *>(reference), &referenceDacl) &&
		DaclLetsInNoMoreThan(dacl, referenceDacl);
}
#endif

bool WindowsSaveFile::Open(const std::wstring &name, bool privateFile)
{
	Discard();
	keptBackup.clear();
	touched = false;
	tempNotPrivate = false;
	privateSave = privateFile;
	targetSecurityKnown = false;
	targetSecurity.clear();
	//an absolute name puts the temporary file next to the file, see wxTempFile::Open
	DWORD length = ::GetFullPathNameW(name.c_str(), 0, nullptr, nullptr);
	if (length == 0) { return false; }
	std::vector<wchar_t> full(length);
	length = ::GetFullPathNameW(name.c_str(), length, full.data(), nullptr);
	if (length == 0 || length >= full.size()) { return false; }
	target.assign(full.data(), length);
	size_t separator = target.find_last_of(L"\\/");
	if (separator == std::wstring::npos) { return false; }
	//The folder stays open until the save ends. Opened for listing and not
	//shared for deleting, it cannot be renamed, removed or replaced with a
	//junction meanwhile. A folder that cannot be listed is saved to without
	//this.
	folderPath = target.substr(0, separator);
	if (folderPath.empty() || folderPath.back() == L':') { folderPath += L"\\"; }
	HANDLE folderHandle = OpenFolder(folderPath, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE);
	if (folderHandle != INVALID_HANDLE_VALUE) {
		folder = folderHandle;
		folderOpened = true;
		if (!ReadFileFacts(folderHandle, &folderId, nullptr, nullptr)) { folderId = WindowsFileId(); }
	}
	//the file at the name now; Commit saves only while the name leads to it.
	//Whom it lets in is read too, when this user may read that.
	targetExisted = false;
	HANDLE handle = OpenNoFollow(target, READ_CONTROL);
	bool readsSecurity = handle != INVALID_HANDLE_VALUE;
	if (!readsSecurity && ::GetLastError() == ERROR_ACCESS_DENIED) { handle = OpenNoFollow(target); }
	if (handle != INVALID_HANDLE_VALUE) {
		bool savable = false;
		bool known = ReadFileFacts(handle, &targetId, nullptr, &savable);
		targetSecurityKnown = readsSecurity && ReadSecurity(handle, &targetSecurity);
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
	//The new contents of a file that is there are kept from everybody the
	//file keeps out while they are written: ReplaceFileW gives the new file
	//the old one's DACL when it replaces it, and a rewrite only copies them
	//into the old file. A new file's temporary file is the new file, which
	//keeps the ACL it is made with when it is moved to the name: the ACEs its
	//folder passes on, as a file made there by any other program gets, no
	//more than the saved file has. A private file's is private in either
	//case, and Commit narrows a private file that is there.
	void *made = nullptr;
	temp = MakeTemp(&made, &tempId);
	if (temp.empty()) {
		Discard();
		return false;
	}
	file = made;
	return true;
}

FileAccess WindowsSaveFile::TempAccess() const
{
	if (privateSave) { return FileAccess::Private; }
	if (!targetExisted) { return FileAccess::Folder; }
	//a file whose DACL could not be read may keep anybody out
	return targetSecurityKnown ? FileAccess::NoWiderThanTarget : FileAccess::Private;
}

std::wstring WindowsSaveFile::MakeTemp(void **handle, WindowsFileId *id)
{
	HANDLE made = INVALID_HANDLE_VALUE;
	bool notPrivate = false;
	std::wstring name = MakeTempFileNear(target, &made, id, TempAccess(), targetSecurity, &notPrivate);
	tempNotPrivate = tempNotPrivate || notPrivate;
	*handle = name.empty() ? nullptr : made;
	return name;
}

bool WindowsSaveFile::Write(const void *data, size_t size)
{
	return file && WriteAll(file, data, size);
}

bool WindowsSaveFile::FolderUnchanged() const
{
	if (!folderOpened || !folderId.Known()) { return true; }
	HANDLE handle = OpenFolder(folderPath, FILE_READ_ATTRIBUTES, shareAll);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	WindowsFileId now;
	bool same = ReadFileFacts(handle, &now, nullptr, nullptr) && now == folderId;
	::CloseHandle(handle);
	return same;
}

void WindowsSaveFile::CloseFile()
{
	if (file) {
		::CloseHandle(file);
		file = nullptr;
	}
}

void WindowsSaveFile::Discard()
{
	if (file) {
		//file is the temporary file: deleted through its own handle
		if (!temp.empty() && DeleteByHandle(file)) { temp.clear(); }
		CloseFile();
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
	if (!file) { return false; }
	//the new contents are on disk before the file is touched
	bool saved = ::FlushFileBuffers(file) != 0;
	//a folder on the way to the file replaced meanwhile: nothing is written
	if (saved && !FolderUnchanged()) { saved = false; }
	if (saved) {
		DWORD links = 0;
		NameLeadsTo now = LookAtName(target, targetExisted, targetId, &links);
		if (!targetExisted) {
			//a new file; one another program made in the meantime is not
			//replaced, and the rename fails on a name made after this look too
			saved = now == NameLeadsTo::Nothing && MoveIntoPlace();
			if (saved) { touched = true; }
		}
		else if (now != NameLeadsTo::TheSameFile) {
			//another program removed the file or put another one, a link or a
			//folder at its name: nothing is written
			saved = false;
		}
		else if (privateSave && !NarrowSaved()) {
			//a private file that keeps others able to open it is not written
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

bool WindowsSaveFile::MoveIntoPlace()
{
	//The temporary file is moved through the handle its contents were
	//written through, still shared with nobody, so a file another program
	//put at its name is never the one moved to the target's name.
	if (RenameOpenFile(file, target)) {
		//the new name on disk before the save ends, as MOVEFILE_WRITE_THROUGH
		::FlushFileBuffers(file);
		CloseFile();
		temp.clear();
		return true;
	}
	//A redirector that cannot rename through a handle, or a Windows that
	//takes no Win32 path there: the file is moved by its name, while that
	//still leads to the file Open made. Another error, as a name that is
	//taken, fails the save.
	DWORD error = ::GetLastError();
	if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_SUPPORTED && error != ERROR_INVALID_FUNCTION &&
		error != ERROR_INVALID_NAME && error != ERROR_PATH_NOT_FOUND) {
		return false;
	}
	CloseFile();
	DWORD links = 0;
	if (LookAtName(temp, true, tempId, &links) != NameLeadsTo::TheSameFile ||
		!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
		return false;
	}
	temp.clear();
	return true;
}

bool WindowsSaveFile::NarrowSaved()
{
	//Before the new contents are in it: a replace gives the new file this
	//DACL, and a rewrite writes into the file. A file an older version left,
	//or that took a folder's ACEs, lets others in that a private file must not.
	HANDLE handle = OpenSameHandle(target, targetId, READ_CONTROL, shareAll);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	bool narrowed = NarrowFile(handle, true);
	::CloseHandle(handle);
	return narrowed;
}

bool WindowsSaveFile::Replace()
{
	CloseFile();
	//The old file is moved to a name of its own while it is replaced, so a
	//replace that stops half way can be undone. The name is made as a new
	//file first, which ReplaceFileW replaces, so it is no name another
	//program could guess and put a file or link at. It lets in no more than
	//the old file, which then has that name.
	void *reserved = nullptr;
	WindowsFileId reservedId;
	std::wstring backup = MakeTemp(&reserved, &reservedId);
	if (backup.empty()) { return false; }
	::CloseHandle(reserved);
	//Without REPLACEFILE_IGNORE_MERGE_ERRORS, a replace that cannot give the
	//new file the old one's ACLs, streams and attributes fails with both files
	//under their names, and the new contents are then copied into the old
	//file, which keeps all of them.
	if (::ReplaceFileW(target.c_str(), temp.c_str(), backup.c_str(), 0, nullptr, nullptr)) {
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
			!::MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
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
	//the temporary file Open made, not what its name leads to now, again
	//shared with nobody
	if (!file) {
		HANDLE reopened = OpenSameHandle(temp, tempId, GENERIC_READ | DELETE, 0);
		if (reopened == INVALID_HANDLE_VALUE) { return false; }
		file = reopened;
	}
	long long newLength = LengthOf(file);
	//the file Open found, not what its name leads to now; a program that has
	//the file open without sharing writes keeps it as it is
	if (newLength < 0 || !targetExisted) { return false; }
	HANDLE out = OpenSameHandle(target, targetId, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE);
	if (out == INVALID_HANDLE_VALUE) { return false; }
	long long oldLength = LengthOf(out);
	//a backup of the old contents, on disk before the file is touched; it
	//lets in no one the file keeps out, as the temporary file does
	void *made = nullptr;
	WindowsFileId backupId;
	std::wstring backupName = oldLength < 0 ? std::wstring() : MakeTemp(&made, &backupId);
	if (backupName.empty()) {
		::CloseHandle(out);
		return false;
	}
	HANDLE backup = made;
	bool saved = CopyContents(out, backup) && ::FlushFileBuffers(backup);
	if (saved) {
		touched = true;
		saved = SeekToStart(out) && CopyContents(file, out) && SetLength(out, newLength) && ::FlushFileBuffers(out);
		if (!saved) {
			bool restored = SeekToStart(out) && CopyContents(backup, out) && SetLength(out, oldLength) &&
				::FlushFileBuffers(out);
			if (!restored) { keptBackup = backupName; }
		}
	}
	//the backup is deleted through its own handle
	if (keptBackup.empty()) { DeleteByHandle(backup); }
	::CloseHandle(backup);
	::CloseHandle(out);
	return saved;
}

bool MakePrivateFolderOnWindows(const std::wstring &folder)
{
	//Made with the DACL of PrivateSecurity, which what is made in it
	//inherits; on a volume that refuses that, it is given it below. A folder
	//that was there, the user's own, is given it too unless it lets nobody
	//else in already with a protected DACL, and passes it on to the files in
	//it that inherit their ACEs, such as the autosaves an older version made.
	//Without traverse checking, the files in a folder could still be opened
	//by their paths, so the folder's ACL alone would not keep them private.
	//A link or junction at the name, and a folder another user owns, also one
	//whose DACL lets only this user in, are left as they are and not used,
	//nor a folder on a volume that keeps no ACLs, as FAT; see NarrowFolder.
	DWORD attributes = ::GetFileAttributesW(folder.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		PrivateSecurity security(true);
		SECURITY_ATTRIBUTES made = { static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), security.Get(), FALSE };
		if (refusals.securityAtCreate || !security.Get() || !::CreateDirectoryW(folder.c_str(), &made)) {
			::CreateDirectoryW(folder.c_str(), nullptr);
		}
	}
	HANDLE handle = ::CreateFileW(folder.c_str(), READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES, shareAll, nullptr,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	BY_HANDLE_FILE_INFORMATION info;
	bool narrowed = ::GetFileInformationByHandle(handle, &info) &&
		(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
		NarrowFolder(handle);
	::CloseHandle(handle);
	return narrowed;
}

FILE *OpenPrivateFileOnWindows(const std::wstring &path)
{
	//A file left at the name, or a link, which DeleteFileW removes and does
	//not follow, is removed; the file is then made anew with MakeFile, which
	//fails on a name taken again meanwhile. A file that is still open
	//without sharing deletes, or a folder at the name, is not opened and is
	//left there; a caller then makes no file, see OpenPrivateFile. A file
	//that is made but cannot be opened as a stream is deleted again.
	::DeleteFileW(path.c_str());
	HANDLE handle = MakeFile(path, FILE_SHARE_READ | FILE_SHARE_WRITE, FileAccess::Private,
		std::vector<unsigned char>());
	if (handle == INVALID_HANDLE_VALUE) { return nullptr; }
	int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDWR | _O_BINARY);
	if (fd == -1) {
		DeleteByHandle(handle);
		::CloseHandle(handle);
		return nullptr;
	}
	FILE *file = _fdopen(fd, "w+b");
	if (!file) {
		//the fd owns the handle, which is still open until _close
		DeleteByHandle(handle);
		_close(fd);
	}
	return file;
}

#endif
