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

#pragma once

// What the header line of Hotkeys.txt or AudioHotkeys.txt says about the file,
// kept free of wxWidgets so the tests can reach it.

#include "SemVer.h"

#include <cwchar>
#include <string>
#include <string_view>

enum class HotkeysFileVersion
{
	// no header, or a build too old to read: the file is replaced by the defaults
	Outdated,
	// an old four-part header, "[<name> 0.8.0.<build>]"
	OldBuild,
	// "[HikariSub v<semver>]", written by this program's SaveHkeys
	Current,
};

struct HotkeysFileHeader
{
	HotkeysFileVersion version = HotkeysFileVersion::Outdated;
	// an old build before 1141, whose lines ConfigConverter has to rewrite
	bool convert = false;
};

// firstLine is the file's first line, with its '['.
inline HotkeysFileHeader ReadHotkeysHeader(std::wstring_view firstLine)
{
	HotkeysFileHeader header;
	if (firstLine.empty() || firstLine[0] != L'[')
		return header;

	std::wstring_view name = firstLine.substr(1, firstLine.find(L']') - 1);
	constexpr std::wstring_view current = L"HikariSub v";
	if (name.substr(0, current.size()) == current) {
		std::wstring_view version = name.substr(current.size());
		version = version.substr(0, version.find(L' '));
		std::string narrow;
		for (wchar_t c : version)
			narrow += (c > 0 && c < 0x80) ? static_cast<char>(c) : '\x01';
		if (ParseSemVer(narrow)) {
			header.version = HotkeysFileVersion::Current;
			return header;
		}
	}

	// 0.8.0.build: the build starts five characters after the first '.'
	size_t first = firstLine.find(L'.');
	if (first == std::wstring_view::npos)
		return header;
	std::wstring_view rest = (first + 5 < firstLine.size()) ? firstLine.substr(first + 5) : std::wstring_view();
	std::wstring build(rest.substr(0, rest.find(L' ')));
	long number = std::wcstol(build.c_str(), nullptr, 10);
	if (number > 487)
		header.version = HotkeysFileVersion::OldBuild;
	header.convert = number < 1141;
	return header;
}

// SaveHkeys leaves out every shortcut without a key, so a short current file
// is the user's choice and is kept as it is. Only an old-build file with ten
// or fewer bindings gets the default shortcuts it does not set.
inline bool HotkeysNeedDefaultFill(HotkeysFileVersion version, int bindings)
{
	return version == HotkeysFileVersion::OldBuild && bindings <= 10;
}

// The fill adds only the default ids the file does not set: a binding the
// file holds is never replaced by its default.
template <class Map>
void FillMissingHotkeys(Map &keys, const Map &defaults)
{
	keys.insert(defaults.begin(), defaults.end());
}

// ConfigConverter rewrites a pre-1141 file with a current "HikariSub v<semver>"
// header and only the file's own bindings. When the defaults were filled in,
// the filled set has to be saved once: the next start reads the rewritten file
// as current, and a current file never gets the defaults back.
inline bool HotkeysNeedWriteBack(const HotkeysFileHeader &header, bool filled)
{
	return header.convert && filled;
}
