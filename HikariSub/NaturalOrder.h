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

#include <string_view>

// Natural name order, for the previous and next video file
// (VideoBox::NextFile): letters compared without case (towlower, as
// wxString::CmpNoCase), a run of ASCII digits compared as one number, so
// "Ep 2" comes before "Ep 10" and "ep 3" before "Ep 4". Names that are equal
// that way ("a" and "A", "Ep 01" and "Ep 1") are ordered by their plain code
// units, so the order is total and does not depend on the folder's listing.
// Returns <0, 0 or >0; 0 only for equal strings.
int NaturalCompare(std::wstring_view first, std::wstring_view second);
