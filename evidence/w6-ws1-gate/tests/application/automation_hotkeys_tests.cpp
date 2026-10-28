// S2: automation hotkeys stored by the legacy name and read from a legacy
// Hotkeys.txt (Hotkeys::SaveHkeys at 20d647c4).

#include "hikari/application/automation_hotkeys.h"

#include <gtest/gtest.h>

using namespace hikari::application;

TEST(AutomationHotkeys, LegacyNamesAreRegistryAliases)
{
    EXPECT_EQ(aliasOfLegacyName("Script foo.lua-2"), std::optional<std::string>("foo.lua:2"));
    EXPECT_EQ(aliasOfLegacyName("Script my-macro.moon-0"), std::optional<std::string>("my-macro.moon:0"));
    EXPECT_FALSE(aliasOfLegacyName("Script foo.lua"));
    EXPECT_FALSE(aliasOfLegacyName("Script foo.lua-x"));
    EXPECT_FALSE(aliasOfLegacyName("GLOBAL_SAVE_SUBS G"));
    EXPECT_EQ(legacyNameOf("foo.lua", 2), "Script foo.lua-2");
}

TEST(AutomationHotkeys, WxAcceleratorsBecomePortableSequences)
{
    EXPECT_EQ(portableKeys("Ctrl-Shift-A"), "Ctrl+Shift+A");
    EXPECT_EQ(portableKeys("F5"), "F5");
    EXPECT_EQ(portableKeys("Alt--"), "Alt+-");
    EXPECT_EQ(portableKeys("Ctrl-Num 0"), "Ctrl+Num 0");
}

TEST(AutomationHotkeys, OnlyScriptLinesAreRead)
{
    const auto bindings = parseLegacyScriptHotkeys("[HikariSub 0.8.0.1200]\r\n"
                                                   "GLOBAL_SAVE_SUBS G=Ctrl-S\r\n"
                                                   "Script blur.lua-0=Ctrl-Shift-B\r\n"
                                                   "Script broken=Ctrl-X\r\n"
                                                   "Script karaoke.moon-3=F9\r\n");
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(bindings[0].legacyName, "Script blur.lua-0");
    EXPECT_EQ(bindings[0].keys, "Ctrl+Shift+B");
    EXPECT_TRUE(bindings[0].macroName.empty()); // imported: no recorded registration
    EXPECT_EQ(bindings[1].legacyName, "Script karaoke.moon-3");
    EXPECT_EQ(bindings[1].keys, "F9");
}
