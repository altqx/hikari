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

#include <cwctype>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// The names the Font collector writes its font files under, in a folder or
// in a zip archive. Two different fonts can have the same file name, such as
// a per-user and a system arial.ttf, or a font in the external fonts folder:
// written under one name, the later one replaced the earlier one in a folder
// and made a second entry of that name in an archive. The later one now
// gets a name of its own, "arial (2).ttf", then "arial (3).ttf". A file with
// the same bytes as a font already written under its name is the same font,
// and is not written again. Names are compared without case, as Windows
// compares them.
class FontOutputNames
{
public:
	enum Outcome {
		NEW_NAME,  // written under its own file name
		SAME_FONT, // the same bytes are already written, under name
		RENAMED,   // a different font has its file name; written under name
	};
	struct Result {
		Outcome outcome;
		std::wstring name;
	};
	// Whether earlierSource, already written for the same file name, holds
	// the same bytes as the file being added.
	using SameBytes = std::function<bool(const std::wstring &earlierSource)>;

	// The name to write source, whose file name is fileName, under.
	Result Add(const std::wstring &source, const std::wstring &fileName, const SameBytes &sameBytes)
	{
		const std::wstring key = Lower(fileName);
		std::vector<Written> &written = byFileName[key];
		for (const Written &earlier : written) {
			if (sameBytes(earlier.source))
				return { SAME_FONT, earlier.name };
		}
		Result result{ NEW_NAME, fileName };
		if (taken.count(key)) {
			result.outcome = RENAMED;
			const size_t dot = fileName.find_last_of(L'.');
			const std::wstring stem = (dot == std::wstring::npos || dot == 0) ? fileName : fileName.substr(0, dot);
			const std::wstring ext = (dot == std::wstring::npos || dot == 0) ? std::wstring() : fileName.substr(dot);
			for (int number = 2;; number++) {
				result.name = stem + L" (" + std::to_wstring(number) + L")" + ext;
				if (!taken.count(Lower(result.name)))
					break;
			}
		}
		taken.insert(Lower(result.name));
		written.push_back({ source, result.name });
		return result;
	}

	void Clear()
	{
		byFileName.clear();
		taken.clear();
	}

private:
	struct Written {
		std::wstring source;
		std::wstring name;
	};
	static std::wstring Lower(std::wstring name)
	{
		for (wchar_t &ch : name)
			ch = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(ch)));
		return name;
	}
	// lower-case file name -> the fonts written for it
	std::map<std::wstring, std::vector<Written>> byFileName;
	// lower-case names already in the output
	std::set<std::wstring> taken;
};
