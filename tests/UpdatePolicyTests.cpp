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
#include "../HikariSub/JsonLimits.h"
#include "../HikariSub/UpdatePolicy.h"
#include "../HikariSub/VersionHikariSub.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace update_policy;

namespace {

Release Stable(const char *tag)
{
	return Release{ tag, false, false };
}

Release Pre(const char *tag)
{
	return Release{ tag, false, true };
}

Release Draft(const char *tag)
{
	return Release{ tag, true, false };
}

// The tag ChooseRelease offers, or "" when it offers none.
std::string Offered(const std::vector<Release> &releases, const char *installed, bool stableOnly)
{
	Choice choice = ChooseRelease(releases, installed, stableOnly);
	return choice.offer < 0 ? std::string() : releases[choice.offer].tag;
}

std::string Skipped(const std::vector<Release> &releases, const char *installed, bool stableOnly)
{
	Choice choice = ChooseRelease(releases, installed, stableOnly);
	return choice.skippedPrerelease < 0 ? std::string() : releases[choice.skippedPrerelease].tag;
}

Outcome OutcomeOf(const std::vector<Release> &releases, const char *installed, bool stableOnly)
{
	return GetOutcome(ChooseRelease(releases, installed, stableOnly));
}

// GitHub's list for this repo when rc.2 was the newest release, newest
// first, with the Qt metadata releases between the candidates.
const std::vector<Release> kRepoReleases = {
	Pre("v0.0.1-rc.2"),
	Pre("qt-metadata-windows-6.11.2-20261002"),
	Pre("qt-metadata-linux-6.11.2-20261002"),
	Pre("v0.0.1-rc.1"),
};

} // namespace

TEST(update_rc1_is_offered_the_highest_newer_candidate)
{
	std::vector<Release> releases = { Pre("v0.0.1-rc.3"), Pre("v0.0.1-rc.2"), Pre("v0.0.1-rc.1") };
	CHECK(Offered(releases, "0.0.1-rc.1", false) == "v0.0.1-rc.3");
	CHECK(Offered(releases, "0.0.1-rc.1", true) == "v0.0.1-rc.3");
	CHECK(OutcomeOf(releases, "0.0.1-rc.1", true) == Outcome::Offer);
	CHECK(Offered(kRepoReleases, "0.0.1-rc.1", true) == "v0.0.1-rc.2");
}

TEST(update_highest_version_wins_in_any_list_order)
{
	std::vector<Release> releases = { Pre("v0.0.1-rc.2"), Pre("v0.0.1-rc.10"), Pre("v0.0.1-rc.3"),
		Stable("v0.0.1"), Pre("v0.0.2-rc.1"), Pre("v0.0.1-rc.4") };
	CHECK(Offered(releases, "0.0.1-rc.1", true) == "v0.0.2-rc.1");
	CHECK(Offered(releases, "0.0.1-rc.1", false) == "v0.0.2-rc.1");

	std::vector<Release> candidates = { Pre("v0.0.1-rc.2"), Pre("v0.0.1-rc.10"), Pre("v0.0.1-rc.3") };
	CHECK(Offered(candidates, "0.0.1-rc.1", true) == "v0.0.1-rc.10");

	// The oldest listed first, as an old release published late would be.
	std::vector<Release> stables = { Stable("v1.0.0"), Stable("v1.2.0"), Stable("v1.1.0") };
	CHECK(Offered(stables, "1.0.0", true) == "v1.2.0");
}

TEST(update_stable_only_keeps_prereleases_from_a_stable_build)
{
	std::vector<Release> releases = { Pre("v0.0.2-rc.2"), Pre("v0.0.2-rc.1"), Stable("v0.0.1") };
	CHECK(Offered(releases, "0.0.1", true).empty());
	CHECK(OutcomeOf(releases, "0.0.1", true) == Outcome::OnlyPrereleaseNewer);
	CHECK(Skipped(releases, "0.0.1", true) == "v0.0.2-rc.2");
	CHECK(Offered(releases, "0.0.1", false) == "v0.0.2-rc.2");
	CHECK(OutcomeOf(releases, "0.0.1", false) == Outcome::Offer);

	// A suffix makes a prerelease without GitHub's flag, and the flag makes
	// one without a suffix.
	CHECK(Offered({ Stable("v0.0.2-rc.1") }, "0.0.1", true).empty());
	CHECK(Offered({ Pre("v0.0.2") }, "0.0.1", true).empty());
	CHECK(Skipped({ Pre("v0.0.2") }, "0.0.1", true) == "v0.0.2");

	// A newer stable release is offered past a higher prerelease.
	std::vector<Release> mixed = { Pre("v0.1.0-rc.1"), Stable("v0.0.2"), Stable("v0.0.3") };
	CHECK(Offered(mixed, "0.0.1", true) == "v0.0.3");
	CHECK(OutcomeOf(mixed, "0.0.1", true) == Outcome::Offer);
}

TEST(update_release_candidate_sees_newer_candidates_and_stables_with_stable_only)
{
	CHECK(Offered({ Pre("v0.0.1-rc.3") }, "0.0.1-rc.2", true) == "v0.0.1-rc.3");
	CHECK(Offered({ Stable("v0.0.1") }, "0.0.1-rc.2", true) == "v0.0.1");
	CHECK(Offered({ Stable("v0.0.1"), Pre("v0.0.1-rc.3") }, "0.0.1-rc.2", true) == "v0.0.1");
	CHECK(Offered({ Stable("v0.0.1"), Pre("v0.0.2-rc.1") }, "0.0.1-rc.2", true) == "v0.0.2-rc.1");
	CHECK(Skipped({ Pre("v0.0.1-rc.3") }, "0.0.1-rc.2", true).empty());
}

