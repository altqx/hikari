#include "hikari/application/ai_review.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/style.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kScript = "[Script Info]\n"
                                     "Title: Ep01\n"
                                     "\n"
                                     "[V4+ Styles]\n"
                                     "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                                     "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                                     "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                                     "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,"
                                     "100,0,0,1,2,2,2,10,10,10,1\n"
                                     "\n"
                                     "[Events]\n"
                                     "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,Mio,0,0,0,,one\n"
                                     "Dialogue: 0,0:00:03.00,0:00:04.00,Default,Haru,0,0,0,,two\n"
                                     "Dialogue: 0,0:00:05.00,0:00:06.00,Default,Mio,0,0,0,,three\n"
                                     "Dialogue: 0,0:00:07.00,0:00:08.00,Default,Haru,0,0,0,,four\n";

const DocumentId kDoc{1}, kOther{2};

const core::LineRecord *line(const EditSession &s, core::LineId id)
{
    for (const auto *l : s.document().lines())
        if (l->id == id)
            return l;
    return nullptr;
}

std::vector<std::u8string> texts(const EditSession &s)
{
    std::vector<std::u8string> out;
    for (const auto *l : s.document().lines())
        out.push_back(l->text);
    return out;
}

struct ReviewTest : ::testing::Test {
    EditSession session{load(kScript)};
    EditSession other{load(kScript)};
    AiReview review;
    core::LineId l1{1}, l2{2}, l3{3}, l4{4};

    // As document.apply does for a reviewed caller.
    void ai(EditSession &s, DocumentId doc, std::string name, const std::function<bool(core::Document &)> &apply,
            const std::string &caller = "ai")
    {
        const core::Document before = s.document();
        const ContentId previous = s.contentId();
        Command command{name, s.revision(), {}, apply};
        ASSERT_TRUE(s.run(command).has_value());
        review.record(doc, caller, caller == "ai" ? "AI" : "Claude Code", std::move(name), before, previous, s);
    }
    void ai(std::string name, const std::function<bool(core::Document &)> &apply)
    {
        ai(session, kDoc, std::move(name), apply);
    }
    void user(core::LineId id, std::u8string text)
    {
        Command command{"Edit Line", session.revision(), {id},
                        [&](core::Document &d) { return d.setLineText(id, text); }};
        ASSERT_TRUE(session.run(command).has_value());
    }
    AiReview::Sessions sessions()
    {
        return [this](DocumentId id) { return id == kDoc ? &session : id == kOther ? &other : nullptr; };
    }
};

}

TEST_F(ReviewTest, AnAppliedChangeWaitsPerLineAndUndoRestoresTheExactLine)
{
    const core::LineRecord original = *line(session, l2);
    ai("Shortened 2 Lines", [&](core::Document &d) {
        return d.editLine(l2, [](core::LineRecord &l) {
            l.text = u8"2";
            l.end.value = core::DocumentTime(2'500'000);
        }) && d.setLineText(l3, u8"3");
    });
    ASSERT_EQ(review.changes(kDoc).size(), 1u);
    EXPECT_EQ(review.changes(kDoc)[0]->name, "Shortened 2 Lines");
    EXPECT_EQ(review.pendingLines(kDoc, session.document()), 2u);

    const auto pending = review.pending(kDoc, session.document());
    ASSERT_EQ(pending.size(), 2u);
    ASSERT_EQ(review.undo(kDoc, session, {pending[0].change, pending[0].item}, false), true);
    const core::LineRecord *restored = line(session, l2);
    EXPECT_TRUE(sameLineFields(*restored, original));
    EXPECT_EQ(restored->changeVersion, original.changeVersion);
    EXPECT_EQ(session.history().back().name, "Undo AI change to Line 2");
    EXPECT_EQ(line(session, l3)->text, u8"3");
    EXPECT_EQ(review.pendingLines(kDoc, session.document()), 1u);
}

