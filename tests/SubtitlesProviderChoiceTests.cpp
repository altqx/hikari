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

#include "check.h"
#include "../HikariSub/SubtitlesProviderChoice.h"

namespace {

const std::vector<std::wstring> windowsRenderers = { L"xy-vsfilter_textsub", L"vsfiltermod_textsub" };
const std::vector<std::wstring> noRenderers;

bool Resolves(const std::wstring &stored, const std::vector<std::wstring> &renderers, const std::wstring &expected)
{
	return ResolveSubtitlesProvider(stored, renderers) == expected;
}

} // namespace

TEST(libass_stays_libass)
{
	CHECK(Resolves(L"libass", windowsRenderers, L"libass"));
	CHECK(Resolves(L"libass", noRenderers, L"libass"));
}

TEST(a_csri_renderer_that_loaded_stays)
{
	CHECK(Resolves(L"vsfiltermod_textsub", windowsRenderers, L"vsfiltermod_textsub"));
	CHECK(Resolves(L"xy-vsfilter_textsub", windowsRenderers, L"xy-vsfilter_textsub"));
}

TEST(no_choice_takes_the_default_csri_renderer)
{
	CHECK(Resolves(L"", windowsRenderers, L"xy-vsfilter_textsub"));
}

TEST(no_choice_without_csri_takes_libass)
{
	// Linux, or Windows without the Csri folder
	CHECK(Resolves(L"", noRenderers, L"libass"));
}

TEST(a_csri_renderer_that_is_missing_falls_back)
{
	// a Windows config on Linux: VSFilter there would draw nothing
	CHECK(Resolves(L"xy-vsfilter_textsub", noRenderers, L"libass"));
	CHECK(Resolves(L"some_removed_renderer", windowsRenderers, L"xy-vsfilter_textsub"));
}

TEST(names_match_exactly)
{
	CHECK(Resolves(L"Libass", noRenderers, L"libass"));
	CHECK(Resolves(L"Libass", windowsRenderers, L"xy-vsfilter_textsub"));
	CHECK(Resolves(L"xy-vsfilter", windowsRenderers, L"xy-vsfilter_textsub"));
}
