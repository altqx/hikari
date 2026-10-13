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

// The Windows side of an update that installs (installers spec, 4.2):
//
//   hikarisub.exe --hikari-apply-update <version> <file> <pid> <relaunch>
//
// The app does not hand the downloaded installer to UAC. For an all-users
// install it elevates the installed hikarisub.exe in Program Files, which
// only administrators can change, with this switch; for a per-user install
// it runs the same unelevated. It elevates only a program the user cannot
// change (IsProtectedFromUser): the scope comes from package-kind.txt,
// which is no more protected than the folder it is in, so a per-user copy
// that claims to be for all users is never elevated, nor one installed
// for all users into a folder the user can write. That process trusts nothing the user's
// side wrote (the pending record, its SHA-256, the download folder):
//
//   1. <version> must be a release version newer than its own.
//   2. It downloads the release's SHA256SUMS itself, over HTTPS from the
//      fixed GitHub URL, and takes the digest listed for
//      HikariSub-<version>-windows-x64-setup.exe.
//   3. Elevated, it copies <file> into a new folder under %windir%\Temp
//      that only SYSTEM and administrators can write; per-user, it uses
//      <file> where it is. Either way it opens that file denying writes
//      and deletes, hashes it through that handle, and runs it, in
//      /UPDATE mode for its own install folder, only when the hash is the
//      listed one, keeping the handle open until the installer has
//      started.
//
// It exits with update_verify's codes: 0 once the installer runs, 2 for a
// bad request, 3 when the file is not the release's, 4 when it cannot
// copy or start it, 6 when SHA256SUMS cannot be downloaded.

#pragma once

#ifdef _WIN32

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace update_apply
{
	constexpr wchar_t kSwitch[] = L"--hikari-apply-update";

	struct Request
	{
		std::string version;
		std::wstring file;
		// The app that asked, for the installer's /WAITPID.
		unsigned long pid = 0;
		bool relaunch = false;
	};

	// The four arguments after the switch, or nothing when any is not
	// what it should be.
	std::optional<Request> Parse(const std::vector<std::wstring> &arguments);

	// The switch and its arguments as one command line tail, quoted.
	std::wstring CommandLine(const Request &request);

	struct Environment
	{
		// This copy's version and install folder, and its scope.
		std::string installed;
		std::wstring installDir;
		bool allUsers = false;
		// Where the private copy's folder is made, for all users.
		std::wstring stagingParent;
		// The release's SHA256SUMS, or nothing when it cannot be had.
		std::function<std::optional<std::string>(const std::string &version)> fetchSums;
		// Starts the installer with its arguments; false when it cannot.
		std::function<bool(const std::wstring &installer, const std::wstring &arguments)> run;
	};

	int Apply(const Request &request, const Environment &environment);

	// The real pieces of an Environment.

	// A GET over HTTPS, with WinHTTP's certificate checks, of at most
	// limit bytes; the final URL after redirects must pass allowed.
	std::optional<std::string> FetchHttps(const std::string &url,
		const std::function<bool(const std::string &finalUrl)> &allowed, size_t limit);

	// CreateProcessW, with the installer's folder as its working folder.
	bool Launch(const std::wstring &installer, const std::wstring &arguments);

	// %windir%\Temp.
	std::wstring SystemTempFolder();

	// Whether this process runs elevated.
	bool IsElevated();

	// What the user may do to a path: the access mask AccessCheck grants
	// for MAXIMUM_ALLOWED, or nothing when it cannot be told.
	using RightsOf = std::function<std::optional<unsigned long>(const std::wstring &path)>;
	struct Entry
	{
		std::wstring name;
		bool folder = false;
		// A symbolic link, junction or other reparse point.
		bool reparse = false;
	};
	// A folder's entries, or nothing when it cannot be listed.
	using ListOf = std::function<std::optional<std::vector<Entry>>(const std::wstring &folder)>;

	// Whether elevating the program in installDir, a plain "X:\..." path,
	// runs only what administrators put there: the user can write,
	// delete, re-own or re-permission nothing in installDir or under it,
	// and can neither delete nor replace any folder on the way to it.
	// Anything that cannot be told, a reparse point in the folder among
	// them, counts as changeable.
	bool IsProtected(const std::wstring &installDir, const RightsOf &rightsOf, const ListOf &list);
	// IsProtected() for the user of this process, on the disk.
	bool IsProtectedFromUser(const std::wstring &installDir);
}

#endif
