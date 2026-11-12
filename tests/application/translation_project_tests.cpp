#include "hikari/application/translation_project.h"

#include <gtest/gtest.h>

using namespace hikari::application;
namespace fs = std::filesystem;

TEST(HonyaFolder, InAProjectItIsTheRootOrTheGlossarySubfolder)
{
    EXPECT_EQ(honyaFolder(fs::path("/show"), u8"", fs::path("/show/ep01/a.ass"), u8"elsewhere"), fs::path("/show"));
    EXPECT_EQ(honyaFolder(fs::path("/show"), u8" honya/ ", std::nullopt, u8""), fs::path("/show/honya/"));
    EXPECT_EQ(honyaFolder(fs::path("/show"), u8"a/../b", std::nullopt, u8""), fs::path("/show/b"));
}

TEST(HonyaFolder, TheGlossarySettingNeverLeavesTheProject)
{
    EXPECT_EQ(honyaFolder(fs::path("/show"), u8"../other", std::nullopt, u8""), std::nullopt);
    EXPECT_EQ(honyaFolder(fs::path("/show"), u8"/etc", std::nullopt, u8""), std::nullopt);
    EXPECT_EQ(honyaFolder(fs::path("/show"), u8"a/../../x", std::nullopt, u8""), std::nullopt);
}

TEST(HonyaFolder, OutsideAProjectTheScriptInfoPointerNamesItBesideTheFile)
{
    EXPECT_EQ(honyaFolder(std::nullopt, u8"ignored", fs::path("/subs/ep01.ass"), u8"../honya"), fs::path("/honya"));
    EXPECT_EQ(honyaFolder(std::nullopt, u8"", fs::path("/subs/ep01.ass"), u8"."), fs::path("/subs/"));
    EXPECT_EQ(honyaFolder(std::nullopt, u8"", fs::path("/subs/ep01.ass"), u8""), std::nullopt);
    EXPECT_EQ(honyaFolder(std::nullopt, u8"", std::nullopt, u8"honya"), std::nullopt);
}
