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

// The menu bar title under the point (x, y), in the bar's client coordinates,
// or -1 when there is none. Titles are laid out from x = 0; title i takes its
// text width, textWidths[i], plus indent. When titleStart is given, it gets
// the x where the found title begins.
//
// Only a point inside the bar (0 <= y < barHeight) is over a title. A Leaving
// event carries the pointer's new position, which can be inside an open menu,
// below the bar, at an x under another title; that must not switch menus.
inline int MenuBarTitleAt(const std::vector<int> &textWidths, int indent,
	int barHeight, int x, int y, int *titleStart = nullptr)
{
	if (y < 0 || y >= barHeight){ return -1; }
	int start = 0;
	for (size_t i = 0; i < textWidths.size(); i++){
		int end = start + textWidths[i] + indent;
		if (x > start && end > x){
			if (titleStart){ *titleStart = start; }
			return (int)i;
		}
		start = end;
	}
	return -1;
}
