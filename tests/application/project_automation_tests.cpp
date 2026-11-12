#include "hikari/application/project_automation.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace hikari::application;

namespace {

const std::vector<FolderListing> kBase{{"/app/automation/Autoload", {"a.lua", "fx.lua", "notes.txt"}},
                                       {"/me/automation/autoload", {"fx.lua", "mine.moon"}}};

}

TEST(ProjectAutomation, FoldersSitUnderHikariAutomation)
{
    EXPECT_EQ(projectAutoloadFolder("/show", false), "/show/.hikari/automation/autoload");
    EXPECT_EQ(projectIncludeFolder("C:\\show", true), "C:\\show\\.hikari\\automation\\include");
}

TEST(ProjectAutomation, LoadOrderIsShippedThenUserThenProject)
{
    const auto plan = planAutoload(kBase, {{1, {"/show", true}, {"z.lua", "a.lua"}}}, false);
    EXPECT_EQ(plan.shown.at(1), (std::vector<std::string>{"/show/.hikari/automation/autoload/a.lua",
                                                          "/me/automation/autoload/fx.lua", "/me/automation/autoload/mine.moon",
                                                          "/show/.hikari/automation/autoload/z.lua"}));
    EXPECT_TRUE(plan.locked.empty());
}

TEST(ProjectAutomation, AProjectScriptHidesTheSameNameOnlyInItsWindow)
{
    const auto plan = planAutoload(kBase, {{1, {"/show", true}, {"fx.lua"}}, {2, {}, {}}}, false);
    EXPECT_NE(std::ranges::find(plan.shown.at(1), "/show/.hikari/automation/autoload/fx.lua"), plan.shown.at(1).end());
    EXPECT_EQ(std::ranges::find(plan.shown.at(1), "/me/automation/autoload/fx.lua"), plan.shown.at(1).end());
    EXPECT_NE(std::ranges::find(plan.shown.at(2), "/me/automation/autoload/fx.lua"), plan.shown.at(2).end());
    EXPECT_NE(std::ranges::find(plan.load, "/me/automation/autoload/fx.lua"), plan.load.end());
    EXPECT_NE(std::ranges::find(plan.load, "/show/.hikari/automation/autoload/fx.lua"), plan.load.end());
}

TEST(ProjectAutomation, HidingByNameFoldsCaseOnWindows)
{
    const auto plan = planAutoload({{"C:\\me\\automation\\autoload", {"FX.lua"}}}, {{1, {"C:\\show", true}, {"fx.LUA"}}},
                                   true);
    EXPECT_EQ(plan.shown.at(1), std::vector<std::string>{"C:\\show\\.hikari\\automation\\autoload\\fx.LUA"});
}

TEST(ProjectAutomation, AnUntrustedProjectLoadsNoScriptsAndLocksThem)
{
    const auto plan = planAutoload(kBase, {{1, {"/show", false}, {"fx.lua", "z.lua"}}}, false);
    EXPECT_EQ(plan.locked.at(1), (std::vector<std::string>{"/show/.hikari/automation/autoload/fx.lua",
                                                           "/show/.hikari/automation/autoload/z.lua"}));
    EXPECT_NE(std::ranges::find(plan.shown.at(1), "/me/automation/autoload/fx.lua"), plan.shown.at(1).end());
    for (const auto &script : plan.load)
        EXPECT_EQ(script.find("/show/"), std::string::npos) << script;
}

TEST(ProjectAutomation, OnlyAProjectsOwnScriptsSearchItsInclude)
{
    const std::vector<std::string> base{"/me/automation/include", "/app/automation/Include"};
    const std::vector<std::string> roots{"/show", "/other"};
    EXPECT_EQ(includeFoldersFor("/show/.hikari/automation/autoload/fx.lua", base, roots, false),
              (std::vector<std::string>{"/show/.hikari/automation/include", "/me/automation/include",
                                        "/app/automation/Include"}));
    EXPECT_EQ(includeFoldersFor("/show/01/local.lua", base, roots, false).front(), "/show/.hikari/automation/include");
    EXPECT_EQ(includeFoldersFor("/me/automation/autoload/fx.lua", base, roots, false), base);
    EXPECT_EQ(includeFoldersFor("/showcase/x.lua", base, roots, false), base);
}
