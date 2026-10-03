// A1: one-Line command, draft and undo transactions (accepted policy, T43/C07/C43).

#include "hikari/application/edit_session.h"
#include "hikari/core/ass_load.h"

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

constexpr std::string_view kThreeLines = "[Events]\n"
                                         "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                                         "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n"
                                         "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n";

std::u8string textOf(const EditSession &s, core::LineId id)
{
    for (const auto *l : s.document().lines())
        if (l->id == id)
            return l->text;
    return u8"<missing>";
}

struct SessionTest : ::testing::Test {
    EditSession session{load(kThreeLines)};
    core::LineId l1{1}, l2{2}, l3{3};

    Command setText(core::LineId line, std::u8string text)
    {
        return Command{"Set text", session.revision(), {line},
                       [line, text](core::Document &d) { return d.setLineText(line, text); }};
    }
};

} // namespace

TEST_F(SessionTest, StartsCleanWithNoHistory)
{
    EXPECT_FALSE(session.isDirty());
    EXPECT_FALSE(session.canUndo());
    EXPECT_EQ(session.historySize(), 1u);
}

TEST_F(SessionTest, DraftIsOneAndCommitsOnLeave)
{
    ASSERT_TRUE(session.editDraftText(l1, u8"one!"));
    EXPECT_TRUE(session.isDirty()); // a pending draft is unsaved work
    EXPECT_EQ(textOf(session, l1), u8"one"); // not committed yet
    ASSERT_TRUE(session.editDraftText(l2, u8"two!")); // leaving l1 commits it
    EXPECT_EQ(textOf(session, l1), u8"one!");
    EXPECT_EQ(session.draftLine(), l2);
    session.navigateTo(l3); // navigating commits too
    EXPECT_EQ(textOf(session, l2), u8"two!");
    EXPECT_FALSE(session.draftLine());
    EXPECT_EQ(session.historySize(), 3u); // two committed drafts, two steps
}

TEST_F(SessionTest, DiscardLeavesContentAndHistory)
{
    session.editDraftText(l1, u8"nope");
    session.discardDraft();
    EXPECT_EQ(textOf(session, l1), u8"one");
    EXPECT_EQ(session.historySize(), 1u);
    EXPECT_FALSE(session.isDirty());
}

TEST_F(SessionTest, UnchangedDraftAddsNoStep)
{
    session.editDraftText(l1, u8"one");
    EXPECT_FALSE(session.commitDraft());
    EXPECT_EQ(session.historySize(), 1u);
}

TEST_F(SessionTest, CommandOverDraftCommitsDraftFirstAsItsOwnStep)
{
    session.editDraftText(l2, u8"typed");
    ASSERT_TRUE(session.run(Command{"Upper", session.revision() + 1, {l2}, [&](core::Document &d) {
        return d.setLineText(l2, u8"TYPED");
    }}));
    EXPECT_EQ(textOf(session, l2), u8"TYPED");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(textOf(session, l2), u8"typed"); // the draft step remains
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(textOf(session, l2), u8"two");
}

TEST_F(SessionTest, RejectedCommandChangesNothing)
{
    session.setSelection({l1, {l1, l3}});
    const auto before = session.document().lines().size();
    const auto content = session.contentId();
    const auto history = session.historySize();
    const bool dirty = session.isDirty();
    auto bad = Command{"Reject", session.revision(), {l1}, [](core::Document &d) {
        d.setLineText(core::LineId{1}, u8"half-applied");
        return false; // validation failure after touching the working copy
    }};
    EXPECT_EQ(session.run(bad).error(), CommandRefusal::Invalid);
    EXPECT_EQ(textOf(session, l1), u8"one");
    EXPECT_EQ(session.document().lines().size(), before);
    EXPECT_EQ(session.contentId(), content);
    EXPECT_EQ(session.historySize(), history);
    EXPECT_EQ(session.isDirty(), dirty);
    EXPECT_EQ(session.selection(), (Selection{l1, {l1, l3}}));
}

TEST_F(SessionTest, StaleAndMissingTargetsAreRejected)
{
    auto stale = setText(l1, u8"x");
    stale.expectedRevision += 1;
    EXPECT_EQ(session.run(stale).error(), CommandRefusal::StaleRevision);
    EXPECT_EQ(session.run(setText(core::LineId{99}, u8"x")).error(), CommandRefusal::UnknownLine);
    EXPECT_EQ(session.historySize(), 1u);
}

TEST_F(SessionTest, UndoRestoresContentAndSelection)
{
    session.setSelection({l2, {l2}});
    ASSERT_TRUE(session.run(setText(l2, u8"changed")));
    session.setSelection({l3, {l3}});
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(textOf(session, l2), u8"two");
    EXPECT_EQ(session.selection(), (Selection{l2, {l2}}));
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(textOf(session, l2), u8"changed");
}

TEST_F(SessionTest, UndoWithPendingDraftCommitsItSoRedoRestoresIt)
{
    session.editDraftText(l1, u8"draft");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(textOf(session, l1), u8"one");
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(textOf(session, l1), u8"draft");
}

