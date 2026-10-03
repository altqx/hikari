// P8: the update check against legacy UpdateChecker and SemVer at 20d647c4.

#include "hikari/application/update_check.h"

#include <gtest/gtest.h>

using namespace hikari::application;

TEST(SemVer, ParsesAndOrdersLikeLegacy)
{
    ASSERT_TRUE(parseSemVer("v1.2.3"));
    EXPECT_EQ(parseSemVer("1.2.3-rc.1+build.5")->prerelease, "rc.1");
    for (const char *bad : {"1.2", "1.2.3.4", "01.2.3", "1.2.3-01", "1.2.3-", "1.2.3+", "x1.2.3", "v1.2.3-a..b"})
        EXPECT_FALSE(parseSemVer(bad)) << bad;
    EXPECT_TRUE(isNewerVersion("v0.0.2", "0.0.1-rc.1"));
    EXPECT_TRUE(isNewerVersion("0.0.1", "0.0.1-rc.1"));   // a release beats its prerelease
    EXPECT_TRUE(isNewerVersion("0.0.1-rc.2", "0.0.1-rc.1"));
    EXPECT_TRUE(isNewerVersion("1.0.0-rc.10", "1.0.0-rc.9")); // numeric identifiers by value
    EXPECT_TRUE(isNewerVersion("1.0.0-beta", "1.0.0-alpha.1"));
    EXPECT_TRUE(isNewerVersion("1.0.0-alpha.1", "1.0.0-alpha")); // more identifiers win
    EXPECT_TRUE(isNewerVersion("1.0.0-a", "1.0.0-1"));         // alphanumeric above numeric
    EXPECT_FALSE(isNewerVersion("1.0.0+other", "1.0.0"));      // build metadata ignored
    EXPECT_FALSE(isNewerVersion("1.0.0.1", "0.0.1"));          // old four-part tags are no versions
    EXPECT_TRUE(isPrereleaseTag("v1.2.0-rc.1"));
    EXPECT_FALSE(isPrereleaseTag("v1.2.0"));
}

TEST(UpdateCheck, FirstNewerReleaseSkippingDraftsAndPrereleases)
{
    const std::vector<ReleaseEntry> feed{
        {"v0.3.0", "Draft", "u0", "", true, false},
        {"v0.2.0-rc.1", "Tagged rc", "u1", "", false, false},
        {"v0.1.5", "Flagged", "u2", "", false, true},
        {"v0.1.2", "Stable", "u3", "notes", false, false},
        {"v0.1.1", "Older", "u4", "", false, false},
    };
    EXPECT_EQ(findNewerRelease(feed, "0.1.0", true)->url, "u3");
    EXPECT_EQ(findNewerRelease(feed, "0.1.0", false)->url, "u1");
    EXPECT_FALSE(findNewerRelease(feed, "0.1.2", true));
    EXPECT_FALSE(findNewerRelease({}, "0.1.0", false));
    EXPECT_TRUE(automaticCheckDue(true, 100, 100));
    EXPECT_FALSE(automaticCheckDue(true, 99, 100));
    EXPECT_FALSE(automaticCheckDue(false, 200, 100));
}
