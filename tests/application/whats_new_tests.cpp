#include "hikari/application/update_check.h"

#include <gtest/gtest.h>

using namespace hikari::application;

namespace {

ReleaseEntry release(std::string tag, bool prerelease = false, bool draft = false)
{
    ReleaseEntry r;
    r.tag = std::move(tag);
    r.name = "HikariSub " + r.tag.substr(1);
    r.notes = "Notes " + r.tag;
    r.prerelease = prerelease;
    r.draft = draft;
    return r;
}

std::vector<std::string> tags(const std::vector<ReleaseEntry> &releases)
{
    std::vector<std::string> out;
    for (const auto &r : releases)
        out.push_back(r.tag);
    return out;
}

std::vector<ReleaseEntry> feed()
{
    return {release("v0.1.4", false, true), release("v0.1.3"),         release("v0.1.3-beta.2", true),
            release("v0.1.2"),              release("v0.1.2-rc.1", true), release("v0.1.1"),
            release("v0.0.3"),              release("v0.1.0"),           release("qt-metadata-linux-6.11.2-20261002", true),
            release("v0.0.2-rc.4", true)};
}

}

TEST(WhatsNew, TheReleasesAfterTheVersionRunBeforeUpToTheInstalledOne)
{
    EXPECT_EQ(tags(whatsNewReleases(feed(), "0.1.1", "0.1.3")),
              (std::vector<std::string>{"v0.1.3", "v0.1.3-beta.2", "v0.1.2", "v0.1.2-rc.1"}));
    EXPECT_EQ(tags(whatsNewReleases(feed(), "0.1.0", "0.1.2")),
              (std::vector<std::string>{"v0.1.2", "v0.1.2-rc.1", "v0.1.1"}));
    const auto releases = whatsNewReleases(feed(), "0.1.2", "0.1.3");
    ASSERT_EQ(releases.size(), 2u);
    EXPECT_EQ(releases[0].notes, "Notes v0.1.3");
    EXPECT_EQ(releases[0].name, "HikariSub 0.1.3");
}

TEST(WhatsNew, AFirstStartOrADowngradeShowsTheInstalledVersionAlone)
{
    EXPECT_EQ(tags(whatsNewReleases(feed(), "", "0.1.3")), (std::vector<std::string>{"v0.1.3"}));
    EXPECT_EQ(tags(whatsNewReleases(feed(), "0.1.3", "0.1.3")), (std::vector<std::string>{"v0.1.3"}));
    EXPECT_EQ(tags(whatsNewReleases(feed(), "0.2.0", "0.1.3")), (std::vector<std::string>{"v0.1.3"}));
    EXPECT_EQ(tags(whatsNewReleases(feed(), "not a version", "0.1.3")), (std::vector<std::string>{"v0.1.3"}));
    EXPECT_EQ(tags(whatsNewReleases(feed(), "0.0.2", "0.1.3")), (std::vector<std::string>{"v0.1.3"}));
    EXPECT_EQ(whatsNewReleases(feed(), "0.1.1", "0.1.5").size(), 4u);
    EXPECT_TRUE(whatsNewReleases(feed(), "", "0.1.5").empty());
    EXPECT_TRUE(whatsNewReleases(feed(), "0.1.1", "dev").empty());
}

TEST(WhatsNew, OneEntryPerVersionTheFirstListedWins)
{
    auto releases = feed();
    ReleaseEntry again = release("v0.1.2");
    again.notes = "A second release of the same tag";
    releases.push_back(again);
    const auto found = whatsNewReleases(releases, "0.1.1", "0.1.2");
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].notes, "Notes v0.1.2");
}

TEST(WhatsNew, TheChannelPicksWhatShows)
{
    const auto releases = whatsNewReleases(feed(), "0.1.1", "0.1.3");
    auto shown = whatsNewShown(releases, "0.1.3", true);
    EXPECT_EQ(tags(shown.releases), (std::vector<std::string>{"v0.1.3", "v0.1.2"}));
    EXPECT_EQ(shown.more, 0u);
    shown = whatsNewShown(releases, "0.1.3", false);
    EXPECT_EQ(tags(shown.releases), (std::vector<std::string>{"v0.1.3", "v0.1.3-beta.2", "v0.1.2", "v0.1.2-rc.1"}));
    const auto toBeta = whatsNewReleases(feed(), "0.1.1", "0.1.3-beta.2");
    EXPECT_EQ(tags(whatsNewShown(toBeta, "0.1.3-beta.2", true).releases),
              (std::vector<std::string>{"v0.1.3-beta.2", "v0.1.2", "v0.1.2-rc.1"}));
    std::vector<ReleaseEntry> tagged{release("v0.1.3"), release("v0.1.3-rc.1")};
    EXPECT_EQ(tags(whatsNewShown(tagged, "0.1.3", true).releases), (std::vector<std::string>{"v0.1.3"}));
}

TEST(WhatsNew, AtMostTenVersionsThenHowManyMore)
{
    std::vector<ReleaseEntry> releases;
    for (int patch = 14; patch >= 0; --patch)
        releases.push_back(release("v0.1." + std::to_string(patch)));
    const auto between = whatsNewReleases(releases, "0.1.0", "0.1.14");
    EXPECT_EQ(between.size(), 14u);
    const auto shown = whatsNewShown(between, "0.1.14", true);
    ASSERT_EQ(shown.releases.size(), kMaxWhatsNew);
    EXPECT_EQ(shown.releases.front().tag, "v0.1.14");
    EXPECT_EQ(shown.releases.back().tag, "v0.1.5");
    EXPECT_EQ(shown.more, 4u);
}

TEST(WhatsNew, TheVersionRunBeforeIsKeptUntilTheNextUpdate)
{
    EXPECT_EQ(whatsNewSince("", "", "0.1.1"), "");
    EXPECT_EQ(whatsNewSince("0.1.1", "", "0.1.3"), "0.1.1");
    EXPECT_EQ(whatsNewSince("0.1.1", "0.1.0", "0.1.3"), "0.1.1");
    EXPECT_EQ(whatsNewSince("0.1.3", "0.1.1", "0.1.3"), "0.1.1");
    EXPECT_EQ(whatsNewSince("0.2.0", "0.1.1", "0.1.3"), "");
    EXPECT_EQ(whatsNewSince("0.1.3", "0.2.0", "0.1.3"), "");
    EXPECT_EQ(whatsNewSince("0.0.2", "", "0.1.3"), "");
    EXPECT_EQ(whatsNewSince("not a version", "", "0.1.3"), "");
}