TEST_F(SessionTest, SaveCommitsTheDraftAndUndoToSavePointIsClean)
{
    session.editDraftText(l1, u8"saved text");
    const auto snapshot = session.prepareSave();
    EXPECT_FALSE(session.draftLine());
    EXPECT_EQ(snapshot->document.lines()[0]->text, u8"saved text");
    session.markSaved(snapshot->content); // the write was reported Written
    EXPECT_FALSE(session.isDirty());
    ASSERT_TRUE(session.run(setText(l2, u8"after save")));
    EXPECT_TRUE(session.isDirty());
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.isDirty()); // back at the exact saved content (C43)
}

TEST_F(SessionTest, EditsDuringAnInFlightWriteStayDirty)
{
    const auto snapshot = session.prepareSave();
    ASSERT_TRUE(session.run(setText(l3, u8"typed while writing")));
    session.markSaved(snapshot->content); // the older snapshot finished writing
    EXPECT_TRUE(session.isDirty());
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.isDirty());
}

TEST_F(SessionTest, BranchingAwayFromTheSavePointStaysDirty)
{
    ASSERT_TRUE(session.run(setText(l1, u8"a")));
    session.markSaved(session.prepareSave()->content);
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.run(setText(l1, u8"a"))); // same bytes, different commit
    EXPECT_TRUE(session.isDirty());
    EXPECT_FALSE(session.canRedo());
}

TEST_F(SessionTest, HistoryKeepsFiveHundredStepsAndTheSavedIdentity)
{
    session.markSaved(session.prepareSave()->content);
    for (int i = 0; i < 600; ++i)
        ASSERT_TRUE(session.run(setText(l1, std::u8string(u8"v") + static_cast<char8_t>(u8'a' + i % 26) +
                                            static_cast<char8_t>(u8'a' + i / 26))));
    EXPECT_EQ(session.historySize(), EditSession::kHistoryCapacity);
    int undone = 0;
    while (session.undo())
        ++undone;
    EXPECT_EQ(undone, 499);
    EXPECT_TRUE(session.isDirty()); // the saved state was pruned; nothing reaches it
}

TEST(EditSessionProtected, ReferenceRefusesEdits)
{
    EditSession reference(load(kThreeLines), /*protectedReference=*/true);
    EXPECT_FALSE(reference.editDraftText(core::LineId{1}, u8"x"));
    EXPECT_EQ(reference.run(Command{"x", 0, {core::LineId{1}}, [](core::Document &) { return true; }}).error(),
              CommandRefusal::Protected);
}

// E63-invalid-commit: blocked by default, legacy behaviour as a preference.

TEST_F(SessionTest, FieldDraftsCommitTogetherAsOneStep)
{
    ASSERT_TRUE(session.editDraftText(l1, u8"one!"));
    ASSERT_TRUE(session.editDraft(l1, DraftChange{.end = core::DocumentTime(2'500'000), .marginLeft = 12}));
    EXPECT_EQ(session.draftRecord()->text, u8"one!");
    ASSERT_TRUE(session.navigateTo(l2));
    EXPECT_EQ(session.historySize(), 2u);
    const auto *line = session.document().lines()[0];
    EXPECT_EQ(line->end.value, core::DocumentTime(2'500'000));
    EXPECT_EQ(line->marginLeft.value, 12);
    EXPECT_TRUE(line->edited);
}

