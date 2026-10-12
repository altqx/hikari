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

#include "check.h"
#include "../HikariSub/WindowsSaveFile.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace {

// the SID of the user the tests run as, in SDDL
std::wstring UserSid()
{
	HANDLE token = nullptr;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) { return std::wstring(); }
	DWORD size = 0;
	::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
	std::vector<BYTE> info(size);
	std::wstring text;
	LPWSTR sid = nullptr;
	if (size && ::GetTokenInformation(token, TokenUser, info.data(), size, &size) &&
		::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(info.data())->User.Sid, &sid)) {
		text = sid;
		::LocalFree(sid);
	}
	::CloseHandle(token);
	return text;
}

// a DACL that lets in the user, SYSTEM and Administrators only, as a private
// file has; with inheritable, passed on to what is made in a folder
std::wstring PrivateSddl(bool inheritable)
{
	std::wstring flags = inheritable ? L"OICI" : L"";
	return L"D:P(A;" + flags + L";FA;;;" + UserSid() + L")(A;" + flags + L";FA;;;SY)(A;" + flags + L";FA;;;BA)";
}

// the private DACL and read access for all users, as a folder that is
// shared with the other users of the computer passes on
std::wstring SharedSddl(bool inheritable)
{
	std::wstring flags = inheritable ? L"OICI" : L"";
	return PrivateSddl(inheritable) + L"(A;" + flags + L";FR;;;BU)";
}

// a security descriptor made from SDDL, freed with it
struct Descriptor {
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	explicit Descriptor(const std::wstring &sddl)
	{
		if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor,
			nullptr)) {
			descriptor = nullptr;
		}
	}
	~Descriptor() { if (descriptor) { ::LocalFree(descriptor); } }
	Descriptor(const Descriptor &) = delete;
	Descriptor &operator=(const Descriptor &) = delete;
};

// Refuses what WindowsSecurityRefusals says for the length of a test.
struct Refuse {
	explicit Refuse(bool atCreate, bool change)
	{
		WindowsSecurityRefusals refusals;
		refusals.securityAtCreate = atCreate;
		refusals.securityChange = change;
		SetWindowsSecurityRefusalsForTesting(refusals);
	}
	~Refuse() { SetWindowsSecurityRefusalsForTesting(WindowsSecurityRefusals()); }
};

// A new folder in the temporary folder that passes on read access for all
// users, as a folder shared on a network or a portable install can, removed
// with what is in it when the test ends.
struct SharedFolder {
	std::wstring path;
	SharedFolder()
	{
		wchar_t base[MAX_PATH + 1];
		DWORD length = ::GetTempPathW(MAX_PATH + 1, base);
		if (length == 0 || length > MAX_PATH) { return; }
		static unsigned int count = 0;
		std::wstring name = std::wstring(base, length) + L"hikarisub-winsave-" +
			std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(++count);
		Descriptor security(SharedSddl(true));
		SECURITY_ATTRIBUTES attributes = { static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), security.descriptor, FALSE };
		if (security.descriptor && ::CreateDirectoryW(name.c_str(), &attributes)) { path = name; }
	}
	~SharedFolder()
	{
		if (path.empty()) { return; }
		for (const std::wstring &name : Names(L"*")) { ::DeleteFileW((path + L"\\" + name).c_str()); }
		::RemoveDirectoryW(path.c_str());
	}
	std::wstring operator/(const std::wstring &name) const { return path + L"\\" + name; }
	// the names of the files in the folder that match pattern
	std::vector<std::wstring> Names(const std::wstring &pattern) const
	{
		std::vector<std::wstring> names;
		WIN32_FIND_DATAW found;
		HANDLE find = ::FindFirstFileW((path + L"\\" + pattern).c_str(), &found);
		if (find == INVALID_HANDLE_VALUE) { return names; }
		do {
			std::wstring name = found.cFileName;
			if (name != L"." && name != L"..") { names.push_back(name); }
		} while (::FindNextFileW(find, &found));
		::FindClose(find);
		return names;
	}
	// the temporary files a save left
	size_t Stages() const { return Names(L"hks*.tmp").size(); }
};

// Makes a file at path holding contents, with the DACL of sddl, or with
// what its folder passes on for an empty one.
bool MakeFileWith(const std::wstring &path, const std::string &contents, const std::wstring &sddl)
{
	Descriptor security(sddl.empty() ? L"D:" : sddl);
	SECURITY_ATTRIBUTES attributes = { static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), security.descriptor, FALSE };
	HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, sddl.empty() ? nullptr : &attributes, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) { return false; }
	DWORD written = 0;
	bool made = ::WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) &&
		written == contents.size();
	::CloseHandle(handle);
	return made;
}

std::string Contents(const std::wstring &path)
{
	HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) { return "<missing>"; }
	std::string contents;
	char buffer[4096];
	DWORD got = 0;
	while (::ReadFile(handle, buffer, sizeof(buffer), &got, nullptr) && got > 0) { contents.append(buffer, got); }
	::CloseHandle(handle);
	return contents;
}

