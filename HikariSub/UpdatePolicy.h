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
#include <string>
#include <string_view>
#include <vector>

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

	// What a check needs of one entry of GitHub's release list.
	struct Release
	{
		std::string tag;
		bool draft = false;
		// GitHub's flag. A tag with a prerelease suffix is a prerelease
		// whether or not the flag was set when it was published.
		bool prerelease = false;
	};

	struct Choice
	{
		// The release to offer, as an index into the list, or -1.
		int offer = -1;
		// The highest newer prerelease that "stable versions only" kept
		// out, or -1.
		int skippedPrerelease = -1;
	};

	enum class Outcome
	{
		// A newer release can be offered.
		Offer,
		// Nothing newer can be offered, but a newer prerelease was kept out.
		OnlyPrereleaseNewer,
		UpToDate,
	};

	// Drafts and tags that are not semantic versions, such as the
	// qt-metadata-* releases, are ignored. Of the others newer than
	// installed, the highest is chosen in whatever order GitHub lists them.
	inline Choice ChooseRelease(const std::vector<Release> &releases, std::string_view installed,
		bool stableOnly)
	{
		Choice choice;
		std::optional<SemVer> current = ParseSemVer(installed);
		if (!current)
			return choice;

		bool skipPrereleases = SkipsPrereleases(stableOnly, installed);
		std::optional<SemVer> offered;
		std::optional<SemVer> skipped;
		for (size_t i = 0; i < releases.size(); i++) {
			const Release &release = releases[i];
			if (release.draft)
				continue;
			std::optional<SemVer> version = ParseSemVer(release.tag);
			if (!version || CompareSemVer(*version, *current) <= 0)
				continue;

			bool prerelease = release.prerelease || !version->prerelease.empty();
			std::optional<SemVer> &best = (skipPrereleases && prerelease) ? skipped : offered;
			int &index = (skipPrereleases && prerelease) ? choice.skippedPrerelease : choice.offer;
			if (!best || CompareSemVer(*version, *best) > 0) {
				best = version;
				index = static_cast<int>(i);
			}
		}
		return choice;
	}

	constexpr Outcome GetOutcome(const Choice &choice)
	{
		if (choice.offer >= 0)
			return Outcome::Offer;
		if (choice.skippedPrerelease >= 0)
			return Outcome::OnlyPrereleaseNewer;
		return Outcome::UpToDate;
	}
}
