// E6: the Grid's changed-Line mark (legacy Dialogue State 1 and 2) and
// GLOBAL_REMOVE_TEXT, against legacy at 20d647c4: Dialogue::Copy marks a
// copy changed unless keepstate (SubsDialogue.cpp:1071-1072), SaveFile turns
// every written changed Dialogue saved (SubsGridBase.cpp:391), history steps
// share the Dialogue objects until one is copied again (SubsFile), and
// SubsGrid::DeleteText (SubsGridBase.cpp:928-936).

#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_filtering.h"
#include "hikari/application/grid_groups.h"
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

struct ChangedLinesTest : ::testing::Test {
    EditSession session{load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,b\n"
                             "Comment: 0,0:00:06.50,0:00:08.00,Default,,0,0,0,,c\n")};
    core::LineId a{1}, b{2}, c{3};
    const core::LineRecord &line(core::LineId id) const
    {
        for (const auto *l : session.document().lines())
            if (l->id == id)
                return *l;
        throw std::out_of_range("line");
    }
    int state(core::LineId id) const { return session.changeState(line(id)); }
    std::vector<int> states() const
    {
        std::vector<int> out;
        for (const auto *l : session.document().lines())
            out.push_back(session.changeState(*l));
        return out;
    }
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
    void edit(core::LineId id, std::u8string text)
    {
        ASSERT_TRUE(session.run(Command{"Edit", session.revision(), {id},
                                        [&](core::Document &d) { return d.setLineText(id, text); }}));
    }
    void save()
    {
        const auto snapshot = session.prepareSave();
        ASSERT_TRUE(snapshot);
        session.markSaved(snapshot->content);
    }
};

} // namespace

TEST_F(ChangedLinesTest, LoadedLinesAreUnchangedAndAnEditMarksOnlyItsLine)
{
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0}));
    edit(b, u8"B");
    EXPECT_EQ(states(), (std::vector<int>{0, 1, 0}));
    // The draft's commit is an edit too (EditBox::Send copies the Line).
    ASSERT_TRUE(session.editDraftText(a, u8"A"));
    EXPECT_EQ(state(a), 0); // a pending draft is not in the Grid
    ASSERT_TRUE(session.commitDraft());
    EXPECT_EQ(states(), (std::vector<int>{1, 1, 0}));
}

TEST_F(ChangedLinesTest, SavingTurnsChangedLinesSavedAndANewEditChangesThemAgain)
{
    edit(b, u8"B");
    save();
    EXPECT_EQ(states(), (std::vector<int>{0, 2, 0}));
    edit(b, u8"BB");
    EXPECT_EQ(state(b), 1);
    edit(c, u8"C");
    EXPECT_EQ(states(), (std::vector<int>{0, 1, 1}));
}

TEST_F(ChangedLinesTest, UndoShowsEachStepsOwnCopiesAndSavedCopiesStaySavedAcrossHistory)
{
    edit(b, u8"B1");  // step 1: b's first copy
    edit(a, u8"A");   // step 2: b's first copy shared
    edit(b, u8"B2");  // step 3: b's second copy
    save();           // the second copy and a's copy are saved
    EXPECT_EQ(states(), (std::vector<int>{2, 2, 0}));
    ASSERT_TRUE(session.undo()); // step 2: b's first copy was never saved
    EXPECT_EQ(states(), (std::vector<int>{2, 1, 0}));
    ASSERT_TRUE(session.undo()); // step 1
    EXPECT_EQ(states(), (std::vector<int>{0, 1, 0}));
    ASSERT_TRUE(session.undo()); // the loaded Document
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0}));
    ASSERT_TRUE(session.redo());
    ASSERT_TRUE(session.redo());
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(states(), (std::vector<int>{2, 2, 0}));
    // Saving an earlier step marks that step's copies saved, wherever they are.
    ASSERT_TRUE(session.undo());
    save();
    EXPECT_EQ(states(), (std::vector<int>{2, 2, 0}));
}