TEST(update_ignores_drafts_non_semver_tags_and_older_releases)
{
	std::vector<Release> releases = { Draft("v0.0.1-rc.9"), Pre("qt-metadata-linux-6.11.2-20261002"),
		Stable("v1.0.0.1537"), Stable(""), Stable("nightly"), Stable("v2.0"), Stable("v01.0.0") };
	CHECK(OutcomeOf(releases, "0.0.1-rc.1", false) == Outcome::UpToDate);
	CHECK(Skipped(releases, "0.0.1", true).empty());

	// Not newer: the same version, an earlier candidate, and a candidate of
	// the release that is installed.
	std::vector<Release> older = { Pre("v0.0.1-rc.2"), Pre("v0.0.1-rc.1"), Pre("v0.0.0") };
	CHECK(OutcomeOf(older, "0.0.1-rc.2", false) == Outcome::UpToDate);
	CHECK(OutcomeOf({ Pre("v0.0.1-rc.3") }, "0.0.1", false) == Outcome::UpToDate);

	// The Qt metadata releases are newer by list order only.
	CHECK(OutcomeOf(kRepoReleases, "0.0.1-rc.2", true) == Outcome::UpToDate);
	CHECK(OutcomeOf({}, "0.0.1-rc.2", true) == Outcome::UpToDate);
}

TEST(update_message_decision)
{
	CHECK(GetOutcome(Choice{ 0, -1 }) == Outcome::Offer);
	// Something to offer wins over a skipped prerelease.
	CHECK(GetOutcome(Choice{ 1, 0 }) == Outcome::Offer);
	CHECK(GetOutcome(Choice{ -1, 2 }) == Outcome::OnlyPrereleaseNewer);
	CHECK(GetOutcome(Choice{}) == Outcome::UpToDate);

	// Only a stable build with "stable versions only" is ever told about a
	// skipped prerelease.
	std::vector<Release> releases = { Pre("v0.0.2-rc.1") };
	CHECK(OutcomeOf(releases, "0.0.1", true) == Outcome::OnlyPrereleaseNewer);
	CHECK(OutcomeOf(releases, "0.0.1", false) == Outcome::Offer);
	CHECK(OutcomeOf(releases, "0.0.2-rc.0", true) == Outcome::Offer);
}

TEST(update_installed_version_that_is_not_semver_offers_nothing)
{
	CHECK(OutcomeOf({ Stable("v9.0.0") }, "1.0.0.1537", false) == Outcome::UpToDate);
	CHECK(OutcomeOf({ Stable("v9.0.0") }, "", true) == Outcome::UpToDate);
}

TEST(update_stable_only_applies_to_stable_builds)
{
	CHECK(SkipsPrereleases(true, "0.0.1"));
	CHECK(SkipsPrereleases(true, "1.2.3+build.4"));
	CHECK(!SkipsPrereleases(true, "0.0.1-rc.1"));
	CHECK(!SkipsPrereleases(false, "0.0.1"));
	CHECK(!SkipsPrereleases(false, "0.0.1-rc.1"));
	CHECK(SkipsPrereleases(true, VersionHikariSub) == ParseSemVer(VersionHikariSub)->prerelease.empty());
}

TEST(update_options_default_when_config_does_not_hold_them)
{
	CHECK(kAutoCheckDefault);
	CHECK(kStableOnlyDefault);
	CHECK(ReadOption("true", false));
	CHECK(!ReadOption("false", true));
	CHECK(ReadOption("", kAutoCheckDefault));
	CHECK(ReadOption("1", kAutoCheckDefault));
	CHECK(ReadOption("TRUE", kStableOnlyDefault));
	CHECK(!ReadOption("", false));
}

TEST(update_rc2_config_turns_the_check_on_and_sees_the_next_candidate)
{
	// What rc.1 and rc.2 left in Config.txt: the stable-only default they
	// wrote out, and no UPDATER_AUTO_CHECK, which only the update dialog
	// wrote and which nothing in those builds could open.
	bool autoCheck = ReadOption("", kAutoCheckDefault);
	bool stableOnly = ReadOption("true", kStableOnlyDefault);
	CHECK(autoCheck);
	CHECK(stableOnly);
	std::vector<Release> releases = { Pre("v0.0.1-rc.3"), Pre("qt-metadata-linux-6.11.2-20261002"),
		Pre("v0.0.1-rc.2"), Pre("v0.0.1-rc.1") };
	CHECK(Offered(releases, "0.0.1-rc.2", stableOnly) == "v0.0.1-rc.3");
	CHECK(Offered(releases, "0.0.1-rc.1", stableOnly) == "v0.0.1-rc.3");

	// A choice saved from the update dialog stays.
	CHECK(!ReadOption("false", kAutoCheckDefault));
	CHECK(!ReadOption("false", kStableOnlyDefault));
}

