#include "hikari/application/explorer.h"

#include <gtest/gtest.h>

#include <ranges>

using namespace hikari::application::explorer;

namespace {

Filter defaults()
{
    return {{std::string(kDefaultExcludes[0]), std::string(kDefaultExcludes[1])}, false};
}

std::vector<std::string> namesOf(const std::vector<std::string> &names, const Group &group)
{
    std::vector<std::string> out{names[group.lead]};
    for (const auto i : group.members)
        out.push_back(names[i]);
    return out;
}

}

TEST(Explorer, ClassifiesTheFileTypesHikariSubUses)
{
    EXPECT_EQ(classify("ep01.ass"), FileKind::Subtitles);
    EXPECT_EQ(classify("EP01.SRT"), FileKind::Subtitles);
    EXPECT_EQ(classify("notes.txt"), FileKind::Subtitles);
    EXPECT_EQ(classify("ep01.mkv"), FileKind::Video);
    EXPECT_EQ(classify("ep01.MOV"), FileKind::Video);
    EXPECT_EQ(classify("ep01.flac"), FileKind::Audio);
    EXPECT_EQ(classify("ep01_keyframes.txt"), FileKind::Keyframes);
    EXPECT_EQ(classify("x264.stats"), FileKind::Keyframes);
    EXPECT_EQ(classify("ep01_timecodes.txt"), FileKind::Timecodes);
    EXPECT_EQ(classify("ep01_tc.txt"), FileKind::Timecodes);
    EXPECT_EQ(classify("Chapters.xml"), FileKind::Chapters);
    EXPECT_EQ(classify("Font.OTF"), FileKind::Fonts);
    EXPECT_EQ(classify("readme.md"), FileKind::Other);
    EXPECT_EQ(classify("ep01.lwi"), FileKind::Other);
    EXPECT_EQ(classify("Makefile"), FileKind::Other);
    EXPECT_TRUE(opensAsMedia(FileKind::Audio));
    EXPECT_TRUE(opensAsMedia(FileKind::Keyframes));
    EXPECT_FALSE(opensAsMedia(FileKind::Subtitles));
    EXPECT_FALSE(opensAsMedia(FileKind::Fonts));
}

TEST(Explorer, NaturalOrderReadsNumbersAndIgnoresCase)
{
    EXPECT_TRUE(naturalLess("ep2.ass", "ep10.ass"));
    EXPECT_FALSE(naturalLess("ep10.ass", "ep2.ass"));
    EXPECT_TRUE(naturalLess("ep01.ass", "EP01.mkv"));
    EXPECT_TRUE(naturalLess("Ep01.ass", "ep02.ass"));
    EXPECT_TRUE(naturalLess("ep01.mkv", "ep01_keyframes.txt"));
    EXPECT_TRUE(naturalLess("ep", "ep01"));
    EXPECT_TRUE(naturalLess("ep1.ass", "ep01.ass") != naturalLess("ep01.ass", "ep1.ass"));
    EXPECT_FALSE(naturalLess("a.ass", "a.ass"));
}

TEST(Explorer, GlobsMatchNamesAndPaths)
{
    EXPECT_TRUE(globMatches("*.lwi", "ep01.mkv.lwi"));
    EXPECT_TRUE(globMatches("*.LWI", "ep01.lwi"));
    EXPECT_FALSE(globMatches("*.lwi", "ep01.lwi.bak"));
    EXPECT_TRUE(globMatches("ep0?.ass", "ep01.ass"));
    EXPECT_FALSE(globMatches("ep0?.ass", "ep010.ass"));
    EXPECT_TRUE(globMatches("ep0[1-3].ass", "ep02.ass"));
    EXPECT_FALSE(globMatches("ep0[!1-3].ass", "ep02.ass"));
    EXPECT_TRUE(globMatches("raws/*", "raws/ep01.mkv"));
    EXPECT_FALSE(globMatches("raws/*", "raws/old/ep01.mkv"));
    EXPECT_TRUE(globMatches("raws/**", "raws/old/ep01.mkv"));
    EXPECT_TRUE(globMatches("**/old", "old"));
    EXPECT_TRUE(globMatches("**/old", "raws/old"));
    EXPECT_TRUE(globMatches("/raws", "raws"));
    EXPECT_FALSE(globMatches("", "raws"));
}

