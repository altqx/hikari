#include "hikari/application/recent_files.h"

#include <gtest/gtest.h>

#include <set>

using namespace hikari::application;

TEST(RecentFiles, TheLatestComesFirstWithoutDuplicates)
{
    RecentFiles recent;
    recent.add("a.ass");
    recent.add("b.ass");
    recent.add("a.ass");
    recent.add("");
    EXPECT_EQ(recent.entries(), (std::vector<std::string>{"a.ass", "b.ass"}));
}

TEST(RecentFiles, KeepsTwentyEntries)
{
    RecentFiles recent;
    for (int i = 0; i < 25; ++i)
        recent.add(std::to_string(i) + ".ass");
    ASSERT_EQ(recent.entries().size(), RecentFiles::kCapacity);
    EXPECT_EQ(recent.entries().front(), "24.ass");
    EXPECT_EQ(recent.entries().back(), "5.ass");
}

TEST(RecentFiles, StoredListsKeepTheirOrder)
{
    RecentFiles recent;
    recent.set({"c.ass", "b.ass", "c.ass", "a.ass"});
    EXPECT_EQ(recent.entries(), (std::vector<std::string>{"c.ass", "b.ass", "a.ass"}));
}

TEST(RecentFiles, PruningDropsWhatIsMissing)
{
    RecentFiles recent;
    recent.set({"keep.ass", "gone.ass"});
    EXPECT_TRUE(recent.prune([](const std::string &p) { return p == "gone.ass"; }));
    EXPECT_EQ(recent.entries(), (std::vector<std::string>{"keep.ass"}));
    EXPECT_FALSE(recent.prune([](const std::string &) { return false; }));
}

TEST(RecentFiles, RemembersWhenEachWasLastOpened)
{
    RecentFiles recent;
    recent.add("a.ass", 1000);
    recent.add("b.ass", 2000);
    recent.add("a.ass", 3000);
    EXPECT_EQ(recent.entries(), (std::vector<std::string>{"a.ass", "b.ass"}));
    EXPECT_EQ(recent.opened("a.ass"), std::optional<std::int64_t>(3000));
    EXPECT_EQ(recent.opened("b.ass"), std::optional<std::int64_t>(2000));
    recent.add("b.ass");
    EXPECT_EQ(recent.opened("b.ass"), std::nullopt);
    EXPECT_EQ(recent.opened("c.ass"), std::nullopt);
}

TEST(RecentFiles, OpenedTimesAreStoredBesideTheList)
{
    RecentFiles recent;
    recent.add("/subs/with space.ass", 1700000000000);
    recent.add("legacy.ass");
    recent.add("C:\\subs\\b.ass", 1700000001000);
    const auto records = recent.openedRecords();
    EXPECT_EQ(records, (std::vector<std::string>{"1700000001000 C:\\subs\\b.ass", "1700000000000 /subs/with space.ass"}));

    RecentFiles loaded;
    loaded.set({"C:\\subs\\b.ass", "legacy.ass", "/subs/with space.ass"});
    loaded.setOpenedRecords({"1700000000000 /subs/with space.ass", "garbage", "12x /bad.ass", "5 /not/listed.ass",
                             "1700000001000 C:\\subs\\b.ass"});
    EXPECT_EQ(loaded.opened("/subs/with space.ass"), std::optional<std::int64_t>(1700000000000));
    EXPECT_EQ(loaded.opened("C:\\subs\\b.ass"), std::optional<std::int64_t>(1700000001000));
    EXPECT_EQ(loaded.opened("legacy.ass"), std::nullopt);
    EXPECT_EQ(loaded.opened("/not/listed.ass"), std::nullopt);
    EXPECT_EQ(loaded.openedRecords(), records);
}

TEST(RecentFiles, RemovingAnEntryForgetsItsTime)
{
    RecentFiles recent;
    recent.add("a.ass", 1);
    recent.add("b.ass", 2);
    EXPECT_TRUE(recent.remove("a.ass"));
    EXPECT_FALSE(recent.remove("a.ass"));
    EXPECT_EQ(recent.entries(), (std::vector<std::string>{"b.ass"}));
    EXPECT_EQ(recent.opened("a.ass"), std::nullopt);
    EXPECT_EQ(recent.openedRecords(), (std::vector<std::string>{"2 b.ass"}));
}

TEST(RecentFiles, EntriesPastTheCapacityOrPrunedForgetTheirTimes)
{
    RecentFiles recent;
    for (int i = 0; i < 21; ++i)
        recent.add(std::to_string(i) + ".ass", i);
    EXPECT_EQ(recent.opened("0.ass"), std::nullopt);
    EXPECT_EQ(recent.openedRecords().size(), RecentFiles::kCapacity);
    recent.prune([](const std::string &p) { return p == "20.ass"; });
    EXPECT_EQ(recent.opened("20.ass"), std::nullopt);
    EXPECT_EQ(recent.openedRecords().size(), RecentFiles::kCapacity - 1);
}

TEST(RecentFiles, NetworkPathsAreNotChecked)
{
    const auto exists = [](const std::string &) { return false; };
    const auto remote = [](char drive) { return drive == 'N'; };
    EXPECT_FALSE(isMissingLocalFile("\\\\server\\share\\a.ass", exists, remote));
    EXPECT_FALSE(isMissingLocalFile("N:\\subs\\a.ass", exists, remote));
    EXPECT_TRUE(isMissingLocalFile("C:\\subs\\a.ass", exists, remote));
    EXPECT_TRUE(isMissingLocalFile("/home/a.ass", exists, remote));
    EXPECT_FALSE(isMissingLocalFile("/home/a.ass", [](const std::string &) { return true; }, remote));
}

TEST(OpenKind, FollowsTheLegacyExtensions)
{
    for (const bool single : {true, false}) {
        for (const char *s : {"a.ass", "a.SSA", "a.srt", "a.sub", "a.txt"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Subtitles) << s;
        for (const char *s : {"a.lrc", "a.TTML"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Subtitles) << s;
        for (const char *s : {"a.lua", "a.Moon"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Script) << s;
        for (const char *s : {"a.exe", "a.ZIP", "a.rar", "a.7z"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Refused) << s;
        for (const char *s : {"a.mkv", "a.mp4", "a.wav", "noextension"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Video) << s;
    }
}

TEST(OpenKind, ASessionFileOpensAloneAsASession)
{
    for (const char *s : {"a.hksession", "a.HKSession", "a.kls"}) {
        EXPECT_EQ(openKindOf(s, true), OpenKind::Session) << s;
        EXPECT_EQ(openKindOf(s, false), OpenKind::Refused) << s;
    }
}

TEST(OpenKind, KeyframesOnlyWhenOpeningOneFile)
{
    for (const char *s : {"a.pass", "a.stats", "a.log", "a_keyframes.txt"})
        EXPECT_EQ(openKindOf(s, true), OpenKind::Keyframes) << s;
    EXPECT_EQ(openKindOf("a.pass", false), OpenKind::Video);
    EXPECT_EQ(openKindOf("a_keyframes.txt", false), OpenKind::Subtitles);
    EXPECT_EQ(openKindOf("a_KEYFRAMES.txt", true), OpenKind::Subtitles);
}
