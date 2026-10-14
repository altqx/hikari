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
// libass. A choice copied from another computer, or a renderer that failed
// to load, then still shows and uses a renderer that works. Neither
// xy-VSFilter nor VSFilterMod is used unless it is the stored choice;
// VSFilterMod renders 10 to 30 times slower than xy-VSFilter.
std::wstring ResolveSubtitlesProvider(const std::wstring &stored,
	const std::vector<std::wstring> &csriRenderers);

// The renderers to offer, in order: libass, xy-VSFilter when it loaded, any
// other CSRI renderer, then VSFilterMod when it loaded.
std::vector<std::wstring> OrderSubtitlesProviders(const std::vector<std::wstring> &csriRenderers);

// A renderer to offer only with a warning that it is slow.
bool IsSlowSubtitlesProvider(const std::wstring &name);

// What a change in the fonts folders asks of the renderer before the frame
// is drawn again. libass loads its fonts again, and the subtitles open again
// once it is ready. A CSRI renderer finds a new font only in a script parsed
// after the change: VSFilterMod keeps the glyphs of each Line it has laid
// out for as long as the instance lives, so the scripts parsed before are
// dropped and the subtitles opened again.
enum class FontsReload { ReloadLibass, ParseAgain };
FontsReload FontsReloadFor(const std::wstring &provider);

// What the options store about the renderer: the chosen name, and the
// migration the choice has been through, empty for none.
struct StoredSubtitlesProvider {
	std::wstring provider;
	std::wstring migration;
};

// The migration value MigrateSubtitlesProvider stores.
extern const wchar_t *const SUBTITLES_PROVIDER_MIGRATION_VERSION;

// libass took over from xy-VSFilter as the default renderer. Versions before
// this one stored xy-VSFilter whenever OK was pressed on the Options page
// without another renderer chosen, so a stored xy-VSFilter is most often the
// old default rather than a choice. Settings with no migration value move it
// to libass, once, and gain the migration value, so xy-VSFilter chosen again
// afterwards stays. Any other stored name, VSFilterMod among them, stays.
StoredSubtitlesProvider MigrateSubtitlesProvider(const StoredSubtitlesProvider &stored);