// whether the file or folder at path lets in no one a private one keeps out
bool IsPrivateAt(const std::wstring &path)
{
	PSECURITY_DESCRIPTOR current = nullptr;
	if (::GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr,
		nullptr, &current) != ERROR_SUCCESS) {
		return false;
	}
	Descriptor reference(PrivateSddl(false));
	bool narrow = DescriptorLetsInNoMoreThan(current, reference.descriptor);
	::LocalFree(current);
	return narrow;
}

// one save of contents to path, as OpenWrite::FileWrite makes it
bool Save(const std::wstring &path, const std::string &contents, bool privateFile = false)
{
	WindowsSaveFile save;
	return save.Open(path, privateFile) && save.Write(contents.data(), contents.size()) && save.Commit();
}

} // namespace

TEST(windowsPrivateTargetIsNotSavedWhenTheStageCannotBeMadePrivate)
{
	// a share that refuses a security descriptor and a DACL change: the
	// temporary file would keep the folder's ACEs, which let all users read
	// what only the user may read in the file
	SharedFolder folder;
	CHECK(!folder.path.empty());
	std::wstring target = folder / L"private.ass";
	CHECK(MakeFileWith(target, "old", PrivateSddl(false)));
	Refuse refuse(true, true);
	WindowsSaveFile save;
	CHECK(!save.Open(target));
	CHECK(!save.IsOpened());
	CHECK(!save.Write("new", 3));
	CHECK(!save.Commit());
	CHECK(Contents(target) == "old");
	CHECK(IsPrivateAt(target));
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsPrivateTargetIsSavedWhenOnlyTheSecurityAtCreateIsRefused)
{
	// the temporary file is made with the folder's ACEs and given the
	// private DACL before anything is written into it
	SharedFolder folder;
	std::wstring target = folder / L"private.ass";
	CHECK(MakeFileWith(target, "old", PrivateSddl(false)));
	Refuse refuse(true, false);
	CHECK(Save(target, "new"));
	CHECK(Contents(target) == "new");
	CHECK(IsPrivateAt(target));
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsSharedTargetIsSavedWhenTheStageCannotBeMadePrivate)
{
	// a file that lets in whom its folder passes its ACEs on to is saved as
	// before: its temporary file lets in no one the file keeps out
	SharedFolder folder;
	std::wstring target = folder / L"shared.ass";
	CHECK(MakeFileWith(target, "old", std::wstring()));
	Refuse refuse(true, true);
	CHECK(Save(target, "new"));
	CHECK(Contents(target) == "new");
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsNewFileIsSavedWithItsFoldersAces)
{
	SharedFolder folder;
	std::wstring target = folder / L"new.ass";
	Refuse refuse(true, true);
	CHECK(Save(target, "new"));
	CHECK(Contents(target) == "new");
	CHECK(!IsPrivateAt(target));
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsPrivateSaveIsNotMadeWhenTheStageCannotBeMadePrivate)
{
	SharedFolder folder;
	std::wstring target = folder / L"autosave.ass";
	CHECK(MakeFileWith(target, "old", std::wstring()));
	Refuse refuse(true, true);
	CHECK(!Save(target, "new", true));
	CHECK(Contents(target) == "old");
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsPrivateTargetIsNotReplacedWhenTheBackupNameCannotBeMadePrivate)
{
	// the stage is made private; the name the old file is moved to during
	// the replace cannot be, and nothing is written
	SharedFolder folder;
	std::wstring target = folder / L"private.ass";
	CHECK(MakeFileWith(target, "old", PrivateSddl(false)));
	WindowsSaveFile save;
	CHECK(save.Open(target));
	CHECK(save.Write("new", 3));
	Refuse refuse(true, true);
	CHECK(!save.Commit());
	CHECK(!save.TargetTouched());
	CHECK(save.KeptBackup().empty());
	CHECK(Contents(target) == "old");
	CHECK(IsPrivateAt(target));
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsPrivateTargetIsNotRewrittenWhenTheBackupCannotBeMadePrivate)
{
	// a file with a second name is rewritten in place, with a backup of its
	// old contents that would let all users read them
	SharedFolder folder;
	std::wstring target = folder / L"private.ass";
	std::wstring link = folder / L"link.ass";
	CHECK(MakeFileWith(target, "old", PrivateSddl(false)));
	CHECK(::CreateHardLinkW(link.c_str(), target.c_str(), nullptr) != 0);
	WindowsSaveFile save;
	CHECK(save.Open(target));
	CHECK(save.Write("new", 3));
	Refuse refuse(true, true);
	CHECK(!save.Commit());
	CHECK(!save.TargetTouched());
	CHECK(Contents(target) == "old");
	CHECK(Contents(link) == "old");
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsSharedTargetIsRewrittenWhenTheBackupCannotBeMadePrivate)
{
	SharedFolder folder;
	std::wstring target = folder / L"shared.ass";
	std::wstring link = folder / L"link.ass";
	CHECK(MakeFileWith(target, "old", std::wstring()));
	CHECK(::CreateHardLinkW(link.c_str(), target.c_str(), nullptr) != 0);
	Refuse refuse(true, true);
	CHECK(Save(target, "new"));
	CHECK(Contents(target) == "new");
	CHECK(Contents(link) == "new");
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsStageIsSharedWithNobody)
{
	// one that could open the temporary file before it was given its DACL
	// cannot keep it open to read what is written into it later
	SharedFolder folder;
	std::wstring target = folder / L"shared.ass";
	CHECK(MakeFileWith(target, "old", std::wstring()));
	WindowsSaveFile save;
	CHECK(save.Open(target));
	CHECK(save.Write("new", 3));
	std::vector<std::wstring> stages = folder.Names(L"hks*.tmp");
	CHECK_EQ(stages.size(), static_cast<size_t>(1));
	if (stages.size() == 1) {
		HANDLE handle = ::CreateFileW((folder / stages[0]).c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
			nullptr);
		unsigned long error = ::GetLastError();
		CHECK(handle == INVALID_HANDLE_VALUE);
		CHECK_EQ(error, static_cast<unsigned long>(ERROR_SHARING_VIOLATION));
		if (handle != INVALID_HANDLE_VALUE) { ::CloseHandle(handle); }
	}
	save.Discard();
	CHECK(Contents(target) == "old");
	CHECK_EQ(folder.Stages(), static_cast<size_t>(0));
}

