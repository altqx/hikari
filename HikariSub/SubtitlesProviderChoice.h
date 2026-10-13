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
#include <vector>

// The name the options store for libass.
extern const wchar_t *const LIBASS_PROVIDER;

// The subtitle renderer a stored choice stands for, given the CSRI renderers
// that loaded (the default one first): the stored name when it is libass or
// one of them, else the default CSRI renderer, else libass. A choice copied
// from another computer, or a renderer that failed to load, then still shows
// and uses a renderer that works.
std::wstring ResolveSubtitlesProvider(const std::wstring &stored,
	const std::vector<std::wstring> &csriRenderers);
