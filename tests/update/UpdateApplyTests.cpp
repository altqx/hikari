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

// The installing side of a Windows update (UpdateApply.h), with the
// release's SHA256SUMS and the installer's start stubbed: it checks the
// file against the release before it runs anything, from a copy in a
// private folder for an all-users install.

#ifdef _WIN32

#include "../check.h"
#include "../../HikariSub/Sha256.h"
#include "../../HikariSub/UpdateApply.h"
#include "../../HikariSub/UpdateVerify.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

const std::string kVersion = "0.0.3";
const std::string kAsset = "HikariSub-0.0.3-windows-x64-setup.exe";
const std::string kOfficial = "the release's own installer";
const std::string kForged = "an installer someone else made";

std::string Sha(const std::string &data)
{
	Sha256 hash;
	hash.Update(data.data(), data.size());
	return hash.HexDigest();
}

// A fresh folder under the user's temp folder.
std::wstring TempFolder()
{
	wchar_t base[MAX_PATH];
	GetTempPathW(MAX_PATH, base);
	static int serial = 0;
	std::wstring folder = std::wstring(base) + L"hikarisub-apply-test-" + std::to_wstring(GetCurrentProcessId()) +
		L"-" + std::to_wstring(++serial);
	CreateDirectoryW(folder.c_str(), nullptr);
	return folder;
}

void Put(const std::wstring &path, const std::string &data)
{
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	::WriteFile(file, data.data(), DWORD(data.size()), &written, nullptr);
	CloseHandle(file);
}

std::string Get(const std::wstring &path)
{
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return std::string();
	std::string data(1 << 16, '\0');
	DWORD read = 0;
	::ReadFile(file, data.data(), DWORD(data.size()), &read, nullptr);
	CloseHandle(file);
	data.resize(read);
	return data;
}

std::wstring Parent(const std::wstring &path) { return path.substr(0, path.find_last_of(L'\\')); }

// What a stubbed run saw.
struct Ran
{
	int count = 0;
	std::wstring installer;
	std::wstring arguments;
	std::string content;
	// Whether the file could be opened to write, or renamed, as the
	// installer started.
	bool writable = false;
	bool renamable = false;
	// The private folder's DACL, as SDDL.
	std::wstring folderDacl;
	bool folderProtected = false;
};

struct Fixture
{
	std::wstring downloads = TempFolder();
	std::wstring staging = TempFolder();
	std::wstring file = downloads + L"\\HikariSub-0.0.3-windows-x64-setup.exe";
	int fetches = 0;
	std::optional<std::string> sums;
	Ran ran;

	update_apply::Environment Environment(bool allUsers)
	{
		update_apply::Environment environment;
		environment.installed = "0.0.2";
		environment.installDir = L"C:\\Program Files\\HikariSub";
		environment.allUsers = allUsers;
		environment.stagingParent = staging;
		environment.fetchSums = [this](const std::string &version) {
			fetches++;
			return version == kVersion ? sums : std::nullopt;
		};
		environment.run = [this](const std::wstring &installer, const std::wstring &arguments) {
			ran.count++;
			ran.installer = installer;
			ran.arguments = arguments;
			ran.content = Get(installer);
			HANDLE write = CreateFileW(installer.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE |
				FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			ran.writable = write != INVALID_HANDLE_VALUE;
			if (ran.writable)
				CloseHandle(write);
			std::wstring moved = installer + L".moved";
			ran.renamable = MoveFileW(installer.c_str(), moved.c_str()) != 0;
			if (ran.renamable)
				MoveFileW(moved.c_str(), installer.c_str());
			PACL dacl = nullptr;
			PSECURITY_DESCRIPTOR descriptor = nullptr;
			if (GetNamedSecurityInfoW(Parent(installer).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
					nullptr, &dacl, nullptr, &descriptor) == ERROR_SUCCESS)
			{
				LPWSTR text = nullptr;
				if (ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1,
						DACL_SECURITY_INFORMATION, &text, nullptr))
				{
					ran.folderDacl = text;
					LocalFree(text);
				}
				SECURITY_DESCRIPTOR_CONTROL control = 0;
				DWORD revision = 0;
				GetSecurityDescriptorControl(descriptor, &control, &revision);
				ran.folderProtected = (control & SE_DACL_PROTECTED) != 0;
				LocalFree(descriptor);
			}
			return true;
		};
		return environment;
	}

