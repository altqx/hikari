#include "hikari/application/setting_link.h"

#include <gtest/gtest.h>

using hikari::application::parseSettingLink;

TEST(SettingLink, NamesASettingAndATab)
{
    const auto link = parseSettingLink("hikarisub://settings/grid.autoHideColumns");
    ASSERT_TRUE(link);
    EXPECT_EQ(link->setting, "grid.autoHideColumns");
    EXPECT_FALSE(link->project);
    const auto project = parseSettingLink("HikariSub://Settings/editor.spellchecker?scope=project");
    ASSERT_TRUE(project);
    EXPECT_EQ(project->setting, "editor.spellchecker");
    EXPECT_TRUE(project->project);
    EXPECT_EQ(parseSettingLink("hikarisub://settings/plugin.line-tools.width")->setting, "plugin.line-tools.width");
}

TEST(SettingLink, WithoutAnIdOpensTheWindowAlone)
{
    EXPECT_EQ(parseSettingLink("hikarisub://settings")->setting, "");
    EXPECT_EQ(parseSettingLink("hikarisub://settings/")->setting, "");
    EXPECT_FALSE(parseSettingLink("hikarisub://settings?scope=yours")->project);
}

TEST(SettingLink, RefusesOtherLinksAndOddIds)
{
    EXPECT_FALSE(parseSettingLink("hikarisub://plugin/install?id=x"));
    EXPECT_FALSE(parseSettingLink("hikarisub://settingsx/grid.a"));
    EXPECT_FALSE(parseSettingLink("hikarisub://settings/grid a"));
    EXPECT_FALSE(parseSettingLink("hikarisub://settings/a/b"));
    EXPECT_FALSE(parseSettingLink("hikarisub://settings/" + std::string(201, 'a')));
}

TEST(SettingLink, IgnoresOtherParameters)
{
    const auto link = parseSettingLink("hikarisub://settings/grid.a?page=video&scope=project&x");
    ASSERT_TRUE(link);
    EXPECT_EQ(link->setting, "grid.a");
    EXPECT_TRUE(link->project);
    EXPECT_FALSE(parseSettingLink("hikarisub://settings/grid.a?scope=other")->project);
}
