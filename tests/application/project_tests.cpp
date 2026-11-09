#include "hikari/application/project.h"
#include "hikari/application/recent_files.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

class ProjectFolders : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::random_device rd;
        folder = fs::temp_directory_path() / ("hikari-project-" + std::to_string(rd()));
        fs::create_directories(folder);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
    fs::path touch(const fs::path &relative)
    {
        fs::create_directories((folder / relative).parent_path());
        std::ofstream(folder / relative) << "x";
        return folder / relative;
    }
    fs::path folder;
};

}

TEST_F(ProjectFolders, AFolderOpensAsAFolderWhateverItsName)
{
    for (const char *name : {"show", "episode.ass", "clip.mkv", "a.hksession"}) {
        fs::create_directories(folder / name);
        const auto path = (folder / name).u8string();
        const std::string utf8(path.begin(), path.end());
        EXPECT_EQ(openKindOf(utf8, true), OpenKind::Folder) << name;
        EXPECT_EQ(openKindOf(utf8, false), OpenKind::Folder) << name;
    }
    const auto file = touch("plain.ass").u8string();
    EXPECT_EQ(openKindOf(std::string(file.begin(), file.end()), true), OpenKind::Subtitles);
}

TEST_F(ProjectFolders, TheNearestDotHikariFolderCounts)
{
    fs::create_directories(folder / "show" / ".hikari");
    fs::create_directories(folder / "show" / "01" / ".hikari");
    const auto episode = touch("show/01/ep01.ass");
    const auto extra = touch("show/extras/ncop/ncop.ass");
    const auto outside = touch("other/a.ass");
    EXPECT_EQ(nearestProjectRoot(episode), folder / "show" / "01");
    EXPECT_EQ(nearestProjectRoot(extra), folder / "show");
    EXPECT_EQ(nearestProjectRoot(touch("show/top.ass")), folder / "show");
    EXPECT_EQ(nearestProjectRoot(outside), std::nullopt);
}

TEST_F(ProjectFolders, AFileNamedDotHikariIsNoProject)
{
    touch("show/.hikari");
    EXPECT_EQ(nearestProjectRoot(touch("show/ep01.ass")), std::nullopt);
}

TEST(Project, TheNameIsTheFolders)
{
    EXPECT_EQ(projectName(fs::path("/subs/Show S01")), "Show S01");
    EXPECT_EQ(projectName(fs::path("/subs/Show/")), "Show");
    EXPECT_EQ(projectName(fs::path("/")), "/");
}

TEST(Project, ATrustedFolderCoversItsSubfolders)
{
    const std::vector<std::string> trusted{"/subs/show", "/work/"};
    EXPECT_TRUE(folderTrusted("/subs/show", trusted, false));
    EXPECT_TRUE(folderTrusted("/subs/show/01", trusted, false));
    EXPECT_TRUE(folderTrusted("/work/a", trusted, false));
    EXPECT_FALSE(folderTrusted("/subs/show2", trusted, false));
    EXPECT_FALSE(folderTrusted("/subs", trusted, false));
    EXPECT_FALSE(folderTrusted("/subs/show", {}, false));
    EXPECT_TRUE(folderTrusted("C:\\Subs\\Show\\01", {"c:\\subs\\show"}, true));
}

TEST(Project, TheKeyIsStableAndDiffersPerFolder)
{
    EXPECT_EQ(projectKey("/subs/show"), projectKey("/subs/show"));
    EXPECT_NE(projectKey("/subs/show"), projectKey("/subs/show2"));
    EXPECT_EQ(projectKey("/subs/show").size(), 16u);
}

TEST(Project, AnOpenProjectComesToTheFront)
{
    for (const auto request : {WindowRequest::Default, WindowRequest::New, WindowRequest::Reuse})
        for (const bool empty : {true, false})
            EXPECT_EQ(placeFolder(true, empty, request), FolderPlace::Focus);
}

TEST(Project, AnEmptyWindowTakesTheFolderUnlessANewOneIsAskedFor)
{
    EXPECT_EQ(placeFolder(false, true, WindowRequest::Default), FolderPlace::Current);
    EXPECT_EQ(placeFolder(false, false, WindowRequest::Default), FolderPlace::NewWindow);
    EXPECT_EQ(placeFolder(false, true, WindowRequest::New), FolderPlace::NewWindow);
    EXPECT_EQ(placeFolder(false, false, WindowRequest::Reuse), FolderPlace::Current);
    EXPECT_EQ(placeFolder(false, true, WindowRequest::Reuse), FolderPlace::Current);
}
