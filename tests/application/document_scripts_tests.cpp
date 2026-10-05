// S4: the scripts a Document names in its Script Info, read as legacy
// Automation::AddFromSubs and Add do (Automation.cpp:1153-1171, 1273-1307 at
// 20d647c4).

#include "hikari/application/document_scripts.h"

#include <gtest/gtest.h>

#include <set>

using namespace hikari::application;

namespace {

std::function<bool(const std::string &)> existing(std::set<std::string> files)
{
    return [files = std::move(files)](const std::string &p) { return files.contains(p); };
}

} // namespace

// wxStringTokenizer(paths, "|~$", wxTOKEN_RET_EMPTY_ALL): each of the three
// characters ends a token, empty tokens are kept, and leading whitespace goes.
TEST(DocumentScripts, ValueSplitsAtEachLegacyDelimiter)
{
    EXPECT_EQ(scriptInfoScriptPaths("|/a/one.lua| b.lua"), (std::vector<std::string>{"", "/a/one.lua", "b.lua"}));
    EXPECT_EQ(scriptInfoScriptPaths("  c.lua|"), (std::vector<std::string>{"c.lua", ""}));
    // An Aegisub-style "~" (subtitle folder) or "$" prefix is a delimiter too.
    EXPECT_EQ(scriptInfoScriptPaths("~/subs/x.lua$y.moon"), (std::vector<std::string>{"", "/subs/x.lua", "y.moon"}));
    // Trailing whitespace stays part of the path.
    EXPECT_EQ(scriptInfoScriptPaths("d.lua "), (std::vector<std::string>{"d.lua "}));
}

TEST(DocumentScripts, LoadScriptAppendsWithABar)
{
    EXPECT_EQ(scriptInfoWithScript("", "/s/a.lua"), "|/s/a.lua");
    EXPECT_EQ(scriptInfoWithScript("|/s/a.lua", "rel.lua"), "|/s/a.lua|rel.lua");
}

// Relative and absolute paths that exist are added in order; missing ones are
// skipped without a message; the list keeps one entry per path.
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

// Legacy remembers the value it read last (scriptpaths): the same value is
// not read again while scripts are listed; another Document's value adds its
// scripts after the ones already listed, which stay.
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
    // With every script removed, the same value is read again.
    scripts.remove("a.lua");
    scripts.remove("b.lua");
    ASSERT_TRUE(scripts.readScriptInfo("b.lua|a.lua", files, &added));
    EXPECT_EQ(scripts.scripts(), (std::vector<std::string>{"b.lua", "a.lua"}));
    // The value is remembered without its leading whitespace, so a value
    // that has some is read each time (legacy compares before trimming).
    DocumentScripts spaced;
    ASSERT_TRUE(spaced.readScriptInfo(" a.lua", files));
    EXPECT_TRUE(spaced.readScriptInfo(" a.lua", files));
    EXPECT_FALSE(spaced.readScriptInfo("a.lua", files));
}
