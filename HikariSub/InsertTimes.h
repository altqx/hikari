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

// Whether Insert before starts the new Line at the End of the visible Line
// before the active one, filling the gap between them. activeStart is the
// active Line's Start, which is the new Line's End. Only a gap is filled:
// when the Line before touches or overlaps the active Line, its End is not
// before the new Line's End, and the new Line takes the 4 seconds before
// instead, so its End is never before its Start. Insert after fills the gap
// after the active Line the same way.
inline bool InsertBeforeFillsGap(int previousEnd, int activeStart)
{
	return previousEnd < activeStart;
}