TEST_F(SessionTest, InvalidDraftsAreBlockedByDefault)
{
    ASSERT_TRUE(session.editDraft(l1, DraftChange{.end = core::DocumentTime(500'000)})); // before Start
    EXPECT_EQ(session.draftProblem(), DraftProblem::EndBeforeStart);
    EXPECT_FALSE(session.commitDraft());
    EXPECT_FALSE(session.navigateTo(l2));
    EXPECT_FALSE(session.editDraftText(l2, u8"x"));
    EXPECT_EQ(session.selection().active, l1);
    EXPECT_EQ(session.prepareSave().error(), DraftProblem::EndBeforeStart);
    EXPECT_EQ(session.run(setText(l1, u8"cmd")).error(), CommandRefusal::InvalidDraft);
    EXPECT_FALSE(session.undo());
    EXPECT_EQ(session.historySize(), 1u); // nothing committed
    ASSERT_TRUE(session.draftLine());     // the draft is kept for correction
    ASSERT_TRUE(session.editDraft(l1, DraftChange{.end = core::DocumentTime(1'500'000)}));
    EXPECT_FALSE(session.draftProblem());
    EXPECT_TRUE(session.navigateTo(l2));

    ASSERT_TRUE(session.editDraft(l2, DraftChange{.marginVertical = 10'000}));
    EXPECT_EQ(session.draftProblem(), DraftProblem::MarginOutOfRange);
    EXPECT_FALSE(session.commitDraft());
    session.discardDraft(); // Esc
    EXPECT_TRUE(session.navigateTo(l3));
}

TEST_F(SessionTest, LegacyPreferenceCommitsAndCorrectsOnLeave)
{
    session.setInvalidCommitPolicy(InvalidCommitPolicy::Legacy);
    // Committing in place keeps End before Start, as the legacy editor did...
    ASSERT_TRUE(session.editDraft(l1, DraftChange{.end = core::DocumentTime(500'000), .marginRight = -5}));
    ASSERT_TRUE(session.commitDraft());
    EXPECT_EQ(session.document().lines()[0]->end.value, core::DocumentTime(500'000));
    EXPECT_EQ(session.document().lines()[0]->marginRight.value, 0); // NumCtrl clamps
    // ...and leaving the Line sets End to Start (EditBox::SetLine).
    ASSERT_TRUE(session.editDraft(l2, DraftChange{.end = core::DocumentTime(1'000'000)}));
    ASSERT_TRUE(session.navigateTo(l3));
    EXPECT_EQ(session.document().lines()[1]->end.value, core::DocumentTime(3'000'000));
}

// G10: History inspection and jumps (legacy HistoryDialog, GLOBAL_UNDO_TO_LAST_SAVE).
TEST_F(SessionTest, HistoryListsStepsWithTheirActiveLine)
{
    session.setSelection(Selection{l2, {l2}});
    ASSERT_TRUE(session.run(setText(l2, u8"two!")));
    session.setSelection(Selection{l3, {l3}});
    ASSERT_TRUE(session.run(setText(l3, u8"three!")));
    const auto h = session.history();
    ASSERT_EQ(h.size(), 3u);
    EXPECT_EQ(h[0].name, "Open");
    EXPECT_EQ(h[1].name, "Set text");
    EXPECT_EQ(h[1].active, l2);
    EXPECT_EQ(h[1].activeRow, 2u);
    EXPECT_EQ(h[2].activeRow, 3u);
    EXPECT_EQ(session.historyCursor(), 2u);
}

TEST_F(SessionTest, GoToJumpsLikeARunOfUndoOrRedo)
{
    session.setSelection(Selection{l1, {l1}});
    ASSERT_TRUE(session.run(setText(l1, u8"one!")));
    session.setSelection(Selection{l2, {l2}});
    ASSERT_TRUE(session.run(setText(l2, u8"two!")));
    ASSERT_TRUE(session.goTo(0));
    EXPECT_EQ(textOf(session, l1), u8"one");
    EXPECT_EQ(textOf(session, l2), u8"two");
    EXPECT_EQ(session.selection().active, l1); // recorded with step 1, as Undo restores it
    EXPECT_TRUE(session.canRedo());
    ASSERT_TRUE(session.goTo(2));
    EXPECT_EQ(textOf(session, l2), u8"two!");
    EXPECT_EQ(session.selection().active, l2);
    EXPECT_FALSE(session.goTo(3));
    session.setReadOnly(true);
    EXPECT_FALSE(session.goTo(0));
}

TEST_F(SessionTest, GoToCommitsAPendingDraftFirst)
{
    ASSERT_TRUE(session.run(setText(l1, u8"one!")));
    session.setSelection(Selection{l2, {l2}});
    ASSERT_TRUE(session.editDraftText(l2, u8"typed"));
    ASSERT_TRUE(session.goTo(0));
    EXPECT_FALSE(session.draftLine());
    ASSERT_EQ(session.history().size(), 3u); // the draft became its own step
    EXPECT_EQ(textOf(session, l2), u8"two");
    ASSERT_TRUE(session.goTo(2));
    EXPECT_EQ(textOf(session, l2), u8"typed");
}

TEST_F(SessionTest, TheSavedStepIsKnownWhileItIsInHistory)
{
    EXPECT_EQ(session.savedStep(), 0u); // a freshly opened Document is saved
    ASSERT_TRUE(session.run(setText(l1, u8"one!")));
    auto save = session.prepareSave();
    ASSERT_TRUE(save);
    session.markSaved(save->content);
    ASSERT_TRUE(session.run(setText(l1, u8"one!!")));
    EXPECT_EQ(session.savedStep(), 1u);
    ASSERT_TRUE(session.goTo(*session.savedStep()));
    EXPECT_EQ(textOf(session, l1), u8"one!");
    EXPECT_FALSE(session.isDirty());
    // A new step after going back drops the saved step from history.
    ASSERT_TRUE(session.goTo(0));
    ASSERT_TRUE(session.run(setText(l1, u8"other")));
    EXPECT_FALSE(session.savedStep());
}

TEST_F(SessionTest, HistoryKeepsItsCapacityAndConsistentSteps)
{
    for (int i = 0; i < 520; ++i)
        ASSERT_TRUE(session.run(setText(l1, u8"v" + std::u8string(1, char8_t('a' + i % 26)) +
                                                std::u8string(reinterpret_cast<const char8_t *>(std::to_string(i).c_str())))));
    const auto h = session.history();
    EXPECT_EQ(h.size(), EditSession::kHistoryCapacity);
    EXPECT_EQ(session.historyCursor(), h.size() - 1);
    ASSERT_TRUE(session.goTo(0));
    ASSERT_TRUE(session.goTo(h.size() - 1));
    EXPECT_EQ(textOf(session, l1), u8"v" + std::u8string(1, char8_t('a' + 519 % 26)) + u8"519");
}
