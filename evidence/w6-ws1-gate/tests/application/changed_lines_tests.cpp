// E6: the Grid's changed-Line mark (legacy Dialogue State 1 and 2) and
// GLOBAL_REMOVE_TEXT, against legacy at 20d647c4: Dialogue::Copy marks a
// copy changed unless keepstate (SubsDialogue.cpp:1071-1072), SaveFile turns
// every written changed Dialogue saved (SubsGridBase.cpp:391), history steps
// share the Dialogue objects until one is copied again (SubsFile), and
// SubsGrid::DeleteText (SubsGridBase.cpp:928-936).

#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_filtering.h"
#include "hikari/application/grid_groups.h"
#include "hikari/application/select_lines.h"
#include "hikari/application/shift_times.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
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

// Lines that only move are copies in legacy, so they are marked changed.
TEST_F(ChangedLinesTest, SwappingMarksBothLines)
{
    // SwapRowsF copies both and ChangeDialogueState(1) (SubsFile.cpp:1010-1017).
    edit(c, u8"C");
    save();
    select({a, c}, a);
    ASSERT_TRUE(swapLines(session));
    ASSERT_EQ(session.document().lines()[0]->id, c);
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 1})); // c (saved before), b, a
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 2}));
}

namespace {

EditSession sessionOf(std::initializer_list<std::string_view> starts)
{
    std::string text = "[Events]\n";
    char name = 'p';
    for (const auto start : starts)
        text += "Dialogue: 0," + std::string(start) + ",0:00:09.00,Default,,0,0,0,," + std::string(1, name++) + "\n";
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return EditSession{core::loadAss(bytes).document};
}

std::string order(const EditSession &session)
{
    std::string out;
    for (const auto *l : session.document().lines())
        out += std::string(l->text.begin(), l->text.end());
    return out;
}

std::vector<int> statesOf(const EditSession &session)
{
    std::vector<int> out;
    for (const auto *l : session.document().lines())
        out.push_back(session.changeState(*l));
    return out;
}

} // namespace

TEST(ChangedLinesSort, SortingMarksOnlyTheLinesThatLandOnAnotherRow)
{
    // SortAll copies each Line whose row now holds another (SubsFile.cpp:470-481).
    auto all = sessionOf({"0:00:03.00", "0:00:01.00", "0:00:05.00"});
    ASSERT_TRUE(sortLines(all, SortKey::Start, false));
    EXPECT_EQ(order(all), "qpr");
    EXPECT_EQ(statesOf(all), (std::vector<int>{1, 1, 0}));
    // SortSelected the same among the selected rows (SubsFile.cpp:483-501).
    auto some = sessionOf({"0:00:04.00", "0:00:03.00", "0:00:02.00", "0:00:01.00"});
    const auto lines = some.document().lines();
    some.setSelection(Selection{lines[0]->id, {lines[0]->id, lines[2]->id, lines[3]->id}, lines[0]->id, {}});
    ASSERT_TRUE(sortLines(some, SortKey::Start, true));
    EXPECT_EQ(order(some), "sqrp"); // rows 0, 2, 3 sorted; r stays on row 2
    EXPECT_EQ(statesOf(some), (std::vector<int>{1, 0, 0, 1}));
}

TEST_F(ChangedLinesTest, AddingLinesToAGroupMarksOnlyTheLinesItCopies)
{
    // TreeAddLines copies the Lines it moves into the group (SubsGrid.cpp:
    // 1783-1795); the group's own Lines only shift rows.
    select({b, c}, b);
    ASSERT_TRUE(makeGroups(session));
    const core::LineId description = session.document().lines()[1]->id;
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0, 0}));
    select({a}, a);
    ASSERT_TRUE(addLinesToGroup(session, description));
    ASSERT_EQ(session.document().lines()[1]->id, a);
    EXPECT_EQ(states(), (std::vector<int>{0, 1, 0, 0})); // description, a, b, c
}