TEST(update_release_page_is_built_from_the_tag)
{
	CHECK(ReleasePageUrl("v0.0.1-rc.3") == "https://github.com/altqx/hikari/releases/tag/v0.0.1-rc.3");
	CHECK(ReleasePageUrl("v1.2.3+build.4") == "https://github.com/altqx/hikari/releases/tag/v1.2.3+build.4");
	CHECK(ReleasePageUrl("").empty());
	CHECK(ReleasePageUrl(".").empty());
	CHECK(ReleasePageUrl("..").empty());
	CHECK(ReleasePageUrl("../../evil").empty());
	CHECK(ReleasePageUrl("v1.0.0/../../../evil").empty());
	CHECK(ReleasePageUrl("v1.0.0?x=1").empty());
	CHECK(ReleasePageUrl("v1.0.0#x").empty());
	CHECK(ReleasePageUrl("v1.0.0 x").empty());
	CHECK(ReleasePageUrl("file:///etc/passwd").empty());
	CHECK(ReleasePageUrl("https://evil.example/").empty());
	CHECK(ReleasePageUrl("v1.0.0\"").empty());
	CHECK(ReleasePageUrl("v1.0.0%2F").empty());
	CHECK(ReleasesApiUrl() == "https://api.github.com/repos/altqx/hikari/releases?per_page=50");

	// Every tag ChooseRelease can pick is a version, and so has a page.
	for (const char *tag : { "v0.0.1-rc.2", "0.0.1", "V2.0.0-beta.1+exp.sha.5114f85" }) {
		CHECK(ParseSemVer(tag));
		CHECK(!ReleasePageUrl(tag).empty());
	}
}

TEST(update_next_check_backs_off_a_day_after_a_reply_and_an_hour_after_a_failure)
{
	const long long now = 2000000000;
	CHECK(NextCheck(now, 0, true) == now + kDay);
	CHECK(NextCheck(now, 0, false) == now + kHour);
	CHECK(NextCheck(now, now - kWeek, true) == now + kDay);
	CHECK(NextCheck(now, now, false) == now + kHour);
}

TEST(update_next_check_keeps_remind_me_in_a_week)
{
	const long long now = 2000000000;
	// "Remind me in a week" two days ago: four days and more are left.
	const long long snoozed = now - 2 * kDay + kWeek;
	CHECK(NextCheck(now, snoozed, true) == snoozed);
	CHECK(NextCheck(now, snoozed, false) == snoozed);
	CHECK(NextCheck(now, now + kWeek, true) == now + kWeek);
	// A back-off shorter than the new one is replaced.
	CHECK(NextCheck(now, now + kHour, true) == now + kDay);
	CHECK(NextCheck(now, now + 10, false) == now + kHour);
	// Past a week ahead, the clock was turned back since it was set.
	CHECK(NextCheck(now, now + kWeek + 1, true) == now + kDay);
	CHECK(NextCheck(now, now + 365 * kDay, false) == now + kHour);
}

TEST(update_check_that_never_ends_backs_off_an_hour)
{
	// CheckTracker::Begin() records NextCheckAtStart before the request is
	// made, and only a reply records more.
	const long long now = 2000000000;
	long long next = NextCheckAtStart(now, now - kDay);
	CHECK(next == now + kHour);
	// The next starts within the hour do not check again; the first after
	// it does.
	CHECK(!IsCheckDue(now + 1, next));
	CHECK(!IsCheckDue(now + kHour - 1, next));
	CHECK(IsCheckDue(now + kHour, next));
	// A check that was never recorded would be due at every start.
	CHECK(IsCheckDue(now + 1, now - kDay));

	// A reply after the start moves it to a day.
	CHECK(NextCheck(now + 5, next, true) == now + 5 + kDay);

	// "Remind me in a week" is kept by a check from the menu that fails or
	// never ends.
	const long long snoozed = now + 3 * kDay;
	CHECK(NextCheckAtStart(now, snoozed) == snoozed);
	// A time set before the clock was turned back is not.
	CHECK(NextCheckAtStart(now, now + kWeek + 1) == now + kHour);
	CHECK(NextCheckAtStart(now, 0) == now + kHour);
}

TEST(update_check_is_due)
{
	const long long now = 2000000000;
	CHECK(IsCheckDue(now, 0));
	CHECK(IsCheckDue(now, now));
	CHECK(IsCheckDue(now, now - 1));
	CHECK(!IsCheckDue(now, now + 1));
	CHECK(!IsCheckDue(now, now + kDay));
	CHECK(!IsCheckDue(now, now + kWeek));
	CHECK(IsCheckDue(now, now + kWeek + 1));
	CHECK(IsCheckDue(now, now + 365 * kDay));
}

TEST(update_startup_check_keeps_quiet_about_a_declined_version)
{
	CHECK(WasDeclined("v0.0.1-rc.3", "v0.0.1-rc.3"));
	CHECK(WasDeclined("v0.0.1-rc.3", "0.0.1-rc.3"));
	CHECK(WasDeclined("v0.0.1-rc.3", "v0.0.1-rc.4"));
	CHECK(!WasDeclined("v0.0.1-rc.4", "v0.0.1-rc.3"));
	CHECK(!WasDeclined("v0.0.1", "v0.0.1-rc.9"));
	CHECK(!WasDeclined("v0.0.1-rc.3", ""));
	CHECK(!WasDeclined("v0.0.1-rc.3", "garbage"));
	CHECK(!WasDeclined("garbage", "v0.0.1-rc.3"));
}

namespace {

// What UpdateRequest::OnData keeps of a reply of total bytes that comes in
// pieces of piece bytes: the bytes kept, or 0 once it was cut off.
size_t ReceiveReply(size_t total, size_t piece)
{
	size_t received = 0;
	while (received < total) {
		size_t size = total - received < piece ? total - received : piece;
		if (!FitsReply(received, size))
			return 0;
		received += size;
	}
	return received;
}

} // namespace

TEST(update_reply_cap_is_the_json_parsers)
{
	// JsonValue::Parse() refuses input over json_limits::kMaxInput, so a
	// reply the request keeps can always be parsed, and one it would parse
	// is never cut off.
	static_assert(kMaxResponseBytes == json_limits::kMaxInput);
	CHECK(kMaxResponseBytes == json_limits::kMaxInput);
	// A page of the largest releases is half the cap at most.
	CHECK(kReleasesPerPage * kLargestReleaseBytes <= kMaxResponseBytes / 2);
	// The qt-metadata releases measured on GitHub: 29836 and 29808 bytes.
	CHECK(kLargestReleaseBytes >= 29836);
	CHECK(kReleasesPerPage <= 100);
}

