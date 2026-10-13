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

// The names the options store for libass and for the CSRI renderers of
// xy-VSFilter and VSFilterMod.
extern const wchar_t *const LIBASS_PROVIDER;
extern const wchar_t *const XY_VSFILTER_PROVIDER;
extern const wchar_t *const VSFILTERMOD_PROVIDER;

// The subtitle renderer a stored choice stands for, given the CSRI renderers
// that loaded: the stored name when it is libass or one of them, else
// xy-VSFilter when it loaded, else libass. A choice copied from another
// computer, or a renderer that failed to load, then still shows and uses a
// renderer that works. VSFilterMod renders 10 to 30 times slower than
// xy-VSFilter, so it is used only when chosen.
std::wstring ResolveSubtitlesProvider(const std::wstring &stored,
	const std::vector<std::wstring> &csriRenderers);

// The renderers to offer, in order: xy-VSFilter when it loaded, libass, any
// other CSRI renderer, then VSFilterMod when it loaded.
std::vector<std::wstring> OrderSubtitlesProviders(const std::vector<std::wstring> &csriRenderers);

// A renderer to offer only with a warning that it is slow.
bool IsSlowSubtitlesProvider(const std::wstring &name);
