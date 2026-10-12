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

#include <cstddef>
#include <vector>

enum class TextComparisonResult
{
	Compared,
	TooLarge,
	OutOfMemory,
};

// Where two texts differ, for the subtitle comparison highlight. The texts
// are matched by Unicode code point (longest common subsequence), so a
// character outside the BMP is one character whether the build stores text
// as UTF-16 (wchar_t on Windows, char16_t) or UTF-32 (wchar_t on Linux,
// char32_t); an unpaired surrogate counts as one character of its own.
// Each differing run is appended to its text's ranges as an inclusive
// [first, last] pair of code unit indexes, so a run always covers whole
// characters and can be cut out with wxString::SubString.
template <typename Unit>
TextComparisonResult CompareTextUnits(const Unit *first, size_t firstLength,
	const Unit *second, size_t secondLength,
	std::vector<int> &firstRanges, std::vector<int> &secondRanges);
