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
	// UpdateChecker records NextCheckAtStart before it makes the request,
	// and nothing more when the check ends without a word: no web session,
	// a request that cannot be made, a reply dropped as the window closes.
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

	// A reply after the start moves it to a day, a failure keeps the hour
	// from when the request failed.
	CHECK(NextCheck(now + 5, next, true) == now + 5 + kDay);
	CHECK(NextCheck(now + 5, next, false) == now + 5 + kHour);

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