TEST(update_reply_between_the_old_caps_is_cut_off_as_it_comes)
{
	const size_t mib = 1024 * 1024;
	// The cap was 8 MiB and the parser's 4 MiB: a 6 MiB reply was kept
	// whole, then refused by the parser. It is now cut off at the first
	// piece past 4 MiB.
	CHECK(ReceiveReply(6 * mib, 16 * 1024) == 0);
	CHECK(ReceiveReply(4 * mib + 1, 16 * 1024) == 0);
	CHECK(ReceiveReply(8 * mib, 16 * 1024) == 0);
	CHECK(ReceiveReply(6 * mib, 6 * mib) == 0);
	CHECK(!FitsReply(4 * mib, 1));
	CHECK(!FitsReply(0, 4 * mib + 1));
	// Up to the parser's limit, it is kept whole, in pieces of any size.
	CHECK(ReceiveReply(json_limits::kMaxInput, 16 * 1024) == json_limits::kMaxInput);
	CHECK(ReceiveReply(json_limits::kMaxInput, 1000) == json_limits::kMaxInput);
	CHECK(ReceiveReply(json_limits::kMaxInput, json_limits::kMaxInput) == json_limits::kMaxInput);
	CHECK(FitsReply(4 * mib - 1, 1));
	// A full page of the largest releases.
	CHECK(ReceiveReply(kReleasesPerPage * kLargestReleaseBytes, 16 * 1024) ==
		kReleasesPerPage * kLargestReleaseBytes);
	// No overflow when a piece is larger than what is left.
	CHECK(!FitsReply(json_limits::kMaxInput, static_cast<size_t>(-1)));
	CHECK(!FitsReply(json_limits::kMaxInput + 1, 0));
}

TEST(update_next_check_outlasts_2038)
{
	// 2038-01-19T03:14:08Z, one past what an int holds, and 2100.
	const long long past2038 = 2147483648LL;
	const long long in2100 = 4102444800LL;
	CHECK(FormatNextCheck(past2038) == "2147483648");
	CHECK(ParseNextCheck(FormatNextCheck(past2038)) == past2038);
	CHECK(ParseNextCheck(FormatNextCheck(in2100)) == in2100);
	// A check set after 2038 is not due before it, and the back-off holds.
	CHECK(!IsCheckDue(in2100, ParseNextCheck(FormatNextCheck(NextCheck(in2100, 0, false)))));
	CHECK(IsCheckDue(in2100 + kHour, ParseNextCheck(FormatNextCheck(NextCheck(in2100, 0, false)))));
	CHECK(ParseNextCheck(FormatNextCheck(NextCheck(in2100, 0, true))) == in2100 + kDay);

	// What an earlier build wrote as an int reads the same.
	CHECK(ParseNextCheck("2000003600") == 2000003600LL);
	// Nothing, or anything else, is due.
	CHECK(ParseNextCheck("") == 0);
	CHECK(ParseNextCheck("soon") == 0);
	CHECK(ParseNextCheck("12x") == 0);
	CHECK(ParseNextCheck(" 12") == 0);
	CHECK(ParseNextCheck("99999999999999999999") == 0);
	CHECK(IsCheckDue(in2100, ParseNextCheck("")));
}

namespace {

// UPDATER_NEXT_CHECK, and how often it was written out.
struct FakeNextCheck : NextCheckStore
{
	long long value = 0;
	int saves = 0;

	long long Load() const override { return value; }
	void Save(long long nextCheck) override
	{
		value = nextCheck;
		saves++;
	}
};

const Ending kEndings[] = { Ending::NoRequest, Ending::Failed, Ending::Cancelled, Ending::TimedOut,
	Ending::CutOff, Ending::NotFromApi, Ending::NotAList, Ending::NotReadToEnd, Ending::Answered };

const RequestEnd kRequestEnds[] = { RequestEnd::Completed, RequestEnd::Cancelled, RequestEnd::Failed };

} // namespace

TEST(update_every_ending_of_a_check_is_recorded_once)
{
	const long long now = 2000000000;
	for (Ending ending : kEndings) {
		FakeNextCheck store;
		store.value = now - kDay;
		CheckTracker tracker(store);
		CHECK(tracker.Begin(now, false) == Start::Started);
		// Saved before the request is made, so a kill from here on still
		// backs off the hour.
		CHECK(store.saves == 1);
		CHECK(store.value == now + kHour);

		tracker.End(now + 30, ending);
		// A second word of the same check, as a state event after the
		// cut-off's cancel would be, changes nothing.
		tracker.End(now + 40, Ending::Answered);
		tracker.End(now + 40, Ending::Failed);
		tracker.Done();
		if (ending == Ending::Answered) {
			CHECK(store.saves == 2);
			CHECK(store.value == now + 30 + kDay);
		}
		else {
			// No session or request, a failure, a cancel, the deadline,
			// a reply cut off, from elsewhere, not a release list or one
			// not read to its end: the hour from the start stands.
			CHECK(store.saves == 1);
			CHECK(store.value == now + kHour);
		}
		CHECK(!tracker.IsRunning());
	}
}

TEST(update_check_ended_without_a_word_keeps_the_hour)
{
	// Done() with no End(), as when the request's handler went first.
	const long long now = 2000000000;
	FakeNextCheck store;
	CheckTracker tracker(store);
	CHECK(tracker.Begin(now, true) == Start::Started);
	tracker.Done();
	CHECK(store.saves == 1);
	CHECK(store.value == now + kHour);
	// An End() after it is not this check's, and records nothing.
	tracker.End(now + 5, Ending::Answered);
	CHECK(store.saves == 1);
}