	update_apply::Request Request()
	{
		update_apply::Request request;
		request.version = kVersion;
		request.file = file;
		request.pid = 4242;
		request.relaunch = true;
		return request;
	}

	// The private folders Apply left under the staging parent.
	int StagedFolders()
	{
		WIN32_FIND_DATAW found;
		HANDLE find = FindFirstFileW((staging + L"\\HikariSub-update-*").c_str(), &found);
		if (find == INVALID_HANDLE_VALUE)
			return 0;
		int count = 0;
		do {
			count++;
		} while (FindNextFileW(find, &found));
		FindClose(find);
		return count;
	}
};

std::string Listed(const std::string &content, const std::string &name = kAsset)
{
	return Sha(content) + "  " + name + "\n" + std::string(64, 'b') + "  hikarisub-0.0.3-linux-amd64.deb\n";
}

} // namespace

TEST(updateApplyParsesOnlyAVersionAFileAPidAndAFlag)
{
	std::optional<update_apply::Request> request =
		update_apply::Parse({ L"0.0.3", L"C:\\Users\\a b\\x.exe", L"4242", L"1" });
	CHECK(request && request->version == "0.0.3" && request->file == L"C:\\Users\\a b\\x.exe" &&
		request->pid == 4242 && request->relaunch);
	// A digest, or anything more, is not taken.
	CHECK(!update_apply::Parse({ L"0.0.3", L"C:\\x.exe", L"4242", L"1", std::wstring(64, L'a') }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"C:\\x.exe", L"4242" }));
	CHECK(!update_apply::Parse({ L"v0.0.3", L"C:\\x.exe", L"4242", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3+b", L"C:\\x.exe", L"4242", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"x.exe", L"4242", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"\\\\server\\share\\x.exe", L"4242", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"C:\\a\\..\\x.exe", L"4242", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"C:\\x.exe:stream", L"4242", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"C:\\x.exe", L"0", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"C:\\x.exe", L"-1", L"0" }));
	CHECK(!update_apply::Parse({ L"0.0.3", L"C:\\x.exe", L"4242", L"yes" }));

	// The command line reads back as it was given.
	update_apply::Request back;
	back.version = "0.1.0-beta.1";
	back.file = L"C:\\Users\\a b\\AppData\\Local\\HikariSub\\Updates\\0.1.0-beta.1\\setup.exe";
	back.pid = 7;
	back.relaunch = false;
	std::wstring line = L"hikarisub.exe " + update_apply::CommandLine(back);
	int count = 0;
	LPWSTR *argv = CommandLineToArgvW(line.c_str(), &count);
	CHECK(argv && count == 6 && std::wstring(argv[1]) == update_apply::kSwitch);
	if (argv && count == 6) {
		std::optional<update_apply::Request> parsed =
			update_apply::Parse({ argv[2], argv[3], argv[4], argv[5] });
		CHECK(parsed && parsed->version == back.version && parsed->file == back.file && parsed->pid == 7 &&
			!parsed->relaunch);
	}
	LocalFree(argv);
}

TEST(updateApplyAllUsersRunsAVerifiedCopyFromAPrivateFolder)
{
	Fixture fixture;
	Put(fixture.file, kOfficial);
	fixture.sums = Listed(kOfficial);
	CHECK_EQ(update_apply::Apply(fixture.Request(), fixture.Environment(true)), update_verify::kOk);
	CHECK_EQ(fixture.ran.count, 1);
	// Not the download: a copy, in a new folder under the staging parent.
	CHECK(fixture.ran.installer != fixture.file);
	CHECK(Parent(Parent(fixture.ran.installer)) == fixture.staging);
	CHECK(fixture.ran.content == kOfficial);
	// No one could change or move the copy as it started.
	CHECK(!fixture.ran.writable);
	CHECK(!fixture.ran.renamable);
	// The folder inherits nothing and lets only SYSTEM, administrators and
	// its owner in.
	CHECK(fixture.ran.folderProtected);
	CHECK(fixture.ran.folderDacl.find(L";;;BU)") == std::wstring::npos);
	CHECK(fixture.ran.folderDacl.find(L";;;WD)") == std::wstring::npos);
	CHECK(fixture.ran.folderDacl.find(L";;;AU)") == std::wstring::npos);
	CHECK(fixture.ran.folderDacl.find(L";;;IU)") == std::wstring::npos);
	CHECK(fixture.ran.folderDacl.find(L";;;SY)") != std::wstring::npos);
	CHECK(fixture.ran.folderDacl.find(L";;;BA)") != std::wstring::npos);
	CHECK(fixture.ran.arguments ==
		L"/S /UPDATE /WAITPID=4242 /RELAUNCH /ALLUSERS /D=C:\\Program Files\\HikariSub");
}

TEST(updateApplyRefusesAForgedInstallerBeforeRunningIt)
{
	// The caller's pending record would carry the forged file's own digest;
	// Apply is not given it, and the release lists another.
	Fixture fixture;
	Put(fixture.file, kForged);
	fixture.sums = Listed(kOfficial);
	CHECK_EQ(update_apply::Apply(fixture.Request(), fixture.Environment(true)), update_verify::kMismatch);
	CHECK_EQ(fixture.ran.count, 0);
	CHECK_EQ(fixture.StagedFolders(), 0);
	CHECK_EQ(update_apply::Apply(fixture.Request(), fixture.Environment(false)), update_verify::kMismatch);
	CHECK_EQ(fixture.ran.count, 0);
}

TEST(updateApplyRefusesADigestListedUnderAnotherName)
{
	Fixture fixture;
	Put(fixture.file, kForged);
	fixture.sums = Listed(kOfficial) + Sha(kForged) + "  HikariSub-0.0.3-windows-x64.zip\n";
	CHECK_EQ(update_apply::Apply(fixture.Request(), fixture.Environment(true)), update_verify::kMismatch);
	fixture.sums = Sha(kForged) + "  HikariSub-0.0.4-windows-x64-setup.exe\n";
	CHECK_EQ(update_apply::Apply(fixture.Request(), fixture.Environment(true)), update_verify::kMismatch);
	CHECK_EQ(fixture.ran.count, 0);
	CHECK_EQ(fixture.StagedFolders(), 0);
}

TEST(updateApplyRunsNothingWithoutTheReleaseSums)
{
	Fixture fixture;
	Put(fixture.file, kOfficial);
	fixture.sums = std::nullopt;
	CHECK_EQ(update_apply::Apply(fixture.Request(), fixture.Environment(true)), update_verify::kNoSums);
	CHECK_EQ(fixture.ran.count, 0);
	CHECK_EQ(fixture.StagedFolders(), 0);
}

TEST(updateApplyRefusesAVersionThatIsNotNewer)
{
	Fixture fixture;
	Put(fixture.file, kOfficial);
	fixture.sums = Listed(kOfficial);
	update_apply::Environment environment = fixture.Environment(true);
	environment.installed = "0.0.3";
	CHECK_EQ(update_apply::Apply(fixture.Request(), environment), update_verify::kBadArguments);
	environment.installed = "0.0.4";
	CHECK_EQ(update_apply::Apply(fixture.Request(), environment), update_verify::kBadArguments);
	update_apply::Request request = fixture.Request();
	request.version = "0.0.3+evil";
	CHECK_EQ(update_apply::Apply(request, fixture.Environment(true)), update_verify::kBadArguments);
	CHECK_EQ(fixture.fetches, 0);
	CHECK_EQ(fixture.ran.count, 0);
}

TEST(updateApplyUserScopeVerifiesTheDownloadWhereItIs)
{
	Fixture fixture;
	Put(fixture.file, kOfficial);
	fixture.sums = Listed(kOfficial);
	CHECK_EQ(update_apply::Apply(fixture.Request(), fixture.Environment(false)), update_verify::kOk);
	CHECK_EQ(fixture.ran.count, 1);
	CHECK(fixture.ran.installer == fixture.file);
	CHECK(fixture.ran.content == kOfficial);
	// Held against writes and renames until the installer has started.
	CHECK(!fixture.ran.writable);
	CHECK(!fixture.ran.renamable);
	CHECK(fixture.ran.arguments ==
		L"/S /UPDATE /WAITPID=4242 /RELAUNCH /CURRENTUSER /D=C:\\Program Files\\HikariSub");
	CHECK_EQ(fixture.StagedFolders(), 0);
}

TEST(updateApplyReportsAnInstallerThatDoesNotStart)
{
	Fixture fixture;
	Put(fixture.file, kOfficial);
	fixture.sums = Listed(kOfficial);
	update_apply::Environment environment = fixture.Environment(true);
	environment.run = [](const std::wstring &, const std::wstring &) { return false; };
	CHECK_EQ(update_apply::Apply(fixture.Request(), environment), update_verify::kFailed);
	CHECK_EQ(fixture.StagedFolders(), 0);
}

#endif

namespace {

// A made-up disk: each path's rights for the user, and each folder's entries.
struct FakeDisk
{
	std::map<std::wstring, unsigned long> rights;
	std::map<std::wstring, std::vector<update_apply::Entry>> folders;

	update_apply::RightsOf RightsOf()
	{
		return [this](const std::wstring &path) -> std::optional<unsigned long> {
			auto found = rights.find(path);
			if (found == rights.end())
				return std::nullopt;
			return found->second;
		};
	}
	update_apply::ListOf ListOf()
	{
		return [this](const std::wstring &folder) -> std::optional<std::vector<update_apply::Entry>> {
			auto found = folders.find(folder);
			if (found == folders.end())
				return std::nullopt;
			return found->second;
		};
	}
	bool Protected(const std::wstring &dir) { return update_apply::IsProtected(dir, RightsOf(), ListOf()); }
};

// Program Files as Windows sets it up: users read and run.
FakeDisk ProgramFiles()
{
	const unsigned long read = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
	FakeDisk disk;
	// C:\ lets users make folders in it, as Windows has it.
	disk.rights[L"C:\\"] = read | FILE_ADD_SUBDIRECTORY;
	disk.rights[L"C:\\Program Files"] = read;
	disk.rights[L"C:\\Program Files\\HikariSub"] = read;
	disk.rights[L"C:\\Program Files\\HikariSub\\hikarisub.exe"] = read;
	disk.rights[L"C:\\Program Files\\HikariSub\\ffms2.dll"] = read;
	disk.rights[L"C:\\Program Files\\HikariSub\\Automation"] = read;
	disk.rights[L"C:\\Program Files\\HikariSub\\Automation\\a.lua"] = read;
	disk.folders[L"C:\\Program Files\\HikariSub"] = {
		{ L"hikarisub.exe", false, false }, { L"ffms2.dll", false, false }, { L"Automation", true, false } };
	disk.folders[L"C:\\Program Files\\HikariSub\\Automation"] = { { L"a.lua", false, false } };
	return disk;
}

} // namespace

TEST(updateApplyElevatesOnlyAProgramTheUserCannotChange)
{
	FakeDisk disk = ProgramFiles();
	CHECK(disk.Protected(L"C:\\Program Files\\HikariSub"));
	CHECK(disk.Protected(L"C:\\Program Files\\HikariSub\\"));

	// A DLL the user may write, or put beside the program, or a file deep
	// in the folder: an elevation could run the user's code.
	unsigned long changes[] = { FILE_WRITE_DATA, FILE_APPEND_DATA, DELETE, WRITE_DAC, WRITE_OWNER };
	for (unsigned long change : changes) {
		FakeDisk dll = ProgramFiles();
		dll.rights[L"C:\\Program Files\\HikariSub\\ffms2.dll"] |= change;
		CHECK(!dll.Protected(L"C:\\Program Files\\HikariSub"));
		FakeDisk folder = ProgramFiles();
		folder.rights[L"C:\\Program Files\\HikariSub"] |= change;
		CHECK(!folder.Protected(L"C:\\Program Files\\HikariSub"));
		FakeDisk deep = ProgramFiles();
		deep.rights[L"C:\\Program Files\\HikariSub\\Automation\\a.lua"] |= change;
		CHECK(!deep.Protected(L"C:\\Program Files\\HikariSub"));
	}
	FakeDisk addFile = ProgramFiles();
	addFile.rights[L"C:\\Program Files\\HikariSub\\Automation"] |= FILE_ADD_FILE;
	CHECK(!addFile.Protected(L"C:\\Program Files\\HikariSub"));

	// A folder on the way that the user can empty or re-permission: the
	// install folder could be renamed away and replaced.
	unsigned long replaces[] = { FILE_DELETE_CHILD, DELETE, WRITE_DAC, WRITE_OWNER };
	for (unsigned long replace : replaces) {
		FakeDisk way = ProgramFiles();
		way.rights[L"C:\\Program Files"] |= replace;
		CHECK(!way.Protected(L"C:\\Program Files\\HikariSub"));
		FakeDisk root = ProgramFiles();
		root.rights[L"C:\\"] |= replace;
		CHECK(!root.Protected(L"C:\\Program Files\\HikariSub"));
	}

	// What cannot be told counts as changeable: a path without rights,
	// a folder that cannot be listed, a link inside.
	FakeDisk unknown = ProgramFiles();
	unknown.rights.erase(L"C:\\Program Files\\HikariSub\\ffms2.dll");
	CHECK(!unknown.Protected(L"C:\\Program Files\\HikariSub"));
	FakeDisk unlisted = ProgramFiles();
	unlisted.folders.erase(L"C:\\Program Files\\HikariSub\\Automation");
	CHECK(!unlisted.Protected(L"C:\\Program Files\\HikariSub"));
	FakeDisk link = ProgramFiles();
	link.folders[L"C:\\Program Files\\HikariSub"].push_back({ L"plugins", true, true });
	CHECK(!link.Protected(L"C:\\Program Files\\HikariSub"));

	// Only a plain local path.
	CHECK(!disk.Protected(L"\\\\server\\share\\HikariSub"));
	CHECK(!disk.Protected(L"\\\\?\\C:\\Program Files\\HikariSub"));
	CHECK(!disk.Protected(L"C:\\Program Files\\..\\Program Files\\HikariSub"));
	CHECK(!disk.Protected(L"C:Program Files\\HikariSub"));
	CHECK(!disk.Protected(L"C:\\"));
	CHECK(!disk.Protected(L""));
}

TEST(updateApplyFindsTheUsersOwnFolderChangeable)
{
	// The user's temp folder, as a per-user copy's LocalAppData is.
	std::wstring folder = TempFolder();
	Put(folder + L"\\hikarisub.exe", "a program");
	CHECK(!update_apply::IsProtectedFromUser(folder));
}

#ifdef _WIN32
// The real fetch, against a published release (HIKARISUB_TEST_NETWORK=1):
// GitHub redirects to its asset host, every hop must be allowed, and every
// server's certificate must chain to a root the machine trusts.
TEST(updateApplyFetchFollowsOnlyAllowedHosts)
{
	wchar_t flag[8] = {};
	if (GetEnvironmentVariableW(L"HIKARISUB_TEST_NETWORK", flag, 8) != 1 || flag[0] != L'1') {
		std::printf("skipped: HIKARISUB_TEST_NETWORK=1 fetches from GitHub\n");
		return;
	}
	std::string url = update_verify::SumsUrl("0.0.1");
	auto anyGitHub = [](const std::string &u) {
		for (const char *prefix : { "https://github.com/", "https://objects.githubusercontent.com/",
				"https://release-assets.githubusercontent.com/" })
		{
			if (u.rfind(prefix, 0) == 0)
				return true;
		}
		return false;
	};
	std::optional<std::string> sums = update_apply::FetchHttps(url, anyGitHub, 65536);
	CHECK(sums && sums->find("HikariSub-0.0.1-windows-x64.zip") != std::string::npos);
	// A redirect to a host that is not allowed is not followed.
	auto githubOnly = [](const std::string &u) { return u.rfind("https://github.com/", 0) == 0; };
	CHECK(!update_apply::FetchHttps(url, githubOnly, 65536));
	// Nor is a URL that is not allowed fetched at all.
	CHECK(!update_apply::FetchHttps(url, [](const std::string &) { return false; }, 65536));
}
#endif
