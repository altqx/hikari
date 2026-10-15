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

// Whether a script gives a layout resolution, LayoutResX, LayoutResY or both,
// in its script info. Only such a script has both values set to its new
// resolution: on loading and on OK in the script properties when the
// resolutions are linked, and on resampling. Info is a SubsFile, or anything
// else whose GetSInfo(L"name") returns the value, empty when it is missing.
template <typename Info>
bool HasLayoutRes(Info &info)
{
	return !info.GetSInfo(L"LayoutResX").empty() || !info.GetSInfo(L"LayoutResY").empty();
}
