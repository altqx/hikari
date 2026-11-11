#include "hikari/application/ai_inline.h"

#include <gtest/gtest.h>

#include <string>

using namespace hikari;
using namespace hikari::application::ai_inline;

namespace {

core::LineRecord line(std::u8string text, std::int64_t startMs = 1000, std::int64_t endMs = 3000)
{
    core::LineRecord r;
    r.text = std::move(text);
    r.start.value = core::DocumentTime(startMs * 1000);
    r.end.value = core::DocumentTime(endMs * 1000);
    r.style = u8"Default";
    r.actor = u8"Mio";
    return r;
}

std::string s(std::u8string_view v)
{
    return {reinterpret_cast<const char *>(v.data()), v.size()};
}

}

TEST(AiInline, MaskReplacesEveryBlockWithANumberedMarker)
{
    const Masked m = mask(u8"{\\i1}Smells{\\i0} amazing{\\fad(100,0)}");
    EXPECT_EQ(s(m.text), "⟦1⟧Smells⟦2⟧ amazing⟦3⟧");
    ASSERT_EQ(m.tags.size(), 3u);
    EXPECT_EQ(s(m.tags[2]), "{\\fad(100,0)}");
}

TEST(AiInline, UnmaskNeedsEveryMarkerOnceInOrder)
{
    const Masked m = mask(u8"{\\i1}a{\\i0} b");
    EXPECT_EQ(s(*unmask(u8"⟦1⟧A⟦2⟧ B", m.tags)), "{\\i1}A{\\i0} B");
    EXPECT_FALSE(unmask(u8"⟦1⟧A B", m.tags));
    EXPECT_FALSE(unmask(u8"⟦2⟧A⟦1⟧ B", m.tags));
    EXPECT_FALSE(unmask(u8"⟦1⟧A⟦1⟧⟦2⟧", m.tags));
    EXPECT_FALSE(unmask(u8"⟦1⟧A⟦2⟧⟦3⟧", m.tags));
    EXPECT_FALSE(unmask(u8"⟦1A", m.tags));
}

TEST(AiInline, SkipsCommentsEmptyTagOnlyDrawingsAndKaraoke)
{
    core::LineRecord comment = line(u8"Note");
    comment.comment = true;
    EXPECT_EQ(skipOf(comment), Skip::Comment);
    EXPECT_EQ(skipOf(line(u8"")), Skip::Empty);
    EXPECT_EQ(skipOf(line(u8"  ")), Skip::Empty);
    EXPECT_EQ(skipOf(line(u8"{\\an8\\pos(960,80)}")), Skip::OnlyTags);
    EXPECT_EQ(skipOf(line(u8"{\\p1}m 0 0 l 10 0")), Skip::Drawing);
    EXPECT_EQ(skipOf(line(u8"{\\p0}Text")), Skip::None);
    EXPECT_EQ(skipOf(line(u8"{\\k20}ka{\\k30}ra")), Skip::Karaoke);
    EXPECT_EQ(skipOf(line(u8"{\\kf20}ka")), Skip::Karaoke);
    EXPECT_EQ(skipOf(line(u8"{\\K20}ka")), Skip::Karaoke);
    EXPECT_EQ(skipOf(line(u8"{\\pos(1,2)\\fnKozuka}Sign")), Skip::None);
    EXPECT_EQ(skipOf(line(u8"Plain")), Skip::None);
}

TEST(AiInline, PromptCarriesTheLineItsTimesAndNeighboursOnly)
{
    const Prepared p = prepare({Action::Rewrite, "less formal", {}, 15}, line(u8"{\\i1}Well{\\i0}, I mean it."),
                               {{u8"Before one"}, {u8"After one", u8"After two"}});
    EXPECT_NE(p.user.find("Rewrite the Line: less formal"), std::string::npos);
    EXPECT_NE(p.user.find("Style: Default"), std::string::npos);
    EXPECT_NE(p.user.find("Speaker: Mio"), std::string::npos);
    EXPECT_NE(p.user.find("Time: 0:00:01.00 to 0:00:03.00 (2.00 s)"), std::string::npos);
    EXPECT_NE(p.user.find("Lines before:\nBefore one\n"), std::string::npos);
    EXPECT_NE(p.user.find("Lines after:\nAfter one\nAfter two\n"), std::string::npos);
    EXPECT_NE(p.user.find("<line>⟦1⟧Well⟦2⟧, I mean it.</line>"), std::string::npos);
    EXPECT_EQ(p.user.find("\\i1"), std::string::npos);
    EXPECT_NE(p.system.find("Keep every marker exactly once"), std::string::npos);
}

