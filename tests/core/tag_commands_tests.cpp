// Legacy Bold/Italic/Underline/Strikeout commands (V2-E-grammar). Expected
// results were traced by hand through TagFindReplace::FindTag/PutTagInText at
// 20d647c4; legacy capture run 37024532554 confirms the four Ctrl+B cases.

#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

using namespace hikari::core;
using namespace hikari::core::legacy;

namespace {

std::string s8(const std::u16string &s)
{
    const auto u = toUtf8(s);
    return std::string(u.begin(), u.end());
}

EditorText run(std::u16string text, long from, long to, char16_t tag, bool style = false)
{
    return toggleTag(EditorText{std::move(text), from, to}, tag, style);
}

} // namespace

TEST(TagCommands, SelectionGetsTheTagAndItsRestoration)
{
    const auto r = run(u"abc", 0, 3, u'b');
    EXPECT_EQ(s8(r.text), "{\\b1}abc{\\b0}");
    EXPECT_EQ(r.selectionStart, 5); // the selection still covers "abc"
    EXPECT_EQ(r.selectionEnd, 8);
}

TEST(TagCommands, StyleValueDecidesTheDirection)
{
    // A bold Style: Bold switches it off, and the selection restores it.
    EXPECT_EQ(s8(run(u"abc", 0, 3, u'b', true).text), "{\\b0}abc{\\b1}");
}

TEST(TagCommands, TheTagInEffectAtTheCaretIsSwitched)
{
    const auto r = run(u"{\\b1}abc", 8, 8, u'b');
    EXPECT_EQ(s8(r.text), "{\\b1}abc{\\b0}");
    EXPECT_EQ(r.selectionStart, 13);
}

TEST(TagCommands, InsideABlockTheTagGoesBeforeTheOtherTags)
{
    const auto r = run(u"{\\i1}abc", 4, 4, u'b');
    EXPECT_EQ(s8(r.text), "{\\b1\\i1}abc");
}

TEST(TagCommands, AnExistingValueIsReplacedInPlace)
{
    const auto r = run(u"{\\b1}abc", 2, 2, u'b');
    EXPECT_EQ(s8(r.text), "{\\b0}abc");
}

TEST(TagCommands, OtherTagLettersAndEmptyText)
{
    EXPECT_EQ(s8(run(u"x", 0, 1, u'i').text), "{\\i1}x{\\i0}");
    EXPECT_EQ(s8(run(u"x", 0, 1, u's').text), "{\\s1}x{\\s0}");
    EXPECT_EQ(s8(run(u"", 0, 0, u'u').text), "{\\u1}");
}

TEST(TagCommands, FindBrackets)
{
    EXPECT_EQ(findBrackets(u"{\\i1}abc", 4), std::make_pair(0L, 4L));
    EXPECT_EQ(findBrackets(u"abc", 1), std::make_pair(-1L, -1L));
    EXPECT_EQ(findBrackets(u"a{\\b1", 3), std::make_pair(1L, 4L)); // unclosed: to the end
}

TEST(TagCommands, MatchTheLegacyCapture)
{
    // tests/fixtures/legacy-observations/run-37024532554: the old app's
    // Lines after the key script, with the same caret and selection.
    EXPECT_EQ(s8(run(u"abc", 0, 3, u'b').text), "{\\b1}abc{\\b0}");    // End, Shift+Home
    EXPECT_EQ(s8(run(u"{\\b1}abc", 8, 8, u'b').text), "{\\b1}abc{\\b0}"); // End
    EXPECT_EQ(s8(run(u"{\\i1}abc", 4, 4, u'b').text), "{\\b1\\i1}abc");    // Home, Right x4
    EXPECT_EQ(s8(run(u"{\\b1}abc", 2, 2, u'b').text), "{\\b0}abc");         // Home, Right x2
}
