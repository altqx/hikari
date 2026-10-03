// P2: the recent subtitles list (legacy SetRecent/AppendRecent) and the
// extension rules for opened and dropped files (legacy OpenFile/OpenFiles).

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
        for (const char *s : {"a.lua", "a.Moon"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Script) << s;
        for (const char *s : {"a.exe", "a.ZIP", "a.rar", "a.7z"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Refused) << s;
        for (const char *s : {"a.mkv", "a.mp4", "a.wav", "noextension"})
            EXPECT_EQ(openKindOf(s, single), OpenKind::Video) << s;
    }
}

TEST(OpenKind, KeyframesOnlyWhenOpeningOneFile)
{
    for (const char *s : {"a.pass", "a.stats", "a.log", "a_keyframes.txt"})
        EXPECT_EQ(openKindOf(s, true), OpenKind::Keyframes) << s;
    // OpenFiles has no keyframes rule: they open as video, the text file as subtitles.
    EXPECT_EQ(openKindOf("a.pass", false), OpenKind::Video);
    EXPECT_EQ(openKindOf("a_keyframes.txt", false), OpenKind::Subtitles);
    // EndsWith is case-sensitive in legacy.
    EXPECT_EQ(openKindOf("a_KEYFRAMES.txt", true), OpenKind::Subtitles);
}