TEST(AiInline, EachActionAsksItsOwnTask)
{
    const core::LineRecord l = line(u8"Text", 0, 2000);
    EXPECT_NE(prepare({Action::Shorten, {}, {}, 15}, l, {}).user.find("15 characters per second or less: it is on "
                                                                         "screen for 2.00 s, so aim for at most 30"),
              std::string::npos);
    EXPECT_NE(prepare({Action::Spelling}, l, {}).user.find("Fix the spelling, grammar and punctuation"), std::string::npos);
    EXPECT_NE(prepare({Action::Translate, {}, "English"}, l, {}).user.find("Translate the Line into English."),
              std::string::npos);
    EXPECT_NE(prepare({Action::Custom, "Add honorifics"}, l, {}).user.find("Rewrite the Line: Add honorifics"),
              std::string::npos);
}

TEST(AiInline, ASpanSendsTheRestAsContextAndChangesOnlyThePart)
{
    const core::LineRecord l = line(u8"{\\i1}Hello{\\i0} there, friend");
    const std::u8string_view text = l.text;
    const std::size_t at = text.find(u8"there");
    const Prepared p = prepare({Action::Spelling}, l, {}, std::pair{at, at + 5});
    EXPECT_NE(p.user.find("<line>Hello <edit>there</edit>, friend</line>"), std::string::npos);
    EXPECT_NE(p.user.find("the part inside <edit>"), std::string::npos);
    const Answer a = readAnswer(p, "their");
    ASSERT_EQ(a.outcome, Outcome::Changed);
    EXPECT_EQ(s(a.text), "{\\i1}Hello{\\i0} their, friend");
}

TEST(AiInline, ReadsTheAnswerBackIntoTheLine)
{
    const Prepared p = prepare({Action::Rewrite, "x"}, line(u8"{\\i1}Smells{\\i0} amazing though."), {});
    const Answer changed = readAnswer(p, "  ⟦1⟧Smells⟦2⟧ great.\n");
    ASSERT_EQ(changed.outcome, Outcome::Changed);
    EXPECT_EQ(s(changed.text), "{\\i1}Smells{\\i0} great.");
    EXPECT_EQ(readAnswer(p, "Smells great.").outcome, Outcome::DroppedTag);
    EXPECT_EQ(readAnswer(p, "⟦1⟧Smells⟦2⟧ amazing though.").outcome, Outcome::Same);
    EXPECT_EQ(readAnswer(p, "   ").outcome, Outcome::Empty);
    EXPECT_EQ(s(readAnswer(p, "\"⟦1⟧A⟦2⟧ b\"").text), "{\\i1}A{\\i0} b");
    EXPECT_EQ(s(readAnswer(p, "<line>⟦1⟧A⟦2⟧ b</line>").text), "{\\i1}A{\\i0} b");
    EXPECT_EQ(s(readAnswer(p, "```\n⟦1⟧A⟦2⟧ b\n```").text), "{\\i1}A{\\i0} b");
    EXPECT_EQ(s(readAnswer(p, "⟦1⟧A⟦2⟧ b\n  second").text), "{\\i1}A{\\i0} b\\Nsecond");
}

TEST(AiInline, KeepsQuotesTheLineAlreadyHad)
{
    const Prepared p = prepare({Action::Spelling}, line(u8"\"Quoted.\""), {});
    EXPECT_EQ(readAnswer(p, "\"Quoted!\"").outcome, Outcome::Changed);
    EXPECT_EQ(s(readAnswer(p, "\"Quoted!\"").text), "\"Quoted!\"");
}

TEST(AiInline, ActionIdsRoundTrip)
{
    for (const Action a : {Action::Rewrite, Action::Shorten, Action::Spelling, Action::Translate, Action::Custom})
        EXPECT_EQ(actionFromId(actionId(a)), a);
    EXPECT_FALSE(actionFromId("timing"));
}
