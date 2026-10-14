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

// Whether a \move moves over the whole Line instead of over times of its own.
// valueCount is the number of values the tag has, t1 and t2 its two times
// from the Line's start. A \move without both times moves over the whole
// Line, and so does one whose times are both 0 or less, as in libass and
// VSFilter.
inline bool MoveCoversWholeLine(int valueCount, double t1, double t2)
{
	return valueCount < 6 || (t1 <= 0 && t2 <= 0);
}