TEST(windowsPrivateFolderIsNotUsedWhenItCannotBeMadePrivate)
{
	SharedFolder folder;
	{
		Refuse refuse(true, true);
		CHECK(!MakePrivateFolderOnWindows(folder / L"Refused"));
	}
	CHECK(MakePrivateFolderOnWindows(folder / L"Refused"));
	CHECK(IsPrivateAt(folder / L"Refused"));
	::RemoveDirectoryW((folder / L"Refused").c_str());
	{
		Refuse refuse(true, false);
		CHECK(MakePrivateFolderOnWindows(folder / L"Narrowed"));
	}
	CHECK(IsPrivateAt(folder / L"Narrowed"));
	::RemoveDirectoryW((folder / L"Narrowed").c_str());
}

TEST(windowsPrivateFileIsNotOpenedWhenItCannotBeMadePrivate)
{
	SharedFolder folder;
	{
		Refuse refuse(true, true);
		CHECK(OpenPrivateFileOnWindows(folder / L"cache.part") == nullptr);
	}
	CHECK(folder.Names(L"cache.part").empty());
	FILE *file = OpenPrivateFileOnWindows(folder / L"cache.part");
	CHECK(file != nullptr);
	if (file) { fclose(file); }
	CHECK(IsPrivateAt(folder / L"cache.part"));
}

TEST(windowsDescriptorComparison)
{
	std::wstring user = UserSid();
	Descriptor privateFile(PrivateSddl(false));
	Descriptor shared(SharedSddl(false));
	Descriptor everyone(PrivateSddl(false) + L"(A;;FR;;;WD)");
	Descriptor writers(PrivateSddl(false) + L"(A;;FW;;;BU)");
	Descriptor changesDacl(PrivateSddl(false) + L"(A;;WD;;;BU)");
	Descriptor denies(L"D:(D;;FR;;;BG)" + SharedSddl(false).substr(3));
	Descriptor nullDacl(L"D:NO_ACCESS_CONTROL");
	CHECK(DescriptorLetsInNoMoreThan(privateFile.descriptor, privateFile.descriptor));
	CHECK(DescriptorLetsInNoMoreThan(privateFile.descriptor, shared.descriptor));
	CHECK(!DescriptorLetsInNoMoreThan(shared.descriptor, privateFile.descriptor));
	CHECK(DescriptorLetsInNoMoreThan(shared.descriptor, shared.descriptor));
	// what Everyone may do, every user may
	CHECK(DescriptorLetsInNoMoreThan(shared.descriptor, everyone.descriptor));
	CHECK(!DescriptorLetsInNoMoreThan(everyone.descriptor, shared.descriptor));
	// writing lets no one read; changing the DACL does
	CHECK(DescriptorLetsInNoMoreThan(writers.descriptor, privateFile.descriptor));
	CHECK(!DescriptorLetsInNoMoreThan(changesDacl.descriptor, privateFile.descriptor));
	// a deny ACE the file has must be in the temporary file too
	CHECK(!DescriptorLetsInNoMoreThan(shared.descriptor, denies.descriptor));
	CHECK(DescriptorLetsInNoMoreThan(denies.descriptor, denies.descriptor));
	// a null DACL lets everybody in
	CHECK(DescriptorLetsInNoMoreThan(shared.descriptor, nullDacl.descriptor));
	CHECK(!DescriptorLetsInNoMoreThan(nullDacl.descriptor, privateFile.descriptor));
	CHECK(!user.empty());
}

#endif
