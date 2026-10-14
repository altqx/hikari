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

// What a start of HikariSub leaves in the options.
StoredSubtitlesProvider Start(const StoredSubtitlesProvider &stored)
{
	return MigrateSubtitlesProvider(stored);
}

bool Stores(const StoredSubtitlesProvider &stored, const std::wstring &provider, const std::wstring &migration)
{
	return stored.provider == provider && stored.migration == migration;
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

TEST(no_choice_takes_libass)
{
	CHECK(Resolves(L"", windowsRenderers, L"libass"));
	// CSRI lists its renderers in the order they load, so VSFilterMod can come first
	CHECK(Resolves(L"", modFirst, L"libass"));
	CHECK(Resolves(L"", xyOnly, L"libass"));
	// Linux, or Windows without the Csri folder
	CHECK(Resolves(L"", noRenderers, L"libass"));
}

TEST(an_unknown_choice_takes_libass)
{
	CHECK(Resolves(L"some_removed_renderer", windowsRenderers, L"libass"));
	CHECK(Resolves(L"some_removed_renderer", modFirst, L"libass"));
	CHECK(Resolves(L"some_removed_renderer", noRenderers, L"libass"));
}

TEST(a_csri_renderer_that_is_missing_falls_back_to_libass)
{
	// a Windows config on Linux: VSFilter there would draw nothing
	CHECK(Resolves(L"xy-vsfilter_textsub", noRenderers, L"libass"));
	CHECK(Resolves(L"vsfiltermod_textsub", noRenderers, L"libass"));
	// VSFilterMod removed: libass, not the other VSFilter
	CHECK(Resolves(L"vsfiltermod_textsub", xyOnly, L"libass"));
	CHECK(Resolves(L"xy-vsfilter_textsub", modOnly, L"libass"));
}

TEST(vsfiltermod_alone_is_never_the_default)
{
	CHECK(Resolves(L"", modOnly, L"libass"));
	CHECK(Resolves(L"some_removed_renderer", modOnly, L"libass"));
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

TEST(libass_is_offered_first_and_vsfiltermod_last)
{
	const std::vector<std::wstring> all = { L"libass", L"xy-vsfilter_textsub", L"vsfiltermod_textsub" };
	CHECK(OrderSubtitlesProviders(windowsRenderers) == all);
	CHECK(OrderSubtitlesProviders(modFirst) == all);
	CHECK(OrderSubtitlesProviders(xyOnly) == (std::vector<std::wstring>{ L"libass", L"xy-vsfilter_textsub" }));
	CHECK(OrderSubtitlesProviders(modOnly) == (std::vector<std::wstring>{ L"libass", L"vsfiltermod_textsub" }));
	CHECK(OrderSubtitlesProviders(noRenderers) == std::vector<std::wstring>{ L"libass" });
	const std::vector<std::wstring> withOther = { L"vsfiltermod_textsub", L"other_textsub", L"xy-vsfilter_textsub" };
	CHECK(OrderSubtitlesProviders(withOther) ==
		(std::vector<std::wstring>{ L"libass", L"xy-vsfilter_textsub", L"other_textsub", L"vsfiltermod_textsub" }));
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
	CHECK(Resolves(L"Libass", windowsRenderers, L"libass"));
	CHECK(Resolves(L"xy-vsfilter", windowsRenderers, L"libass"));
	CHECK(Resolves(L"XY-VSFILTER_TEXTSUB", windowsRenderers, L"libass"));
}

TEST(a_new_install_starts_on_libass)
{
	// a new install's defaults store libass and no migration value
	StoredSubtitlesProvider fresh = Start({ L"libass", L"" });
	CHECK(Stores(fresh, L"libass", L"1"));
	CHECK(ResolveSubtitlesProvider(fresh.provider, windowsRenderers) == L"libass");
	// a file with nothing stored, as versions before this one wrote it
	StoredSubtitlesProvider empty = Start({ L"", L"" });
	CHECK(Stores(empty, L"", L"1"));
	CHECK(ResolveSubtitlesProvider(empty.provider, windowsRenderers) == L"libass");
}

TEST(a_stored_xy_vsfilter_moves_to_libass_once)
{
	StoredSubtitlesProvider migrated = Start({ L"xy-vsfilter_textsub", L"" });
	CHECK(Stores(migrated, L"libass", L"1"));
	CHECK(ResolveSubtitlesProvider(migrated.provider, windowsRenderers) == L"libass");
	// the next start leaves it alone
	CHECK(Stores(Start(migrated), L"libass", L"1"));
}

TEST(xy_vsfilter_chosen_after_the_migration_stays)
{
	StoredSubtitlesProvider stored = Start({ L"xy-vsfilter_textsub", L"" });
	// the user picks xy-VSFilter on the Video page; the options save
	stored.provider = L"xy-vsfilter_textsub";
	for (int restart = 0; restart < 3; restart++) {
		stored = Start(stored);
		CHECK(Stores(stored, L"xy-vsfilter_textsub", L"1"));
	}
	CHECK(ResolveSubtitlesProvider(stored.provider, windowsRenderers) == L"xy-vsfilter_textsub");
}

TEST(a_stored_vsfiltermod_stays)
{
	StoredSubtitlesProvider stored = Start({ L"vsfiltermod_textsub", L"" });
	CHECK(Stores(stored, L"vsfiltermod_textsub", L"1"));
	CHECK(ResolveSubtitlesProvider(stored.provider, windowsRenderers) == L"vsfiltermod_textsub");
	CHECK(Stores(Start(stored), L"vsfiltermod_textsub", L"1"));
}

TEST(other_stored_choices_stay_through_the_migration)
{
	CHECK(Stores(Start({ L"libass", L"" }), L"libass", L"1"));
	CHECK(Stores(Start({ L"other_textsub", L"" }), L"other_textsub", L"1"));
	// an unknown name stays stored and resolves to libass
	StoredSubtitlesProvider unknown = Start({ L"some_removed_renderer", L"" });
	CHECK(Stores(unknown, L"some_removed_renderer", L"1"));
	CHECK(ResolveSubtitlesProvider(unknown.provider, windowsRenderers) == L"libass");
}

TEST(settings_with_a_migration_value_are_not_migrated)
{
	CHECK(Stores(Start({ L"xy-vsfilter_textsub", L"1" }), L"xy-vsfilter_textsub", L"1"));
	// a later migration's value, from a newer version
	CHECK(Stores(Start({ L"xy-vsfilter_textsub", L"2" }), L"xy-vsfilter_textsub", L"2"));
}
