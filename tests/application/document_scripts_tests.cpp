#include "hikari/application/document_scripts.h"

#include <gtest/gtest.h>

#include <set>

using namespace hikari::application;

namespace {

std::function<bool(const std::string &)> existing(std::set<std::string> files)
{
    return [files = std::move(files)](const std::string &p) { return files.contains(p); };
}

}

TEST(DocumentScripts, ValueSplitsAtEachLegacyDelimiter)
{
    EXPECT_EQ(scriptInfoScriptPaths("|/a/one.lua| b.lua"), (std::vector<std::string>{"", "/a/one.lua", "b.lua"}));
    EXPECT_EQ(scriptInfoScriptPaths("  c.lua|"), (std::vector<std::string>{"c.lua", ""}));
    EXPECT_EQ(scriptInfoScriptPaths("~/subs/x.lua$y.moon"), (std::vector<std::string>{"", "/subs/x.lua", "y.moon"}));
    EXPECT_EQ(scriptInfoScriptPaths("d.lua "), (std::vector<std::string>{"d.lua "}));
}

TEST(DocumentScripts, LoadScriptAppendsWithABar)
{
    EXPECT_EQ(scriptInfoWithScript("", "/s/a.lua"), "|/s/a.lua");
    EXPECT_EQ(scriptInfoWithScript("|/s/a.lua", "rel.lua"), "|/s/a.lua|rel.lua");
}

TEST(DocumentScripts, ExistingFilesAreAddedOnce)
{
    DocumentScripts scripts;
    std::vector<std::string> added;
    EXPECT_FALSE(scripts.readScriptInfo("", existing({})));
    ASSERT_TRUE(scripts.readScriptInfo("|/abs/one.lua|missing.lua|rel.lua|/abs/one.lua",
                                       existing({"/abs/one.lua", "rel.lua"}), &added));
    EXPECT_EQ(added, (std::vector<std::string>{"/abs/one.lua", "rel.lua"}));
    EXPECT_EQ(scripts.scripts(), added);
    EXPECT_FALSE(scripts.add("rel.lua"));
    EXPECT_TRUE(scripts.add("loaded.lua"));
    EXPECT_EQ(scripts.scripts().back(), "loaded.lua");
}

TEST(DocumentScripts, TheLastValueReadIsNotReadAgain)
{
    DocumentScripts scripts;
    const auto files = existing({"a.lua", "b.lua"});
    ASSERT_TRUE(scripts.readScriptInfo("a.lua", files));
    EXPECT_FALSE(scripts.readScriptInfo("a.lua", files));
    std::vector<std::string> added;
    ASSERT_TRUE(scripts.readScriptInfo("b.lua|a.lua", files, &added));
    EXPECT_EQ(added, std::vector<std::string>{"b.lua"});
    EXPECT_EQ(scripts.scripts(), (std::vector<std::string>{"a.lua", "b.lua"}));
    scripts.remove("a.lua");
    scripts.remove("b.lua");
    ASSERT_TRUE(scripts.readScriptInfo("b.lua|a.lua", files, &added));
    EXPECT_EQ(scripts.scripts(), (std::vector<std::string>{"b.lua", "a.lua"}));
    DocumentScripts spaced;
    ASSERT_TRUE(spaced.readScriptInfo(" a.lua", files));
    EXPECT_TRUE(spaced.readScriptInfo(" a.lua", files));
    EXPECT_FALSE(spaced.readScriptInfo("a.lua", files));
}

TEST(DocumentScripts, TildePathsResolveBesideTheSubtitleFile)
{
    EXPECT_EQ(scriptInfoScriptPaths("|~../.hikari/automation/fx.lua|/abs/b.lua|rel.lua", "/show/01"),
              (std::vector<std::string>{"", "", "/show/.hikari/automation/fx.lua", "/abs/b.lua", "rel.lua"}));
    EXPECT_EQ(scriptInfoScriptPaths("~/subs/x.lua", "/show/01"), (std::vector<std::string>{"", "/subs/x.lua"}));
    DocumentScripts scripts;
    std::vector<std::string> added;
    ASSERT_TRUE(scripts.readScriptInfo("|~../.hikari/automation/fx.lua", existing({"/show/.hikari/automation/fx.lua"}),
                                       &added, "/show/01"));
    EXPECT_EQ(added, std::vector<std::string>{"/show/.hikari/automation/fx.lua"});
}

TEST(DocumentScripts, ProjectScriptsAreWrittenRelativeWithATilde)
{
    EXPECT_EQ(scriptInfoPathFor("/show/.hikari/automation/fx.lua", "/show/01", "/show", false),
              "~../.hikari/automation/fx.lua");
    EXPECT_EQ(scriptInfoPathFor("/show/.hikari/automation/sub/fx.lua", "/show", "/show", false),
              "~.hikari/automation/sub/fx.lua");
    EXPECT_EQ(scriptInfoPathFor("/home/me/fx.lua", "/show/01", "/show", false), "/home/me/fx.lua");
    EXPECT_EQ(scriptInfoPathFor("/show/fx.lua", "/show/01", "/show", false), "/show/fx.lua");
    EXPECT_EQ(scriptInfoPathFor("/show/.hikari/automation/fx.lua", "/show/01", "", false),
              "/show/.hikari/automation/fx.lua");
    const std::string written = scriptInfoWithScript("", scriptInfoPathFor("/show/.hikari/automation/fx.lua", "/show/01",
                                                                           "/show", false));
    EXPECT_EQ(scriptInfoScriptPaths(written, "/show/01").back(), "/show/.hikari/automation/fx.lua");
}