TEST(update_check_keeps_remind_me_in_a_week_whatever_the_ending)
{
	const long long now = 2000000000;
	const long long snoozed = now + 3 * kDay;
	for (Ending ending : kEndings) {
		FakeNextCheck store;
		store.value = snoozed;
		CheckTracker tracker(store);
		// A check from the menu during the week.
		CHECK(tracker.Begin(now, true) == Start::Started);
		tracker.End(now + 5, ending);
		tracker.Done();
		CHECK(store.value == snoozed);
	}
}

TEST(update_one_check_runs_at_a_time)
{
	const long long now = 2000000000;
	FakeNextCheck store;
	CheckTracker tracker(store);

	// The startup check is running when the menu asks: the menu's check
	// joins it, so that it reports, and no second request, record or
	// dialog follows.
	CHECK(tracker.Begin(now, false) == Start::Started);
	CHECK(!tracker.Reports());
	CHECK(tracker.Begin(now + 1, true) == Start::Joined);
	CHECK(tracker.Reports());
	CHECK(tracker.Begin(now + 2, true) == Start::Joined);
	CHECK(tracker.Begin(now + 2, false) == Start::Ignored);
	CHECK(tracker.Reports());
	CHECK(store.saves == 1);

	// Once it has ended and while it shows what it found, nothing starts
	// or joins.
	tracker.End(now + 3, Ending::Answered);
	CHECK(store.saves == 2);
	CHECK(tracker.Begin(now + 4, true) == Start::Ignored);
	CHECK(tracker.Begin(now + 4, false) == Start::Ignored);
	CHECK(tracker.Reports());
	CHECK(store.saves == 2);
	tracker.Done();

	// Then the next check starts, and reports only if asked from the menu.
	CHECK(tracker.Begin(now + 5, true) == Start::Started);
	CHECK(tracker.Reports());
	CHECK(store.saves == 3);
	tracker.End(now + 6, Ending::Failed);
	tracker.Done();
	CHECK(tracker.Begin(now + 7, false) == Start::Started);
	CHECK(!tracker.Reports());
	tracker.Done();
	CHECK(!tracker.Reports());
}

TEST(update_check_ends_as_its_request_did)
{
	// Before the deadline and the cut-off, a completed request's reply is
	// read, and the next page is asked for when there is one.
	for (RequestEnd end : kRequestEnds) {
		CheckRun run;
		CHECK(run.TakesData());
		CHECK(run.AsksNextPage());
		std::optional<Ending> ending = run.PageEnded(end);
		CHECK(ending.has_value());
		if (end == RequestEnd::Completed)
			CHECK(*ending == Ending::Answered);
		else if (end == RequestEnd::Cancelled)
			CHECK(*ending == Ending::Cancelled);
		else
			CHECK(*ending == Ending::Failed);
	}
}

TEST(update_deadline_cancels_the_request_and_the_check_times_out)
{
	for (RequestEnd end : { RequestEnd::Cancelled, RequestEnd::Failed }) {
		CheckRun run;
		CHECK(run.Fire(true) == CheckRun::Fired::Cancel);
		CHECK(!run.HasEnded());
		CHECK(run.PageEnded(end) == Ending::TimedOut);
		// A page that came whole before the cancel took is read, but the
		// next one is not asked for: the check ends as TimedOut.
		CHECK(run.PageEnded(RequestEnd::Completed) == Ending::Answered);
		CHECK(!run.AsksNextPage());
		run.Finish();
		CHECK(run.HasEnded());
		// The timer, if it fired again, and a further state event do
		// nothing.
		CHECK(run.Fire(false) == CheckRun::Fired::Nothing);
		for (RequestEnd later : kRequestEnds)
			CHECK(!run.PageEnded(later).has_value());
	}
}

TEST(update_request_that_ended_at_the_deadline_keeps_its_ending)
{
	// wx sets the state before its event comes: a request that is no
	// longer active at the deadline is not cancelled, and its event,
	// on the way, decides how the check ends. A reply that came whole
	// is read, and a failure stays one.
	for (RequestEnd end : kRequestEnds) {
		CheckRun run;
		CHECK(run.Fire(false) == CheckRun::Fired::Wait);
		std::optional<Ending> ending = run.PageEnded(end);
		CHECK(ending.has_value());
		if (end == RequestEnd::Completed)
			CHECK(*ending == Ending::Answered);
		else if (end == RequestEnd::Cancelled)
			CHECK(*ending == Ending::Cancelled);
		else
			CHECK(*ending == Ending::Failed);
		// Past the deadline, though, no further page is asked for.
		CHECK(!run.AsksNextPage());
	}
}

TEST(update_check_whose_cancel_never_comes_back_ends_without_it)
{
	// A backend that loses the cancel's event does not keep every later
	// check from starting: the timer's second firing ends the check.
	CheckRun run;
	CHECK(run.Fire(true) == CheckRun::Fired::Cancel);
	CHECK(run.Fire(true) == CheckRun::Fired::GiveUp);
	CHECK(run.HasEnded());
	CHECK(!run.AsksNextPage());
	// The event, should it come after all, ends nothing more.
	for (RequestEnd end : kRequestEnds)
		CHECK(!run.PageEnded(end).has_value());
	CHECK(run.Fire(false) == CheckRun::Fired::Nothing);

	// The same when the request had ended at the deadline but its event
	// was lost.
	CheckRun ended;
	CHECK(ended.Fire(false) == CheckRun::Fired::Wait);
	CHECK(ended.Fire(false) == CheckRun::Fired::GiveUp);
	CHECK(!ended.PageEnded(RequestEnd::Completed).has_value());
}

