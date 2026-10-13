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
const wchar_t *const XY_VSFILTER_PROVIDER = L"xy-vsfilter_textsub";
const wchar_t *const VSFILTERMOD_PROVIDER = L"vsfiltermod_textsub";

namespace {

bool Contains(const std::vector<std::wstring> &names, const std::wstring &name)
{
	return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

std::wstring ResolveSubtitlesProvider(const std::wstring &stored,
	const std::vector<std::wstring> &csriRenderers)
{
	if (stored == LIBASS_PROVIDER)
		return stored;
	if (!stored.empty() && Contains(csriRenderers, stored))
		return stored;
	if (Contains(csriRenderers, XY_VSFILTER_PROVIDER))
		return XY_VSFILTER_PROVIDER;
	return LIBASS_PROVIDER;
}

std::vector<std::wstring> OrderSubtitlesProviders(const std::vector<std::wstring> &csriRenderers)
{
	std::vector<std::wstring> providers;
	if (Contains(csriRenderers, XY_VSFILTER_PROVIDER))
		providers.push_back(XY_VSFILTER_PROVIDER);
	providers.push_back(LIBASS_PROVIDER);
	for (const std::wstring &renderer : csriRenderers) {
		if (renderer != XY_VSFILTER_PROVIDER && !IsSlowSubtitlesProvider(renderer) && !Contains(providers, renderer))
			providers.push_back(renderer);
	}
	if (Contains(csriRenderers, VSFILTERMOD_PROVIDER))
		providers.push_back(VSFILTERMOD_PROVIDER);
	return providers;
}

bool IsSlowSubtitlesProvider(const std::wstring &name)
{
	return name == VSFILTERMOD_PROVIDER;
}