TEST_F(ChangedLinesTest, ASaveMarksTheWrittenSnapshotEvenAfterLaterEdits)
{
    edit(b, u8"B");
    const auto snapshot = session.prepareSave();
    ASSERT_TRUE(snapshot);
    edit(b, u8"BB"); // edited while the write runs
    session.markSaved(snapshot->content);
    EXPECT_EQ(state(b), 1); // the newer copy was not written
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(state(b), 2);
}

TEST_F(ChangedLinesTest, FilteringKeepsTheMarkAndHidingSelectedLinesChangesThem)
{
    // Filter and TurnOffFiltering copy with keepstate (SubsGridFiltering.cpp:
    // 78-95, 212); HideSelections copies without it (146-156).
    FilterSettings comments;
    comments.filterBy = 4; // "Hide comments"
    ASSERT_TRUE(filterLines(session, comments));
    EXPECT_EQ(line(c).visibility, core::LineVisibility::Hidden);
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0}));
    ASSERT_TRUE(turnOffFiltering(session));
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0}));
    select({a}, a);
    ASSERT_TRUE(hideSelectedLines(session));
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 0}));
}

TEST_F(ChangedLinesTest, GroupsKeepTheMarkWhenMadeOrOpenedAndChangeItWhenEdited)
{
    select({a, b}, a);
    ASSERT_TRUE(makeGroups(session));
    // MakeTree: a new description Dialogue (state 0) and keepstate copies.
    const auto lines = session.document().lines();
    ASSERT_EQ(lines.size(), 4u);
    const core::LineId description = lines[0]->id;
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0, 0}));
    ASSERT_TRUE(toggleGroup(session, description)); // OpenCloseTree: in place
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0, 0}));
    ASSERT_TRUE(renameGroup(session, description, u8"name")); // TreeChangeName copies
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 0, 0}));
    ASSERT_TRUE(removeGroup(session, description)); // TreeRemove copies the members
    EXPECT_EQ(states(), (std::vector<int>{1, 1, 0}));
}

TEST_F(ChangedLinesTest, DeletingEveryLineLeavesAnUnchangedDefaultLine)
{
    select({a, b, c}, a);
    ASSERT_TRUE(deleteLines(session));
    ASSERT_EQ(session.document().lines().size(), 1u);
    EXPECT_EQ(states(), (std::vector<int>{0})); // DeleteRows: new Dialogue()
}

TEST_F(ChangedLinesTest, DeleteTextEmptiesTheShownSelectedLinesInOneStep)
{
    ASSERT_TRUE(session.run(Command{"Edit", session.revision(), {b}, [&](core::Document &d) {
        return d.editLine(b, [](core::LineRecord &l) { l.translation = u8"tb"; }, core::ChangeMark::Kept);
    }}));
    select({a, b, c}, a);
    const auto steps = session.historySize();
    // c is hidden: GetSelections skips it.
    const auto shown = [&](core::LineId id) { return id != c; };
    ASSERT_TRUE(deleteText(session, shown));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Deleting text");
    EXPECT_EQ(line(a).text, u8"");
    EXPECT_EQ(line(b).text, u8"");
    EXPECT_EQ(line(b).translation, u8"tb"); // only Text, not TextTl
    EXPECT_EQ(line(c).text, u8"c");
    EXPECT_EQ(states(), (std::vector<int>{1, 1, 0}));
    // Already empty: legacy still copies and records the step.
    ASSERT_TRUE(deleteText(session, shown));
    EXPECT_EQ(session.historySize(), steps + 2);
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(line(a).text, u8"a");
    EXPECT_EQ(line(b).text, u8"b");
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0}));
}

TEST_F(ChangedLinesTest, DeleteTextNeedsAShownSelectedLine)
{
    select({c}, c);
    const auto steps = session.historySize();
    EXPECT_FALSE(deleteText(session, [&](core::LineId id) { return id != c; }));
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(line(c).text, u8"c");
}