TEST(update_cut_off_reply_ends_as_cut_off_whatever_comes)
{
	// The reply was thrown away as it passed the cap, so a request that
	// completed after all, or that the deadline cancelled, is cut off
	// still.
	for (bool deadline : { false, true }) {
		for (RequestEnd end : kRequestEnds) {
			CheckRun run;
			run.CutOff();
			CHECK(!run.TakesData());
			if (deadline)
				CHECK(run.Fire(true) == CheckRun::Fired::Cancel);
			CHECK(run.PageEnded(end) == Ending::CutOff);
		}
	}
}

TEST(update_check_that_ended_ignores_its_timer_and_events)
{
	// The check ends once: a state event after the one that ended it,
	// as a cancelled request's second one would be, and the timer, were
	// it not stopped, while a dialog shows, change nothing.
	CheckRun run;
	CHECK(run.PageEnded(RequestEnd::Completed) == Ending::Answered);
	run.Finish();
	for (RequestEnd end : kRequestEnds)
		CHECK(!run.PageEnded(end).has_value());
	CHECK(run.Fire(true) == CheckRun::Fired::Nothing);
	CHECK(run.Fire(true) == CheckRun::Fired::Nothing);
	CHECK(!run.AsksNextPage());
}

TEST(update_request_times_out)
{
	// A request that hung would keep every later check from starting.
	CHECK(kConnectTimeoutMs > 0);
	CHECK(kDataTimeoutMs > 0);
	CHECK(kConnectTimeoutMs + kDataTimeoutMs <= 5 * 60 * 1000);
	// The backends' timeouts do not bound the whole check on WinHTTP; the
	// deadline does, and leaves a page time to connect.
	CHECK(kCheckDeadlineSeconds * 1000 > kConnectTimeoutMs);
	CHECK(kCheckDeadlineSeconds <= 5 * 60);
	CHECK(kCancelWaitSeconds > 0);
	// A wxTimer takes an int of milliseconds.
	CHECK(kCheckDeadlineSeconds * 1000 <= 2147483647LL);
	CHECK(kCancelWaitSeconds * 1000 <= 2147483647LL);
	// Even a check whose cancel never comes back ends well before the
	// hour's back-off.
	CHECK(kCheckDeadlineSeconds + kCancelWaitSeconds < kHour);
}

TEST(update_reply_is_read_only_from_the_api_over_https)
{
	CHECK(IsApiReplyUrl(ReleasesApiUrl()));
	CHECK(IsApiReplyUrl("https://api.github.com/repos/altqx/hikari/releases?per_page=50"));
	// GitHub's redirect for a renamed repository.
	CHECK(IsApiReplyUrl("https://api.github.com/repositories/123456/releases?per_page=50"));
	CHECK(IsApiReplyUrl("HTTPS://API.GitHub.com/repos/altqx/hikari/releases"));

	// A redirect to plain http, as the curl backend follows.
	CHECK(!IsApiReplyUrl("http://api.github.com/repos/altqx/hikari/releases?per_page=50"));
	CHECK(!IsApiReplyUrl("https://api.github.com.evil.example/repos/altqx/hikari/releases"));
	CHECK(!IsApiReplyUrl("https://api.github.com@evil.example/"));
	CHECK(!IsApiReplyUrl("https://api.github.com:443/repos/altqx/hikari/releases"));
	CHECK(!IsApiReplyUrl("https://evil.example/https://api.github.com/"));
	CHECK(!IsApiReplyUrl("https://github.com/altqx/hikari/releases"));
	CHECK(!IsApiReplyUrl("https://api.github.com"));
	CHECK(!IsApiReplyUrl(""));
	static_assert(IsApiReplyUrl("https://api.github.com/"));
}

namespace {

// A full page of qt-metadata releases, with what else is given at the end.
std::vector<Release> MetadataPage(std::vector<Release> tail = {})
{
	std::vector<Release> page;
	while (page.size() + tail.size() < kReleasesPerPage)
		page.push_back(Pre("qt-metadata-linux-6.11.2-20261002"));
	page.insert(page.end(), tail.begin(), tail.end());
	return page;
}

// The tag ReleasePages offers, or "" when it offers none.
std::string Offered(const ReleasePages &pages)
{
	int offer = pages.GetChoice().offer;
	return offer < 0 ? std::string() : pages.Releases()[offer].tag;
}

std::string Skipped(const ReleasePages &pages)
{
	int skipped = pages.GetChoice().skippedPrerelease;
	return skipped < 0 ? std::string() : pages.Releases()[skipped].tag;
}

// Reads pages as the check does: each is added, and the details of the
// releases chosen are kept from the page they are on, here as the page's
// number and the tag read from it.
struct PageReader
{
	ReleasePages pages;
	ChosenDetails<std::string> chosen;
	int reads = 0;

	PageReader(const char *installed, bool stableOnly)
		: pages(installed, stableOnly)
	{
	}

	bool Add(const std::vector<Release> &page)
	{
		int number = pages.NextPage();
		bool next = pages.Add(page);
		chosen.Keep(pages, [&](size_t index) {
			reads++;
			CHECK(index < page.size());
			return std::to_string(number) + ":" + page.at(index).tag;
		});
		return next;
	}
};

} // namespace

