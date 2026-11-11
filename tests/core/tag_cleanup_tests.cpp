#include "hikari/core/lua_pattern.h"
#include "hikari/core/tag_cleanup.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <stdexcept>

namespace {

namespace tc = hikari::core::tagcleanup;
namespace lp = hikari::core::luapattern;
using tc::BlockScope;
using tc::TagFamily;

tc::Families families(std::initializer_list<TagFamily> list)
{
    tc::Families out;
    for (const auto f : list)
        out.set(static_cast<std::size_t>(f));
    return out;
}

std::u8string remove(std::u8string_view text, std::initializer_list<TagFamily> list, BlockScope scope = BlockScope::All)
{
    return tc::removeTags(text, families(list), scope);
}

TEST(LuaPattern, FindsAndSubstitutesAsLuaDoes)
{
    const auto m = lp::find("a{\\b1}c", "{(.-)}");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->start, 1u);
    EXPECT_EQ(m->end, 6u);
    EXPECT_EQ(m->captures, lp::Captures{"\\b1"});
    EXPECT_EQ(lp::gsub("hello world", "o", "0").text, "hell0 w0rld");
    EXPECT_EQ(lp::gsub("abc", "", "-").text, "-a-b-c-");
    EXPECT_EQ(lp::gsub("abc", "^a", "x").count, 1);
    EXPECT_EQ(lp::gsub("x(a(b)c)y", "%b()", "").text, "xy");
    EXPECT_EQ(lp::gsub("THE (quick) fox", "%f[%a]%a+", "W").text, "W (W) W");
    EXPECT_EQ(lp::gsub("abc", "()b", "%1").text, "a2c");
    EXPECT_EQ(lp::gsub("aaa", "(a)%1", "<%0>").text, "<aa>a");
    EXPECT_EQ(lp::gsub("a b", "(%w+)", "%1%%").text, "a% b%");
    EXPECT_THROW(lp::find("x", "[a"), std::invalid_argument);
    EXPECT_THROW(lp::find("x", "%"), std::invalid_argument);
}

TEST(TagCleanup, FamiliesNameTheirTags)
{
    EXPECT_EQ(tc::familyOf("c"), TagFamily::Colour);
    EXPECT_EQ(tc::familyOf("3a"), TagFamily::Alpha);
    EXPECT_EQ(tc::familyOf("fr"), TagFamily::Rotation);
    EXPECT_EQ(tc::familyOf("kf"), TagFamily::Karaoke);
    EXPECT_EQ(tc::familyOf("jitter"), TagFamily::Other);
    EXPECT_EQ(static_cast<std::size_t>(TagFamily::Other) + 1, tc::kFamilyCount);
}

TEST(TagCleanup, RemovesAFamilyEverywhereAndDropsEmptiedBlocks)
{
    EXPECT_EQ(remove(u8"{\\c&HFF&\\b1}a{\\3c&H00FF00&}b{\\1c&H0&}c", {TagFamily::Colour}), u8"{\\b1}abc");
    EXPECT_EQ(remove(u8"{\\fscx120\\fs30\\fsp2\\fscy80}a", {TagFamily::Scale}), u8"{\\fs30\\fsp2}a");
    EXPECT_EQ(remove(u8"{\\fad(1,2)}{}a", {TagFamily::Fade}), u8"{}a") << "a block that was empty already stays";
    EXPECT_EQ(remove(u8"{note\\b1}a", {TagFamily::FontStyle}), u8"{note}a") << "the comment stays";
}

TEST(TagCleanup, ScopeChoosesTheBlocks)
{
    const std::u8string_view text = u8"{\\b1\\i1}a{\\b0}b{\\i0}";
    EXPECT_EQ(remove(text, {TagFamily::FontStyle}, BlockScope::Start), u8"a{\\b0}b{\\i0}");
    EXPECT_EQ(remove(text, {TagFamily::FontStyle}, BlockScope::Inline), u8"{\\b1\\i1}ab");
    EXPECT_EQ(remove(u8"a{\\b1}b", {TagFamily::FontStyle}, BlockScope::Start), u8"a{\\b1}b")
        << "a text not starting with a block has no start block";
}

TEST(TagCleanup, RemovesInsideTransforms)
{
    EXPECT_EQ(remove(u8"{\\t(0,100,\\bord2\\blur3)}a", {TagFamily::Border}), u8"{\\t(0,100,\\blur3)}a");
    EXPECT_EQ(remove(u8"{\\b1\\t(0,100,\\bord2)}a", {TagFamily::Border}), u8"{\\b1}a") << "an emptied \\t goes";
    EXPECT_EQ(remove(u8"{\\t(\\bord2)\\bord1}a", {TagFamily::Transform}), u8"{\\bord1}a");
}

TEST(TagCleanup, EveryFamilyStripsWhatStripTagsStrips)
{
    tc::Options options;
    options.families.set();
    options.cleanup.removeComments = true;
    EXPECT_EQ(tc::apply(u8"{\\an8\\pos(1,2)}Hello {\\i1}world{\\i0}{note}{\\jitter(1,2,3,4)}", options), u8"Hello world");
}

TEST(TagCleanup, RemoveCommentsKeepsTagBlocks)
{
    EXPECT_EQ(tc::removeComments(u8"{TS 0:01}{\\b1}a{note}b{}c{\\N}d"), u8"{\\b1}abcd");
}

TEST(TagCleanup, MergeBlocksJoinsAdjacentTagBlocks)
{
    EXPECT_EQ(tc::mergeBlocks(u8"{\\b1}{\\i1}{\\k10}a{\\u1}{note}{\\s1}b"), u8"{\\b1\\i1\\k10}a{\\u1}{note}{\\s1}b");
}

TEST(TagCleanup, DropRedundantKeepsWhatRenders)
{
    EXPECT_EQ(tc::dropRedundant(u8"{\\bord1\\b1\\bord2\\c&H1&\\1c&H2&}a"), u8"{\\b1\\bord2\\1c&H2&}a");
    EXPECT_EQ(tc::dropRedundant(u8"{\\pos(1,2)}a{\\pos(3,4)\\move(1,2,3,4)\\an8}b{\\an2}"), u8"{\\pos(1,2)}a{\\an8}b")
        << "line-wide tags: the first wins";
    EXPECT_EQ(tc::dropRedundant(u8"{\\fs20\\fs+5}a"), u8"{\\fs20\\fs+5}a") << "a relative size builds on the last";
    EXPECT_EQ(tc::dropRedundant(u8"{\\fs20\\fs+5\\fs30}a"), u8"{\\fs30}a");
    EXPECT_EQ(tc::dropRedundant(u8"{\\b1\\r\\b1}a"), u8"{\\b1\\r\\b1}a") << "a reset ends the run";
    EXPECT_EQ(tc::dropRedundant(u8"{\\bord1\\t(\\bord3)\\bord2}a"), u8"{\\t(\\bord3)\\bord2}a");
    EXPECT_EQ(tc::dropRedundant(u8"{\\k10\\k20}a"), u8"{\\k10\\k20}a");
}

TEST(TagCleanup, ApplyRunsTheCleanupAfterTheRemoval)
{
    tc::Options options;
    options.families = families({TagFamily::Blur});
    options.cleanup.cleanTags = true;
    EXPECT_EQ(tc::apply(u8"{\\blur1}{\\b1}a{\\pos(1,2)}", options), u8"{\\pos(1,2)\\b1}a");
}

}
