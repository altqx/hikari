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

// The Line SubsGrid::NextLine copies when no shown Line follows the active
// one and it adds a Line at the end: the last shown Line, which is then the
// active one. count is the number of Lines and isShown(i) whether Line i is
// shown in the grid. With no Line shown, the last Line.
template <typename IsShown>
size_t AppendedLineSource(size_t count, IsShown isShown)
{
	for (size_t i = count; i > 0; i--){
		if (isShown(i - 1)){ return i - 1; }
	}
	return count - 1;
}