TEST(update_release_list_pages)
{
	CHECK(ReleasesApiUrl(1) == ReleasesApiUrl());
	CHECK(ReleasesApiUrl(2) == "https://api.github.com/repos/altqx/hikari/releases?per_page=50&page=2");
	CHECK(IsApiReplyUrl(ReleasesApiUrl(kMaxPages)));
	CHECK(kMaxPages >= 2);
	CHECK(MetadataPage().size() == kReleasesPerPage);
}

TEST(update_reads_past_a_full_page_whose_versions_are_older)
{
	// 49 qt-metadata releases and rc.2 on the first page, rc.4 on the
	// second: an rc.3 build stopped at the first, on a version, and said
	// it was up to date.
	ReleasePages pages("0.0.1-rc.3", true);
	CHECK(pages.NextPage() == 1);
	std::vector<Release> first = MetadataPage({ Pre("v0.0.1-rc.2") });
	CHECK(first.size() == kReleasesPerPage);
	CHECK(OutcomeOf(first, "0.0.1-rc.3", true) == Outcome::UpToDate);
	CHECK(pages.Add(first));
	CHECK(pages.ReadsNextPage());
	CHECK(pages.NextPage() == 2);
	CHECK(GetOutcome(pages.GetChoice()) == Outcome::UpToDate);
	// Had the window closed here, the check would not have answered.
	CHECK(pages.GetEnding() == Ending::NotReadToEnd);

	CHECK(!pages.Add({ Pre("v0.0.1-rc.4") }));
	CHECK(!pages.ReadsNextPage());
	CHECK(pages.GetEnding() == Ending::Answered);
	CHECK(GetOutcome(pages.GetChoice()) == Outcome::Offer);
	CHECK(Offered(pages) == "v0.0.1-rc.4");
	CHECK(pages.OnLastPage(pages.GetChoice().offer) == 0);
}

TEST(update_reads_past_a_full_page_with_a_newer_version)
{
	// A newer version on the first page and a higher one on the second:
	// the second's is offered, from where it is on its page.
	ReleasePages pages("0.0.1-rc.3", true);
	CHECK(pages.Add(MetadataPage({ Pre("v0.0.1-rc.4") })));
	CHECK(Offered(pages) == "v0.0.1-rc.4");
	CHECK(pages.OnLastPage(pages.GetChoice().offer) == int(kReleasesPerPage) - 1);
	CHECK(!pages.Add({ Pre("qt-metadata-windows-6.11.2-20261002"), Stable("v0.0.1"), Pre("v0.0.1-rc.5") }));
	CHECK(Offered(pages) == "v0.0.1");
	CHECK(pages.GetChoice().offer == int(kReleasesPerPage) + 1);
	CHECK(pages.OnLastPage(pages.GetChoice().offer) == 1);

	// The other way about, the first page's stays chosen, and is not on
	// the page last added, so its details are the ones read with it.
	ReleasePages reversed("0.0.1-rc.3", true);
	CHECK(reversed.Add(MetadataPage({ Stable("v0.0.1") })));
	CHECK(!reversed.Add({ Pre("v0.0.1-rc.5"), Pre("v0.0.1-rc.4") }));
	CHECK(Offered(reversed) == "v0.0.1");
	CHECK(reversed.GetChoice().offer == int(kReleasesPerPage) - 1);
	CHECK(reversed.OnLastPage(reversed.GetChoice().offer) == -1);
	CHECK(reversed.OnLastPage(-1) == -1);

	// The same version on both pages, as a release published while the
	// pages are read moves one from the end of a page to the next: the
	// first stays chosen.
	ReleasePages repeated("0.0.1-rc.3", true);
	CHECK(repeated.Add(MetadataPage({ Pre("v0.0.1-rc.4") })));
	CHECK(!repeated.Add({ Pre("v0.0.1-rc.4") }));
	CHECK(repeated.OnLastPage(repeated.GetChoice().offer) == -1);

	// A prerelease kept out by "stable versions only" is chosen over the
	// pages the same way.
	ReleasePages stable("0.0.1", true);
	CHECK(stable.Add(MetadataPage({ Pre("v0.0.2-rc.1") })));
	CHECK(!stable.Add({ Pre("v0.0.2-rc.2"), Stable("v0.0.1") }));
	CHECK(GetOutcome(stable.GetChoice()) == Outcome::OnlyPrereleaseNewer);
	CHECK(Skipped(stable) == "v0.0.2-rc.2");
	CHECK(stable.OnLastPage(stable.GetChoice().skippedPrerelease) == 0);
}

TEST(update_check_that_stops_with_pages_to_go_is_not_an_answer)
{
	// The window closed after a full page: the next is not asked for,
	// and whatever the pages read hold, a newer version, an older one or
	// none, the check is not an answer, so the next comes in an hour.
	for (std::vector<Release> tail : { std::vector<Release>{ Pre("v0.0.1-rc.4") },
		std::vector<Release>{ Pre("v0.0.1-rc.2") }, std::vector<Release>{} })
	{
		ReleasePages pages("0.0.1-rc.3", true);
		for (int page = 1; page < kMaxPages; page++) {
			CHECK(pages.Add(MetadataPage(tail)));
			CHECK(pages.ReadsNextPage());
			CHECK(pages.GetEnding() == Ending::NotReadToEnd);
		}
	}
}

