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
const std::vector<std::wstring> modFirst = { L"vsfiltermod_textsub", L"xy-vsfilter_textsub" };
const std::vector<std::wstring> xyOnly = { L"xy-vsfilter_textsub" };
const std::vector<std::wstring> modOnly = { L"vsfiltermod_textsub" };
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

TEST(no_choice_takes_xy_vsfilter)
{
	CHECK(Resolves(L"", windowsRenderers, L"xy-vsfilter_textsub"));
	// CSRI lists its renderers in the order they load, so VSFilterMod can come first
	CHECK(Resolves(L"", modFirst, L"xy-vsfilter_textsub"));
}

TEST(no_choice_without_csri_takes_libass)
{
	// Linux, or Windows without the Csri folder
	CHECK(Resolves(L"", noRenderers, L"libass"));
}

TEST(an_unknown_choice_takes_xy_vsfilter)
{
	CHECK(Resolves(L"some_removed_renderer", windowsRenderers, L"xy-vsfilter_textsub"));
	CHECK(Resolves(L"some_removed_renderer", modFirst, L"xy-vsfilter_textsub"));
	CHECK(Resolves(L"some_removed_renderer", noRenderers, L"libass"));
}

TEST(a_csri_renderer_that_is_missing_falls_back)
{
	// a Windows config on Linux: VSFilter there would draw nothing
	CHECK(Resolves(L"xy-vsfilter_textsub", noRenderers, L"libass"));
	CHECK(Resolves(L"vsfiltermod_textsub", noRenderers, L"libass"));
	// VSFilterMod removed: the renderer that would be the default
	CHECK(Resolves(L"vsfiltermod_textsub", xyOnly, L"xy-vsfilter_textsub"));
}

TEST(vsfiltermod_alone_is_never_the_default)
{
	CHECK(Resolves(L"", modOnly, L"libass"));
	CHECK(Resolves(L"some_removed_renderer", modOnly, L"libass"));
	CHECK(Resolves(L"xy-vsfilter_textsub", modOnly, L"libass"));
}

TEST(other_csri_renderers_are_never_the_default)
{
	const std::vector<std::wstring> otherFirst = { L"other_textsub", L"vsfiltermod_textsub" };
	CHECK(Resolves(L"", otherFirst, L"libass"));
	CHECK(Resolves(L"other_textsub", otherFirst, L"other_textsub"));
}

TEST(vsfiltermod_chosen_is_kept)
{
	CHECK(Resolves(L"vsfiltermod_textsub", windowsRenderers, L"vsfiltermod_textsub"));
	CHECK(Resolves(L"vsfiltermod_textsub", modFirst, L"vsfiltermod_textsub"));
	CHECK(Resolves(L"vsfiltermod_textsub", modOnly, L"vsfiltermod_textsub"));
}

TEST(vsfiltermod_is_offered_last)
{
	const std::vector<std::wstring> all = { L"xy-vsfilter_textsub", L"libass", L"vsfiltermod_textsub" };
	CHECK(OrderSubtitlesProviders(windowsRenderers) == all);
	CHECK(OrderSubtitlesProviders(modFirst) == all);
	CHECK(OrderSubtitlesProviders(xyOnly) == (std::vector<std::wstring>{ L"xy-vsfilter_textsub", L"libass" }));
	CHECK(OrderSubtitlesProviders(modOnly) == (std::vector<std::wstring>{ L"libass", L"vsfiltermod_textsub" }));
	CHECK(OrderSubtitlesProviders(noRenderers) == std::vector<std::wstring>{ L"libass" });
	const std::vector<std::wstring> withOther = { L"vsfiltermod_textsub", L"other_textsub", L"xy-vsfilter_textsub" };
	CHECK(OrderSubtitlesProviders(withOther) ==
		(std::vector<std::wstring>{ L"xy-vsfilter_textsub", L"libass", L"other_textsub", L"vsfiltermod_textsub" }));
}

TEST(only_vsfiltermod_is_slow)
{
	CHECK(IsSlowSubtitlesProvider(L"vsfiltermod_textsub"));
	CHECK(!IsSlowSubtitlesProvider(L"xy-vsfilter_textsub"));
	CHECK(!IsSlowSubtitlesProvider(L"libass"));
}

TEST(names_match_exactly)
{
	CHECK(Resolves(L"Libass", noRenderers, L"libass"));
	CHECK(Resolves(L"Libass", windowsRenderers, L"xy-vsfilter_textsub"));
	CHECK(Resolves(L"xy-vsfilter", windowsRenderers, L"xy-vsfilter_textsub"));
}
