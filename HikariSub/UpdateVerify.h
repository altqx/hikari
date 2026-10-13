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

// What decides, in the process that installs an update, that the package
// is the release's own file (installers spec, 4.2 and 4.4). Nothing the
// unprivileged app recorded is trusted there: not the pending record, not its
// SHA-256. The installing side takes a version and a file only, downloads
// the release's SHA256SUMS itself from the fixed GitHub URL, and compares
// the file with the digest listed for the release file's own name. The
// Linux update helper does the same in shell. Apart from wx and the
// network so that the tests can decide it.

#pragma once

#include "SemVer.h"
#include "UpdatePolicy.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace update_verify
{
	// The release asset that lists every other one's SHA-256, as
	// sha256sum writes it. The release job publishes it.
	constexpr std::string_view kSumsName = "SHA256SUMS";
	constexpr size_t kMaxSumsBytes = 64 * 1024;

	// The installer's exit codes, the same as the Linux helper's.
	constexpr int kOk = 0;
	constexpr int kBadArguments = 2;
	constexpr int kMismatch = 3;
	constexpr int kFailed = 4;
	constexpr int kNoSums = 6;

	// A release version: SemVer without build metadata, as tags are.
	inline bool IsReleaseVersion(std::string_view version)
	{
		// ParseSemVer() takes a leading "v", which a version here has not.
		if (version.empty() || version.size() > 64 || version[0] < '0' || version[0] > '9')
			return false;
		return !update_policy::CandidateVersion("v" + std::string(version)).empty();
	}

	// Where a release's SHA256SUMS is: built from the version alone.
	// Empty for anything that is not a release version.
	inline std::string SumsUrl(std::string_view version)
	{
		if (!IsReleaseVersion(version))
			return std::string();
		return "https://github.com/" + std::string(update_policy::kRepository) + "/releases/download/v" +
			std::string(version) + "/" + std::string(kSumsName);
	}

	// The NSIS installer's release file name for a version (2.1).
	inline std::string NsisAssetName(std::string_view version)
	{
		return "HikariSub-" + std::string(version) + "-windows-x64-setup.exe";
	}

	inline bool IsLowerHex64(std::string_view text)
	{
		if (text.size() != 64)
			return false;
		for (char c : text) {
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		}
		return true;
	}

	// The digest SHA256SUMS lists for name: a line "<sha256>  <name>", or
	// "<sha256> *<name>", with an optional CR. Nothing when the list is too
	// long, or names the file none or more than once, or its digest is
	// not 64 lowercase hex digits.
	inline std::optional<std::string> FindSum(std::string_view sums, std::string_view name)
	{
		if (sums.size() > kMaxSumsBytes || name.empty())
			return std::nullopt;
		std::optional<std::string> found;
		int count = 0;
		while (!sums.empty()) {
			size_t end = sums.find('\n');
			std::string_view line = sums.substr(0, end);
			sums = end == std::string_view::npos ? std::string_view() : sums.substr(end + 1);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			if (line.size() < 66 || line[64] != ' ')
				continue;
			std::string_view entry = line.substr(66);
			if ((line[65] != ' ' && line[65] != '*') || entry != name)
				continue;
			count++;
			found = std::string(line.substr(0, 64));
		}
		if (count != 1 || !IsLowerHex64(*found))
			return std::nullopt;
		return found;
	}

	enum class Verdict
	{
		Ok,
		// Not a release version, or not newer than the installed one.
		BadVersion,
		// The release's SHA256SUMS does not list the file once.
		NotListed,
		// The file's SHA-256 is not the one listed.
		Mismatch,
	};

	inline Verdict CheckVersion(std::string_view version, std::string_view installed)
	{
		if (!IsReleaseVersion(version))
			return Verdict::BadVersion;
		std::optional<SemVer> wanted = ParseSemVer(version), have = ParseSemVer(installed);
		if (!wanted || !have || CompareSemVer(*wanted, *have) <= 0)
			return Verdict::BadVersion;
		return Verdict::Ok;
	}

	// Whether actual, the SHA-256 of the file as the installing side read
	// it, is the one the release lists for asset.
	inline Verdict CheckDigest(std::string_view sums, std::string_view asset, std::string_view actual)
	{
		std::optional<std::string> expected = FindSum(sums, asset);
		if (!expected)
			return Verdict::NotListed;
		return *expected == actual ? Verdict::Ok : Verdict::Mismatch;
	}

	// The NSIS installer's arguments in /UPDATE mode (4.2). /D= comes last
	// and unquoted, as NSIS takes the rest of the line as the folder.
	inline std::string NsisArguments(unsigned long pid, bool relaunch, bool allUsers, std::string_view installDir)
	{
		std::string arguments = "/S /UPDATE /WAITPID=" + std::to_string(pid);
		if (relaunch)
			arguments += " /RELAUNCH";
		arguments += allUsers ? " /ALLUSERS" : " /CURRENTUSER";
		arguments += " /D=" + std::string(installDir);
		return arguments;
	}
}
