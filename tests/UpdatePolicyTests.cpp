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
#include "../HikariSub/UpdatePolicy.h"
#include "../HikariSub/VersionHikariSub.h"

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
	CHECK(ReleasesApiUrl() == "https://api.github.com/repos/altqx/hikari/releases?per_page=100");

	// Every tag ChooseRelease can pick is a version, and so has a page.
	for (const char *tag : { "v0.0.1-rc.2", "0.0.1", "V2.0.0-beta.1+exp.sha.5114f85" }) {
		CHECK(ParseSemVer(tag));
		CHECK(!ReleasePageUrl(tag).empty());
	}
}

TEST(update_response_limit_holds_a_full_page)
{
	// A hundred releases at the size of the qt-metadata ones, 18 assets
	// and about 40 KB each.
	CHECK(kMaxResponseBytes >= 100 * 40 * 1024);
}
