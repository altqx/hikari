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

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace update_policy
{
	// One place for a fork to change.
	constexpr std::string_view kRepository = "altqx/hikari";

	// GitHub's release list comes a page at a time, and nothing of a
	// release can be left out of it but by the page size. Its docs give no
	// order for the list, so the check reads every page, until one that is
	// not full, and chooses over all of them (ReleasePages). Fifty to a
	// page keeps a page of the largest releases, the qt-metadata-* ones,
	// two to a Qt update, at half kMaxResponseBytes. kMaxPages bounds the
	// rest: a check reads 200 releases, makes four requests, receives four
	// times kMaxResponseBytes at most, all within kCheckDeadlineSeconds,
	// and leaves any further releases unread. As a higher version may be
	// among those, a check that finds nothing newer in kMaxPages full
	// pages fails rather than say so (GetEnding()), and kMaxPages has to
	// grow before the repository lists that many.
	constexpr size_t kReleasesPerPage = 50;
	constexpr int kMaxPages = 4;

	// The page of the release list, from 1.
	inline std::string ReleasesApiUrl(int page = 1)
	{
		std::string url = "https://api.github.com/repos/" + std::string(kRepository) + "/releases?per_page=" +
			std::to_string(kReleasesPerPage);
		if (page > 1)
			url += "&page=" + std::to_string(page);
		return url;
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

	// Whether a reply's final URL, after any redirects, is GitHub's API over
	// https. The request asks for https, but a backend may follow a
	// redirect to plain http, as wx's curl one does, and a reply from
	// there could show a made-up version and notes. The scheme and host
	// are compared without case; a port, even 443, or user info fails.
	constexpr bool IsApiReplyUrl(std::string_view url)
	{
		constexpr std::string_view kApi = "https://api.github.com/";
		if (url.size() < kApi.size())
			return false;
		for (size_t i = 0; i < kApi.size(); i++) {
			char c = url[i];
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
			if (c != kApi[i])
				return false;
		}
		return true;
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

	// How long a request may take to connect, and then wait for the reply.
	// Neither bounds the whole reply on every backend: WinHTTP applies the
	// data timeout to each read, so a server or proxy that sends a few
	// bytes a minute keeps a request going for as long as the reply cap
	// takes to fill, years at that rate. kCheckDeadlineSeconds does.
	constexpr long kConnectTimeoutMs = 30 * 1000;
	constexpr long kDataTimeoutMs = 60 * 1000;

	// How long a whole check, every page of it, may take. The application
	// then cancels the request, on every platform, and the check fails as
	// TimedOut (CheckRun). It bounds the check rather than each page, as a
	// check that hung would keep any other from starting and a check from
	// the menu has the user wait for all of its pages; a deadline for each
	// page would have to be a quarter of it to keep kMaxPages pages as
	// short, and fail a slow page that the whole check has time for. A
	// check takes seconds; two minutes leaves room for a slow connection.
	constexpr long long kCheckDeadlineSeconds = 2 * 60;

	// How long the check waits, after the deadline, for the request's
	// last event. A backend can lose it: wx 3.3.3's WinHTTP one sends
	// none when the cancel comes while it takes in a piece of the reply.
	// The check then ends without it (CheckRun::Fire()), so that it does
	// not keep every later check from starting.
	constexpr long long kCancelWaitSeconds = 60;

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
		// The request was cancelled other than by the cut-off or the
		// deadline.
		Cancelled,
		// The check passed kCheckDeadlineSeconds: its request was
		// cancelled, or never ended, or the page that came had more after
		// it (CheckRun).
		TimedOut,
		// The reply passed kMaxResponseBytes and was cut off.
		CutOff,
		// The reply came from somewhere other than IsApiReplyUrl().
		NotFromApi,
		// The reply is not a release list.
		NotAList,
		// The reading stopped on a full page, with releases further down
		// unread: the window that asked closed with pages to go, or
		// kMaxPages were read and nothing on them is newer to offer.
		NotReadToEnd,
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

	// How the request of a page ended, as its last state event says.
	enum class RequestEnd
	{
		Completed,
		Cancelled,
		// Failed, or any other state that ends it.
		Failed,
	};

	// One check's way through its pages to its ending, apart from wx's
	// requests and timer so that the tests can make it: what the
	// deadline, the cut-off and each state event of a request do, and
	// which of them decides how the check ends. The check's timer fires
	// at kCheckDeadlineSeconds and, if the check has not ended then,
	// again kCancelWaitSeconds later.
	class CheckRun
	{
	public:
		// What to do when the timer fires.
		enum class Fired
		{
			// The check has ended, and the timer is left stopped.
			Nothing,
			// Cancel the request, and wait kCancelWaitSeconds for its
			// last event.
			Cancel,
			// Wait kCancelWaitSeconds for the last event of a request
			// that has already ended.
			Wait,
			// End the check now, as TimedOut, without the request's last
			// event, and keep the handler that would get it.
			GiveUp,
		};

		// The timer has fired. Only a request still active is cancelled:
		// wx sets the state of one that has ended before its event comes,
		// and that event decides how the check ends.
		Fired Fire(bool requestActive)
		{
			if (ended)
				return Fired::Nothing;
			if (deadlinePassed) {
				ended = true;
				return Fired::GiveUp;
			}
			deadlinePassed = true;
			cancelledAtDeadline = requestActive;
			return requestActive ? Fired::Cancel : Fired::Wait;
		}

		// The reply has passed kMaxResponseBytes. Whether to cancel its
		// request: as at the deadline, only one still active. The pieces
		// of a reply come before its state event, so the request may have
		// completed by the time one passes the cap, and wx 3.3.3's curl
		// backend ends a completed request a second time if it is
		// cancelled, in State_Cancelled, and releases it twice.
		bool CutOff(bool requestActive)
		{
			cutOff = true;
			return requestActive;
		}

		// Whether pieces of the reply are still taken in.
		bool TakesData() const { return !cutOff; }

		// A further request of the same check, after one whose cut-off
		// did not end it, as a release's update manifest that is too
		// large only rules that release out. The deadline stays the
		// check's.
		void NextRequest() { cutOff = false; }

		// A state event has ended the request of a page. How the check
		// ends, Answered when the reply is to be read, or nothing when it
		// has already ended and the event is ignored. A reply that came
		// whole is read even when the deadline came at the same moment.
		std::optional<Ending> PageEnded(RequestEnd end) const
		{
			if (ended)
				return std::nullopt;
			if (cutOff)
				return Ending::CutOff;
			if (end == RequestEnd::Completed)
				return Ending::Answered;
			if (cancelledAtDeadline)
				return Ending::TimedOut;
			if (end == RequestEnd::Cancelled)
				return Ending::Cancelled;
			return Ending::Failed;
		}

		// Whether the next page can be asked for. Past the deadline it is
		// not, and the check ends as TimedOut.
		bool AsksNextPage() const { return !ended && !deadlinePassed; }

		// The check ends with a state event, and the timer is stopped.
		void Finish() { ended = true; }

		bool HasEnded() const { return ended; }

		// The handler of a check that ended without its request's last
		// event (GiveUp) is kept until that event comes, and then
		// released. It cannot go while the check's result shows, as the
		// dialog runs the event loop with the handler's code still on the
		// stack; an event that comes then releases it once the result has
		// been shown.
		void ReportBegins() { reporting = true; }

		// The result has been shown. Whether the request's last event came
		// meanwhile, so that the handler is released now.
		bool ReportEnds()
		{
			reporting = false;
			return lastEventCame;
		}

		// A state event that ended a request came after the check had
		// ended (PageEnded() gave nothing). Whether the handler is
		// released now; while the result shows, ReportEnds() says so.
		bool LateEventReleases()
		{
			lastEventCame = true;
			return !reporting;
		}

	private:
		bool deadlinePassed = false;
		bool cancelledAtDeadline = false;
		bool cutOff = false;
		bool ended = false;
		bool reporting = false;
		bool lastEventCame = false;
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

	// One of a release's files, as GitHub's list gives it.
	struct ReleaseAsset
	{
		std::string name;
		long long size = 0;
		// "sha256:<hex>", or empty for an asset uploaded before GitHub
		// kept digests.
		std::string digest;
	};

	// What a check needs of one entry of GitHub's release list.
	struct Release
	{
		std::string tag;
		bool draft = false;
		// GitHub's flag. A tag with a prerelease suffix is a prerelease
		// whether or not the flag was set when it was published.
		bool prerelease = false;
		std::vector<ReleaseAsset> assets;
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
	// installed, the highest is chosen in whatever order they are listed;
	// of two that are the same version, the first. A list longer by
	// releases at its end so keeps its choice, or chooses one of those.
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

	// The release list as read so far, a page at a time, and the choice
	// over all of it. GitHub's docs give no order for the list, so any
	// page can hold a higher version than those before it, a newer one
	// after a page with only older ones on it too: each page is read up
	// to one that is not full, the end of the list, or to kMaxPages. What
	// was read is then chosen from as one list, by ChooseRelease(). The
	// pages are asked for one after another by number, so a release
	// deleted between two requests moves one from the next page onto the
	// one already read, and that one is not seen; one published between
	// them is seen twice, and the first stays chosen.
	class ReleasePages
	{
	public:
		ReleasePages(std::string_view installed, bool stableOnly)
			: installed(installed)
			, stableOnly(stableOnly)
		{
		}

		// Adds the page NextPage() asked for, and says whether to read the
		// one after it.
		bool Add(const std::vector<Release> &page)
		{
			lastStart = releases.size();
			releases.insert(releases.end(), page.begin(), page.end());
			pages++;
			lastFull = page.size() >= kReleasesPerPage;
			choice = ChooseRelease(releases, installed, stableOnly);
			return ReadsNextPage();
		}

		// Whether the list goes on past the page last added, and the
		// check reads the next one.
		bool ReadsNextPage() const { return lastFull && pages < kMaxPages; }

		// The page to ask for next, from 1.
		int NextPage() const { return pages + 1; }

		// Every release read, in the order the pages and they came.
		const std::vector<Release> &Releases() const { return releases; }

		// The choice over every page read, as indices into Releases().
		const Choice &GetChoice() const { return choice; }

		// Where an index into Releases() is on the page last added, or -1
		// when it is on an earlier one. A choice on an earlier page is the
		// one made then, so its details need reading only from the page
		// it came on.
		int OnLastPage(int index) const
		{
			if (index < 0 || static_cast<size_t>(index) < lastStart)
				return -1;
			return index - static_cast<int>(lastStart);
		}

		// How a check that has stopped reading ends. A list read to its
		// end, a page that is not full, is an answer. One the check
		// stopped reading with pages to go, as the window that asked
		// closed, is not, whatever it holds, so that the next check comes
		// in an hour rather than a day. Nor is one read to kMaxPages full
		// pages with nothing on them newer to offer, as a higher version
		// may be further down; a newer one found there is offered.
		Ending GetEnding() const
		{
			if (lastFull && (ReadsNextPage() || choice.offer < 0))
				return Ending::NotReadToEnd;
			return Ending::Answered;
		}

		const std::string &Installed() const { return installed; }
		bool StableOnly() const { return stableOnly; }

	private:
		std::string installed;
		bool stableOnly;
		std::vector<Release> releases;
		Choice choice;
		size_t lastStart = 0;
		int pages = 0;
		bool lastFull = false;
	};

	// The details, such as the name and notes, of the releases chosen so
	// far. Each page's are read only for a release chosen from it, as the
	// page is added, and kept: a release chosen from an earlier page keeps
	// the details read with it.
	template <class Details>
	struct ChosenDetails
	{
		Details offer{};
		Details skippedPrerelease{};

		// After pages.Add(page), with read(i) the details of page[i].
		template <class Read>
		void Keep(const ReleasePages &pages, Read read)
		{
			KeepOne(pages, pages.GetChoice().offer, read, &offer);
			KeepOne(pages, pages.GetChoice().skippedPrerelease, read, &skippedPrerelease);
		}

	private:
		template <class Read>
		static void KeepOne(const ReleasePages &pages, int index, Read &read, Details *details)
		{
			int onPage = pages.OnLastPage(index);
			if (onPage >= 0)
				*details = read(static_cast<size_t>(onPage));
		}
	};

	// How many releases a check looks at closely, reading the update
	// manifest of each that has one, before it gives up offering one
	// (installers spec, 2.4).
	constexpr size_t kMaxCandidates = 5;

	// The version of a release's tag, without its "v", when the tag can be
	// offered: "v" and a semantic version without build metadata, made of
	// the characters ReleasePageUrl() takes. Empty otherwise. Releases
	// carry no build metadata, and a download's URL is built from the tag.
	inline std::string CandidateVersion(std::string_view tag)
	{
		if (tag.size() < 2 || tag[0] != 'v' || tag.find('+') != std::string_view::npos)
			return std::string();
		if (ReleasePageUrl(tag).empty() || !ParseSemVer(tag.substr(1)))
			return std::string();
		return std::string(tag.substr(1));
	}

	// The releases that may be offered, as indices into the list, highest
	// version first and, of two of one version, the first listed: those
	// that are not drafts, whose tag CandidateVersion() takes, newer than
	// installed. offer fits the channel; skipped holds the prereleases
	// that "stable versions only" kept out. Each has kMaxCandidates at
	// most.
	struct Candidates
	{
		std::vector<int> offer;
		std::vector<int> skipped;
	};

	inline Candidates ListCandidates(const std::vector<Release> &releases, std::string_view installed,
		bool stableOnly)
	{
		Candidates candidates;
		std::optional<SemVer> current = ParseSemVer(installed);
		if (!current)
			return candidates;
		bool skipPrereleases = SkipsPrereleases(stableOnly, installed);
		std::vector<std::string> versions(releases.size());
		std::vector<std::pair<SemVer, int>> offer, skipped;
		for (size_t i = 0; i < releases.size(); i++) {
			const Release &release = releases[i];
			if (release.draft)
				continue;
			versions[i] = CandidateVersion(release.tag);
			if (versions[i].empty())
				continue;
			// The view points into versions[i], which is not resized.
			SemVer version = *ParseSemVer(versions[i]);
			if (CompareSemVer(version, *current) <= 0)
				continue;
			bool prerelease = release.prerelease || !version.prerelease.empty();
			(skipPrereleases && prerelease ? skipped : offer).push_back({ version, static_cast<int>(i) });
		}
		auto sorted = [](std::vector<std::pair<SemVer, int>> &list) {
			std::stable_sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
				return CompareSemVer(a.first, b.first) > 0;
			});
			std::vector<int> indices;
			for (size_t i = 0; i < list.size() && i < kMaxCandidates; i++)
				indices.push_back(list[i].second);
			return indices;
		};
		candidates.offer = sorted(offer);
		candidates.skipped = sorted(skipped);
		return candidates;
	}

	// The details, such as the name and notes, of the candidates so far.
	// Each page's are read only for a candidate on it, as the page is
	// added, and kept while it stays one: a candidate from an earlier page
	// keeps the details read with it.
	template <class Details>
	class CandidateDetails
	{
	public:
		// After pages.Add(page), with read(i) the details of page[i].
		template <class Read>
		void Keep(const ReleasePages &pages, Read read)
		{
			Candidates candidates = ListCandidates(pages.Releases(), pages.Installed(), pages.StableOnly());
			std::vector<std::pair<int, Details>> kept;
			for (const std::vector<int> *list : { &candidates.offer, &candidates.skipped }) {
				for (int index : *list) {
					if (const Details *details = Find(index)) {
						kept.push_back({ index, *details });
						continue;
					}
					int onPage = pages.OnLastPage(index);
					if (onPage >= 0)
						kept.push_back({ index, read(static_cast<size_t>(onPage)) });
				}
			}
			details = std::move(kept);
		}

		// The details of the release at index in pages.Releases(), or
		// nullptr when it is no candidate.
		const Details *Find(int index) const
		{
			for (const auto &entry : details) {
				if (entry.first == index)
					return &entry.second;
			}
			return nullptr;
		}

		size_t Size() const { return details.size(); }

	private:
		std::vector<std::pair<int, Details>> details;
	};

	constexpr Outcome GetOutcome(const Choice &choice)
	{
		if (choice.offer >= 0)
			return Outcome::Offer;
		if (choice.skippedPrerelease >= 0)
			return Outcome::OnlyPrereleaseNewer;
		return Outcome::UpToDate;
	}
}
