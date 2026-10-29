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

}

TEST(TagCommands, SelectionGetsTheTagAndItsRestoration)
{
    const auto r = run(u"abc", 0, 3, u'b');
    EXPECT_EQ(s8(r.text), "{\\b1}abc{\\b0}");
    EXPECT_EQ(r.selectionStart, 5);
    EXPECT_EQ(r.selectionEnd, 8);
}

TEST(TagCommands, StyleValueDecidesTheDirection)
{
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
    EXPECT_EQ(findBrackets(u"a{\\b1", 3), std::make_pair(1L, 4L));
}

TEST(TagCommands, MatchTheLegacyCapture)
{
    EXPECT_EQ(s8(run(u"abc", 0, 3, u'b').text), "{\\b1}abc{\\b0}");
    EXPECT_EQ(s8(run(u"{\\b1}abc", 8, 8, u'b').text), "{\\b1}abc{\\b0}");
    EXPECT_EQ(s8(run(u"{\\i1}abc", 4, 4, u'b').text), "{\\b1\\i1}abc");
    EXPECT_EQ(s8(run(u"{\\b1}abc", 2, 2, u'b').text), "{\\b0}abc");
}

TEST(TagCommands, SrtWrapsTheSelectionInHtmlLikeTags)
{
    const auto r = toggleNonAssTag(EditorText{u"abc", 0, 3}, u'b', true);
    EXPECT_EQ(s8(r.text), "<b>abc</b>");
    EXPECT_EQ(r.selectionStart, 10);
}

TEST(TagCommands, SrtReplacesATagAtTheCaretWithAnOpeningOne)
{
    EXPECT_EQ(s8(toggleNonAssTag(EditorText{u"<b>abc</b>", 10, 10}, u'b', true).text), "<b>abc<b>");
    EXPECT_EQ(s8(toggleNonAssTag(EditorText{u"abc", 3, 3}, u'i', true).text), "abc<i>");
}

TEST(TagCommands, MicroDvdPutsTheTagAfterTheLastPipeBeforeTheCaret)
{
    EXPECT_EQ(s8(toggleNonAssTag(EditorText{u"abc|def", 5, 5}, u'b', false).text), "abc{Y:b}|def");
    EXPECT_EQ(s8(toggleNonAssTag(EditorText{u"{y:b}abc", 2, 2}, u'b', false).text), "{Y:b}abc");
    EXPECT_EQ(s8(toggleNonAssTag(EditorText{u"abc", 1, 1}, u'i', false).text), "{Y:i}abc");
    EXPECT_EQ(s8(toggleNonAssTag(EditorText{u"abc", 1, 1}, u'u', false).text), "abc");
}

namespace {

std::optional<std::u16string> arialStyle(std::u16string_view tag)
{
    if (tag == u"fn")
        return u"Arial";
    if (tag == u"b")
        return u"0";
    if (tag == u"bord")
        return u"2";
    return std::nullopt;
}

}

TEST(TagButton, ABoldButtonActsLikeBoldWithoutTags)
{
    for (const auto &[from, to] : {std::pair{0L, 0L}, {1L, 1L}, {0L, 3L}, {1L, 2L}}) {
        const EditorText state{u"abc", from, to};
        const auto button = applyTagButton(state, u"b1", false, arialStyle);
        const auto bold = toggleTag(state, u'b', false);
        EXPECT_EQ(button.text, bold.text) << from << "-" << to;
        EXPECT_EQ(button.selectionStart, bold.selectionStart);
        EXPECT_EQ(button.selectionEnd, bold.selectionEnd);
    }
}

TEST(TagButton, ResetsToTheStyleOrZero)
{
    EXPECT_EQ(applyTagButton({u"abc", 0, 3}, u"\\fnTimes", false, arialStyle).text, u"{\\fnTimes}abc{\\fnArial}");
    EXPECT_EQ(applyTagButton({u"abc", 0, 3}, u"\\bord4", false, arialStyle).text, u"{\\bord4}abc{\\bord2}");
    EXPECT_EQ(applyTagButton({u"abc", 0, 3}, u"\\blur3", false, arialStyle).text, u"{\\blur3}abc{\\blur0}");
}

TEST(TagButton, ResetFollowsTheValueInEffect)
{
    EXPECT_EQ(applyTagButton({u"{\\bord6}abc", 8, 11}, u"\\bord4", false, arialStyle).text,
              u"{\\bord4}abc{\\bord6}");
}

TEST(TagButton, PlainTextReplacesTheSelection)
{
    auto r = insertTagButtonText({u"abc", 1, 1}, u"XY");
    EXPECT_EQ(r.text, u"aXYbc");
    EXPECT_EQ(r.selectionStart, 3);
    r = insertTagButtonText({u"abc", 0, 2}, u"X");
    EXPECT_EQ(r.text, u"Xc");
    EXPECT_EQ(insertTagButtonTextAt(u"ab", 9, u"X"), u"abX");
}
