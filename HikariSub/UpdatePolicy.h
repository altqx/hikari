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

// The update check's decisions, apart from wx and the network so that the
// tests can make them.

#pragma once

#include "SemVer.h"

#include <optional>
#include <string_view>

namespace update_policy
{
	// UPDATER_AUTO_CHECK and UPDATER_CHECK_FOR_STABLE when Config.txt does
	// not hold them.
	constexpr bool kAutoCheckDefault = true;
	constexpr bool kStableOnlyDefault = true;

	// Config.txt holds "true" or "false". Anything else, an option no build
	// has written among them, is the option's default.
	constexpr bool ReadOption(std::string_view stored, bool fallback)
	{
		if (stored == "true")
			return true;
		if (stored == "false")
			return false;
		return fallback;
	}

	// "Stable versions only" keeps prereleases out only for a stable build.
	// A release candidate offers the prereleases after it whatever the
	// option says, or its users would never hear of the next candidate.
	constexpr bool SkipsPrereleases(bool stableOnly, std::string_view installed)
	{
		std::optional<SemVer> version = ParseSemVer(installed);
		return stableOnly && !(version && !version->prerelease.empty());
	}
}