TEST(update_keeps_the_details_of_the_chosen_releases_from_their_page)
{
	// A higher version on the second page: its details are read from
	// there, and only the chosen release's.
	PageReader forward("0.0.1-rc.3", true);
	CHECK(forward.Add(MetadataPage({ Pre("v0.0.1-rc.4") })));
	CHECK(forward.chosen.offer == "1:v0.0.1-rc.4");
	CHECK(forward.reads == 1);
	CHECK(!forward.Add({ Pre("qt-metadata-windows-6.11.2-20261002"), Stable("v0.0.1"), Pre("v0.0.1-rc.5") }));
	CHECK(forward.chosen.offer == "2:v0.0.1");
	CHECK(forward.chosen.offer == "2:" + Offered(forward.pages));
	CHECK(forward.reads == 2);

	// The first page's stays chosen, with the details read with it; the
	// second page's details are not read.
	PageReader reversed("0.0.1-rc.3", true);
	CHECK(reversed.Add(MetadataPage({ Stable("v0.0.1") })));
	CHECK(!reversed.Add({ Pre("v0.0.1-rc.5"), Pre("v0.0.1-rc.4") }));
	CHECK(reversed.chosen.offer == "1:v0.0.1");
	CHECK(reversed.reads == 1);

	// The same version on both pages: the first's details stay.
	PageReader repeated("0.0.1-rc.3", true);
	CHECK(repeated.Add(MetadataPage({ Pre("v0.0.1-rc.4") })));
	CHECK(!repeated.Add({ Pre("v0.0.1-rc.4") }));
	CHECK(repeated.chosen.offer == "1:v0.0.1-rc.4");
	CHECK(repeated.reads == 1);

	// A prerelease kept out, and a stable version offered, each from its
	// own page.
	PageReader both("0.0.1", true);
	CHECK(both.Add(MetadataPage({ Stable("v0.0.2"), Pre("v0.0.3-rc.1") })));
	CHECK(both.chosen.offer == "1:v0.0.2");
	CHECK(both.chosen.skippedPrerelease == "1:v0.0.3-rc.1");
	CHECK(both.Add(MetadataPage({ Pre("v0.0.3-rc.2") })));
	CHECK(!both.Add({ Stable("v0.0.2"), Stable("v0.0.2-rc.9") }));
	CHECK(both.chosen.offer == "1:v0.0.2");
	CHECK(both.chosen.skippedPrerelease == "2:v0.0.3-rc.2");
	CHECK(both.chosen.skippedPrerelease == "2:" + Skipped(both.pages));
	CHECK(both.reads == 3);

	// Nothing chosen, nothing read.
	PageReader none("0.0.1-rc.3", true);
	CHECK(!none.Add({ Pre("v0.0.1-rc.2"), Draft("v0.0.2") }));
	CHECK(none.chosen.offer.empty());
	CHECK(none.chosen.skippedPrerelease.empty());
	CHECK(none.reads == 0);
}

TEST(update_a_page_that_is_not_full_ends_the_list)
{
	ReleasePages pages("0.0.1-rc.3", true);
	std::vector<Release> page = MetadataPage({ Pre("v0.0.1-rc.4") });
	page.erase(page.begin());
	CHECK(!pages.Add(page));
	CHECK(pages.GetEnding() == Ending::Answered);
	CHECK(Offered(pages) == "v0.0.1-rc.4");

	// The end of the list with no version on it is an answer: nothing is
	// newer.
	ReleasePages none("0.0.1-rc.3", true);
	CHECK(none.Add(MetadataPage()));
	CHECK(!none.Add({ Pre("qt-metadata-windows-6.11.2-20261002"), Draft("v0.0.2") }));
	CHECK(none.GetEnding() == Ending::Answered);
	CHECK(GetOutcome(none.GetChoice()) == Outcome::UpToDate);

	// So is a list that ends on a full page, with an empty one after it.
	ReleasePages empty("0.0.1-rc.3", true);
	CHECK(empty.Add(MetadataPage({ Pre("v0.0.1-rc.2") })));
	CHECK(!empty.Add({}));
	CHECK(empty.GetEnding() == Ending::Answered);
	CHECK(GetOutcome(empty.GetChoice()) == Outcome::UpToDate);
}

TEST(update_reads_kMaxPages_at_most)
{
	// Full pages to the cap: the reading stops, and the choice is from
	// every page read.
	ReleasePages pages("0.0.1-rc.3", true);
	for (int page = 1; page < kMaxPages; page++)
		CHECK(pages.Add(MetadataPage(page == 2 ? std::vector<Release>{ Pre("v0.0.1-rc.4") }
			: std::vector<Release>{ Pre("v0.0.1-rc.2") })));
	CHECK(pages.NextPage() == kMaxPages);
	CHECK(!pages.Add(MetadataPage({ Pre("v0.0.1-rc.1") })));
	CHECK(pages.Releases().size() == kMaxPages * kReleasesPerPage);
	CHECK(!pages.ReadsNextPage());
	// A newer version found is offered, though a higher one may be
	// further down.
	CHECK(pages.GetEnding() == Ending::Answered);
	CHECK(Offered(pages) == "v0.0.1-rc.4");

	// Nothing newer on them is no answer: a higher version may be further
	// down, so the check fails rather than say nothing is newer, whether
	// the pages hold older versions only, a newer prerelease kept out, or
	// no version at all, only qt-metadata releases, drafts and other tags.
	for (std::vector<Release> last : { std::vector<Release>{ Pre("v0.0.1-rc.2") },
		std::vector<Release>{ Pre("v0.0.2-rc.1") }, std::vector<Release>{ Draft("v0.0.2"), Stable("nightly") } })
	{
		ReleasePages nothingNewer("0.0.1", true);
		for (int page = 1; page < kMaxPages; page++)
			CHECK(nothingNewer.Add(MetadataPage()));
		CHECK(!nothingNewer.Add(MetadataPage(last)));
		CHECK(!nothingNewer.ReadsNextPage());
		CHECK(nothingNewer.GetChoice().offer < 0);
		CHECK(nothingNewer.GetEnding() == Ending::NotReadToEnd);
	}
}