TEST_F(ReviewTest, KeepClearsTheMarksWithoutAStep)
{
    ai("Edit", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    const std::size_t steps = session.historySize();
    review.keep(kDoc, session.document(), {});
    EXPECT_TRUE(review.pending(kDoc, session.document()).empty());
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(line(session, l1)->text, u8"1");
}

TEST_F(ReviewTest, InsertedDeletedAndMovedLinesComeBackInPlace)
{
    ai("Rearranged", [&](core::Document &d) {
        core::LineRecord added = *line(session, l1);
        added.text = u8"new";
        return d.insertLineAfter(l1, added).has_value() && d.removeLine(l3) && d.moveLine(l4, l2);
    });
    EXPECT_EQ(texts(session), (std::vector<std::u8string>{u8"one", u8"new", u8"four", u8"two"}));
    ASSERT_EQ(review.undo(kDoc, session, {review.changes(kDoc)[0]->id, std::nullopt}, false), true);
    EXPECT_EQ(texts(session), (std::vector<std::u8string>{u8"one", u8"two", u8"three", u8"four"}));
    EXPECT_EQ(session.history().back().name, "Undo AI: Rearranged");
    EXPECT_TRUE(review.pending(kDoc, session.document()).empty());
}

TEST_F(ReviewTest, StyleAndScriptInfoChangesAreItemsToo)
{
    ai("Signs", [&](core::Document &d) {
        auto styles = core::decodeStyles(d);
        styles[0].fontsize = u8"54";
        return d.editStyle(0, core::legacy::styleRawFields(styles[0])) && d.setScriptInfo(u8"Title", u8"Episode 1");
    });
    const auto pending = review.pending(kDoc, session.document());
    ASSERT_EQ(pending.size(), 2u);
    EXPECT_EQ(review.pendingLines(kDoc, session.document()), 0u);
    ASSERT_EQ(review.undo(kDoc, session, {}, false), true);
    EXPECT_EQ(core::decodeStyles(session.document())[0].fontsize, u8"48");
    EXPECT_EQ(session.document().scriptInfo(u8"Title"), u8"Ep01");
    EXPECT_EQ(session.history().back().name, "Undo all AI changes");
}

TEST_F(ReviewTest, ALineEditedSinceIsLeftUnlessForced)
{
    ai("Edit", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    user(l1, u8"1!");
    const auto pending = review.pending(kDoc, session.document());
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_TRUE(pending[0].editedSince);
    EXPECT_EQ(review.undo(kDoc, session, {}, false), false);
    EXPECT_EQ(line(session, l1)->text, u8"1!");
    ASSERT_EQ(review.undo(kDoc, session, {}, true), true);
    EXPECT_EQ(line(session, l1)->text, u8"one");
}

TEST_F(ReviewTest, AnotherAiChangeOnTheSameLineIsNotAnEditSince)
{
    ai("First", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    ai("Second", [&](core::Document &d) { return d.setLineText(l1, u8"uno"); });
    for (const auto &p : review.pending(kDoc, session.document()))
        EXPECT_FALSE(p.editedSince);
    EXPECT_EQ(review.pendingLines(kDoc, session.document()), 1u);
    ASSERT_EQ(review.undo(kDoc, session, {}, false), true);
    EXPECT_EQ(line(session, l1)->text, u8"one");
}

TEST_F(ReviewTest, HistoryUndoHidesTheChangeAndRedoBringsItBack)
{
    ai("Edit", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    ASSERT_TRUE(session.undo());
    EXPECT_TRUE(review.pending(kDoc, session.document()).empty());
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(review.pending(kDoc, session.document()).size(), 1u);
}

TEST_F(ReviewTest, AUserEditBetweenWritesStartsANewTurnAndMarksItsStep)
{
    ai("A", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    ai("B", [&](core::Document &d) { return d.setLineText(l2, u8"2"); });
    user(l3, u8"3");
    ai("C", [&](core::Document &d) { return d.setLineText(l4, u8"4"); });
    const auto changes = review.changes(kDoc);
    ASSERT_EQ(changes.size(), 3u);
    EXPECT_EQ(changes[0]->turn, changes[1]->turn);
    EXPECT_NE(changes[1]->turn, changes[2]->turn);
    EXPECT_EQ(review.turn(changes[2]->turn)->number, 2u);
    EXPECT_EQ(review.turnStartingAt(kDoc, changes[0]->step), changes[0]->turn);
    EXPECT_FALSE(review.turnStartingAt(kDoc, changes[1]->step));
    EXPECT_TRUE(review.isAiStep(kDoc, changes[1]->step));
    EXPECT_FALSE(review.isAiStep(kDoc, session.history()[session.historySize() - 2].content));
}

TEST_F(ReviewTest, RestoringACheckpointUndoesKeptChangesInEveryTab)
{
    const std::uint64_t first = review.beginTurn("ai", "Fix typos");
    ai("Typos", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    const std::uint64_t second = review.beginTurn("ai", "Tighten");
    ai("Tighten", [&](core::Document &d) { return d.setLineText(l2, u8"2"); });
    ai(other, kOther, "Tighten", [&](core::Document &d) { return d.setLineText(l3, u8"3"); });
    review.keep(kDoc, session.document(), {});
    user(l2, u8"2!");

    const auto plan = review.checkpoint(second, sessions());
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].lines, 1u);
    ASSERT_EQ(plan[0].editedSince.size(), 1u);
    EXPECT_EQ(plan[0].editedSince[0], l2);

    ASSERT_EQ(review.restore(second, sessions(), false), 1u);
    EXPECT_EQ(line(session, l2)->text, u8"2!");
    EXPECT_EQ(line(other, l3)->text, u8"three");
    EXPECT_EQ(other.history().back().name, "Restore checkpoint before turn 2");

    ASSERT_EQ(review.restore(first, sessions(), true), 1u);
    EXPECT_EQ(texts(session), (std::vector<std::u8string>{u8"one", u8"two", u8"three", u8"four"}));
}

TEST_F(ReviewTest, AnotherCallerGetsItsOwnTurnAndLabel)
{
    ai("A", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    ai(session, kDoc, "Fixed overlaps", [&](core::Document &d) { return d.setLineText(l2, u8"2"); }, "mcp:claude");
    const auto changes = review.changes(kDoc);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_NE(changes[0]->turn, changes[1]->turn);
    EXPECT_EQ(changes[1]->caller, "Claude Code");
    ASSERT_EQ(review.undo(kDoc, session, {changes[1]->id, std::nullopt}, false), true);
    EXPECT_EQ(session.history().back().name, "Undo Claude Code: Fixed overlaps");
}

TEST_F(ReviewTest, ForgettingADocumentDropsItsChanges)
{
    ai("A", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    ai(other, kOther, "B", [&](core::Document &d) { return d.setLineText(l1, u8"1"); });
    EXPECT_EQ(review.documents(), (std::vector<DocumentId>{kDoc, kOther}));
    review.forget(kDoc);
    EXPECT_TRUE(review.changes(kDoc).empty());
    EXPECT_EQ(review.documents(), std::vector<DocumentId>{kOther});
}

TEST_F(ReviewTest, ALineTargetUndoesEveryChangeOnThatLineInOneStep)
{
    ai("First", [&](core::Document &d) { return d.setLineText(l2, u8"2a") && d.setLineText(l3, u8"3a"); });
    ai("Second", [&](core::Document &d) { return d.setLineText(l2, u8"2b"); });
    const std::size_t steps = session.historySize();
    ASSERT_EQ(review.undo(kDoc, session, {.line = l2}, false), true);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(line(session, l2)->text, u8"two");
    EXPECT_EQ(line(session, l3)->text, u8"3a");
    EXPECT_EQ(session.history().back().name, "Undo AI change to Line 2");

    review.keep(kDoc, session.document(), {.line = l3});
    EXPECT_TRUE(review.pending(kDoc, session.document()).empty());
}
