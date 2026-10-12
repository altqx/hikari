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

#include "JsonLimits.h"
#include "SemVer.h"

#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace update_policy
{
	// One place for a fork to change.
	constexpr std::string_view kRepository = "altqx/hikari";

	// GitHub lists the newest first, and can leave nothing out of a
	// release but by the page size. With ten, a run of other releases, such
	// as the qt-metadata-* ones, two to a Qt update, could push every
	// version off the page; fifty leaves room for 24 Qt updates in a row
	// and keeps the page well under kMaxResponseBytes.
	constexpr size_t kReleasesPerPage = 50;

	inline std::string ReleasesApiUrl()
	{
		return "https://api.github.com/repos/" + std::string(kRepository) + "/releases?per_page=" +
			std::to_string(kReleasesPerPage);
	}

	// The largest kind of release in the list, with room to spare: a
	// qt-metadata one, with 18 assets, is about 30 KB. A version's own,
	// with three assets and its notes, is about 13 KB.
	constexpr size_t kLargestReleaseBytes = 40 * 1024;

	// JsonValue::Parse() reads nothing longer, so a reply is cut off as it
	// comes once it passes this, rather than downloaded whole to be
	// refused, and the check fails.
	constexpr size_t kMaxResponseBytes = json_limits::kMaxInput;

	// A page of the largest releases fills half the cap at most.
	static_assert(kReleasesPerPage * kLargestReleaseBytes <= kMaxResponseBytes / 2,
		"a page of releases can come near the reply cap");

	// Whether a piece of the reply fits after the bytes already received.
	constexpr bool FitsReply(size_t received, size_t piece)
	{
		return received <= kMaxResponseBytes && piece <= kMaxResponseBytes - received;
	}

	// The page a release is opened at, built from its tag and never taken
	// from the reply, so that a reply cannot have the browser open another
	// site or a file: or custom-scheme link. Empty when the tag holds a
	// character a version tag does not.
	inline std::string ReleasePageUrl(std::string_view tag)
	{
		if (tag.empty())
			return std::string();
		for (char c : tag) {
			bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
				c == '-' || c == '.' || c == '+';
			if (!safe)
				return std::string();
		}
		if (tag == "." || tag == "..")
			return std::string();
		return "https://github.com/" + std::string(kRepository) + "/releases/tag/" + std::string(tag);
	}

	constexpr long long kHour = 60 * 60;
	constexpr long long kDay = 24 * kHour;
	constexpr long long kWeek = 7 * kDay;

	// UPDATER_NEXT_CHECK as Config.txt holds it: seconds since 1970, in
	// decimal. It is kept as a string, as an int would wrap in January 2038
	// and every check after would be due; an int written by an earlier
	// build reads the same. Anything that is not a whole number reads as 0,
	// so the check is due.
	inline long long ParseNextCheck(std::string_view stored)
	{
		long long value = 0;
		const char *end = stored.data() + stored.size();
		std::from_chars_result result = std::from_chars(stored.data(), end, value);
		if (stored.empty() || result.ec != std::errc() || result.ptr != end)
			return 0;
		return value;
	}

	inline std::string FormatNextCheck(long long nextCheck)
	{
		return std::to_string(nextCheck);
	}

	// UPDATER_NEXT_CHECK is never set more than a week ahead, so a time
	// further off was set before the clock was turned back, and the check
	// is due rather than put off for as long as the clock moved.
	constexpr bool IsCheckDue(long long now, long long nextCheck)
	{
		return now >= nextCheck || nextCheck > now + kWeek;
	}

	// The next automatic check after one that has just ended, automatic or
	// from the menu. A day after a reply, an hour after a failure, so that
	// starting offline does not put the check off for a day. A later time
	// already set, as "Remind me in a week" sets it, is kept.
	constexpr long long NextCheck(long long now, long long nextCheck, bool answered)
	{
		long long next = now + (answered ? kDay : kHour);
		if (nextCheck > next && !IsCheckDue(now, nextCheck))
			return nextCheck;
		return next;
	}

	// The next automatic check, recorded as a check starts and before
	// anything of it can fail: the check counts as failed until a reply
	// says otherwise. Any ending but a reply, a kill included, so puts the
	// next one off an hour, and a reply then moves it to a day.
	constexpr long long NextCheckAtStart(long long now, long long nextCheck)
	{
		return NextCheck(now, nextCheck, false);
	}

	// How long a request may take to connect, and then to bring the whole
	// reply. A check that hung would keep any other from starting.
	constexpr long kConnectTimeoutMs = 30 * 1000;
	constexpr long kDataTimeoutMs = 60 * 1000;

	// Where the next automatic check's time is kept: UPDATER_NEXT_CHECK,
	// written out at once, in the application, and a variable in the tests.
	class NextCheckStore
	{
	public:
		virtual long long Load() const = 0;
		virtual void Save(long long nextCheck) = 0;

	protected:
		~NextCheckStore() = default;
	};

	// How a check ended.
	enum class Ending
	{
		// There was no web session, or no request could be made.
		NoRequest,
		// The request failed or timed out.
		Failed,
		// The request was cancelled other than by the cut-off.
		Cancelled,
		// The reply passed kMaxResponseBytes and was cut off.
		CutOff,
		// The reply is not a release list.
		NotAList,
		// A release list came, whether or not the window that asked was
		// still open to show what it holds.
		Answered,
	};

	// What came of asking for a check.
	enum class Start
	{
		// The check was started, and its back-off saved.
		Started,
		// A check was running, and now reports what it finds, failure and
		// "up to date" too, as a check from the menu does.
		Joined,
		// A check was running, or showing what it found, and nothing more
		// is done.
		Ignored,
	};

	// The one check that runs at a time, and what it saves. A check counts
	// as failed from its start, before anything of it can fail, so that no
	// way out of it, a kill included, skips the hour's back-off; only a
	// reply moves the next check to a day, and it is the only ending that
	// saves anything more. A second check asked for while one runs does not
	// start: from the menu it joins the running one, and an automatic one
	// is ignored, so there is one reply, one record and one dialog.
	class CheckTracker
	{
	public:
		explicit CheckTracker(NextCheckStore &store)
			: store(store)
		{
		}

		Start Begin(long long now, bool interactive)
		{
			if (running) {
				if (!interactive || ended)
					return Start::Ignored;
				reports = true;
				return Start::Joined;
			}
			store.Save(NextCheckAtStart(now, store.Load()));
			running = true;
			ended = false;
			reports = interactive;
			return Start::Started;
		}

		// Records how the running check ended. Only the first call for a
		// check counts.
		void End(long long now, Ending ending)
		{
			if (!running || ended)
				return;
			ended = true;
			if (ending == Ending::Answered)
				store.Save(NextCheck(now, store.Load(), true));
		}

		// The running check has shown all it will, and another can start.
		void Done()
		{
			running = false;
			ended = false;
			reports = false;
		}

		bool IsRunning() const { return running; }

		// Whether the running check tells the user what it found, and that
		// it failed.
		bool Reports() const { return reports; }

	private:
		NextCheckStore &store;
		bool running = false;
		bool ended = false;
		bool reports = false;
	};

	// Whether a startup check keeps quiet about offered, as the user closed
	// the dialog that offered declined or a later version. A check from the
	// menu offers it anyway.
	constexpr bool WasDeclined(std::string_view offered, std::string_view declined)
	{
		std::optional<SemVer> offeredVersion = ParseSemVer(offered);
		std::optional<SemVer> declinedVersion = ParseSemVer(declined);
		return offeredVersion && declinedVersion && CompareSemVer(*offeredVersion, *declinedVersion) <= 0;
	}

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
