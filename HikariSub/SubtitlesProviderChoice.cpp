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

#include "SubtitlesProviderChoice.h"

#include <algorithm>

const wchar_t *const LIBASS_PROVIDER = L"libass";

std::wstring ResolveSubtitlesProvider(const std::wstring &stored,
	const std::vector<std::wstring> &csriRenderers)
{
	if (stored == LIBASS_PROVIDER)
		return stored;
	if (!stored.empty() && std::find(csriRenderers.begin(), csriRenderers.end(), stored) != csriRenderers.end())
		return stored;
	return csriRenderers.empty() ? std::wstring(LIBASS_PROVIDER) : csriRenderers.front();
}
