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

#include <string>
#include <string_view>
#include <vector>

// Natural name order, for the previous and next video file
// (VideoBox::NextFile): letters compared without case (towlower, as
// wxString::CmpNoCase), a run of ASCII digits compared as one number, so
// "Ep 2" comes before "Ep 10" and "ep 3" before "Ep 4". Names that are equal
// that way ("a" and "A", "Ep 01" and "Ep 1") are ordered by their plain code
// units, so the order is total and does not depend on the folder's listing.
// Returns <0, 0 or >0; 0 only for equal strings.
int NaturalCompare(std::wstring_view first, std::wstring_view second);

// Where the current video file sits in its folder's listing, for the previous
// and next file (VideoBox::NextFile). `files` is the listing as wxDir gives
// it (the folder, a separator and a name), sorted with NaturalCompare, and
// `path` is the current file. They are matched by name, as every entry has
// the same folder: on Windows wxFileName::GetPath puts "\\?\" in front of a
// folder of 248 characters or more, so the entries need not start as `path`
// does. With `windowsNames`, a backslash or '/' ends the folder, and when no
// name matches exactly a name equal without case does, as on NTFS.
struct ListingPlace {
	// the matching entry; when none matches, the number of entries whose
	// names sort before the name of `path`
	size_t index;
	bool found;
};
ListingPlace FindInListing(const std::vector<std::wstring> &files, std::wstring_view path, bool windowsNames);