TEST_F(ChangedLinesTest, SelectLinesMovesMarkTheMovedLines)
{
    // SelectLines' Move to beginning / end: Dial->Copy() of each selected
    // Line, deleted and inserted again (SelectLines.cpp:365-367, 387-399),
    // even where the order does not change.
    SelectLinesSettings move;
    move.find = u8"b";
    move.action = SelectLinesSettings::Action::MoveToBeginning;
    ASSERT_TRUE(selectLines(session, move));
    ASSERT_EQ(session.document().lines()[0]->id, b);
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 0}));
    ASSERT_TRUE(session.undo());
    move.find = u8"a";
    ASSERT_TRUE(selectLines(session, move)); // a stays first
    ASSERT_EQ(session.document().lines()[0]->id, a);
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 0}));
}

TEST_F(ChangedLinesTest, ShiftingTimesMarksTheLinesOnlyWhenTheTimesMove)
{
    // ChangeTimes copies with keepstate (SubsGridBase.cpp:562) and marks a
    // Line changed only for a time or frame shift (570, 584).
    edit(b, u8"B");
    save();
    ShiftTimesSettings none;
    none.tagTimes = true; // a zero shift with tag times: kept
    ASSERT_TRUE(shiftTimes(session, none, {}));
    EXPECT_EQ(states(), (std::vector<int>{0, 2, 0}));
    ShiftTimesSettings later;
    later.timeMs = 100;
    later.whichLines = 1;
    select({a}, a);
    ASSERT_TRUE(shiftTimes(session, later, {}));
    EXPECT_EQ(states(), (std::vector<int>{1, 2, 0}));
}

TEST(ChangedLinesShift, EndCorrectionAndThePostprocessorMarkWhatTheyChange)
{
    std::vector<int> frames;
    for (int i = 0; i < 400; ++i)
        frames.push_back(i * 40);
    const LegacyTimebase timebase(std::move(frames), 25.0);
    ShiftContext context;
    context.timebase = &timebase;
    const auto two = [](std::string_view first, std::string_view second) {
        const std::string text = "[Events]\nDialogue: 0," + std::string(first) + ",Default,,0,0,0,,a\n" +
                                 "Dialogue: 0," + std::string(second) + ",Default,,0,0,0,,b\n";
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return EditSession{core::loadAss(bytes).document};
    };
    // The end cut to the next start marks that Line (SubsGridBase.cpp:642-644).
    auto cut = two("0:00:01.00,0:00:03.50", "0:00:03.00,0:00:04.00");
    ShiftTimesSettings correct;
    correct.correctEndTimes = 1;
    ASSERT_TRUE(shiftTimes(cut, correct, context));
    EXPECT_EQ(statesOf(cut), (std::vector<int>{1, 0}));
    // Continuous times: the previous end (754) and the moved start (765-766).
    auto gap = two("0:00:01.00,0:00:02.00", "0:00:02.30,0:00:03.00");
    ShiftTimesSettings continuous;
    continuous.postprocessor = 16 | 4;
    continuous.thresholdStart = 200;
    continuous.thresholdEnd = 300;
    ASSERT_TRUE(shiftTimes(gap, continuous, context));
    EXPECT_EQ(statesOf(gap), (std::vector<int>{1, 1}));
    // Nothing to snap to: no modification, no mark.
    auto far = two("0:00:01.00,0:00:02.00", "0:00:05.00,0:00:06.00");
    context.keyframes = {200};
    ShiftTimesSettings snap;
    snap.postprocessor = 16 | 8;
    snap.keyframeBeforeStart = snap.keyframeAfterStart = snap.keyframeBeforeEnd = snap.keyframeAfterEnd = 100;
    ASSERT_TRUE(shiftTimes(far, snap, context));
    EXPECT_EQ(statesOf(far), (std::vector<int>{0, 0}));
}