TEST(Explorer, TheDefaultFilterShowsOnlyHikariSubFiles)
{
    const Filter filter = defaults();
    EXPECT_TRUE(fileShown(filter, "ep01.ass", "ep01.ass", false));
    EXPECT_TRUE(fileShown(filter, "fonts/a.ttf", "a.ttf", false));
    EXPECT_FALSE(fileShown(filter, "readme.md", "readme.md", false));
    EXPECT_FALSE(fileShown(filter, "ep01.mkv.lwi", "ep01.mkv.lwi", false));
    EXPECT_FALSE(fileShown(filter, "ep01.mkv.ffindex", "ep01.mkv.ffindex", false));
    EXPECT_FALSE(fileShown(filter, ".ep01.ass", ".ep01.ass", false));
    EXPECT_FALSE(fileShown(filter, "ep01.ass", "ep01.ass", true));
}

TEST(Explorer, AllFilesStillHidesHiddenAndExcludedFiles)
{
    Filter filter = defaults();
    filter.allFiles = true;
    EXPECT_TRUE(fileShown(filter, "readme.md", "readme.md", false));
    EXPECT_FALSE(fileShown(filter, "ep01.lwi", "ep01.lwi", false));
    EXPECT_FALSE(fileShown(filter, ".DS_Store", ".DS_Store", false));
}

TEST(Explorer, ExcludesApplyToFoldersByNameOrPath)
{
    const Filter filter{{"old", "raws/tmp"}, false};
    EXPECT_FALSE(folderShown(filter, "raws/old", "old", false));
    EXPECT_FALSE(folderShown(filter, "raws/tmp", "tmp", false));
    EXPECT_TRUE(folderShown(filter, "tmp", "tmp", false));
    EXPECT_FALSE(folderShown(filter, ".git", ".git", false));
    EXPECT_FALSE(fileShown(filter, "raws/tmp", "tmp", false));
}

TEST(Explorer, GroupsByNameAroundTheSubtitles)
{
    const std::vector<std::string> names{"ep01.ass", "ep01.mkv", "ep01_keyframes.txt", "ep01_notes.ass",
                                         "ep02.ass", "op.ass"};
    const auto groups = groupByName(names);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(namesOf(names, groups[0]), (std::vector<std::string>{"ep01.ass", "ep01.mkv", "ep01_keyframes.txt"}));
}

TEST(Explorer, AVideoLeadsWithoutSubtitlesAndTheShortestStemWins)
{
    const std::vector<std::string> names{"ep01.en.ass", "ep01.mkv", "ep01_tc.txt", "ep02.mkv", "ep02.wav",
                                         "ep03.wav", "ep03.flac"};
    const auto groups = groupByName(names);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(namesOf(names, groups[0]), (std::vector<std::string>{"ep01.en.ass", "ep01.mkv", "ep01_tc.txt"}));
    EXPECT_EQ(namesOf(names, groups[1]), (std::vector<std::string>{"ep02.mkv", "ep02.wav"}));
}

TEST(Explorer, OnlyKeyframesTimecodesAndChaptersJoinByUnderscore)
{
    const std::vector<std::string> names{"ep01.mkv", "ep01_chapters.xml", "ep01_keyframes.txt", "ep01_timecodes.txt",
                                         "ep01_v2.ass", "ep01_v2.mkv", "ep01_v2.wav"};
    const auto groups = groupByName(names);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(namesOf(names, groups[0]),
              (std::vector<std::string>{"ep01.mkv", "ep01_chapters.xml", "ep01_keyframes.txt", "ep01_timecodes.txt"}));
    EXPECT_EQ(namesOf(names, groups[1]), (std::vector<std::string>{"ep01_v2.ass", "ep01_v2.mkv", "ep01_v2.wav"}));
}

TEST(Explorer, CaseMattersForGroupingAsForFindSameNamedFile)
{
    const std::vector<std::string> names{"EP01.ass", "ep01.mkv"};
    EXPECT_TRUE(groupByName(names).empty());
}
