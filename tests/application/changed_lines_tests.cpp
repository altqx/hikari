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

}

TEST_F(ChangedLinesTest, LoadedLinesAreUnchangedAndAnEditMarksOnlyItsLine)
{
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0}));
    edit(b, u8"B");
    EXPECT_EQ(states(), (std::vector<int>{0, 1, 0}));
    ASSERT_TRUE(session.editDraftText(a, u8"A"));
    EXPECT_EQ(state(a), 0);
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
    edit(b, u8"B1");
    edit(a, u8"A");
    edit(b, u8"B2");
    save();
    EXPECT_EQ(states(), (std::vector<int>{2, 2, 0}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(states(), (std::vector<int>{2, 1, 0}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(states(), (std::vector<int>{0, 1, 0}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0}));
    ASSERT_TRUE(session.redo());
    ASSERT_TRUE(session.redo());
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(states(), (std::vector<int>{2, 2, 0}));
    ASSERT_TRUE(session.undo());
    save();
    EXPECT_EQ(states(), (std::vector<int>{2, 2, 0}));
}

TEST_F(ChangedLinesTest, ASaveMarksTheWrittenSnapshotEvenAfterLaterEdits)
{
    edit(b, u8"B");
    const auto snapshot = session.prepareSave();
    ASSERT_TRUE(snapshot);
    edit(b, u8"BB");
    session.markSaved(snapshot->content);
    EXPECT_EQ(state(b), 1);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(state(b), 2);
}

TEST_F(ChangedLinesTest, FilteringKeepsTheMarkAndHidingSelectedLinesChangesThem)
{
    FilterSettings comments;
    comments.filterBy = 4;
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
    const auto lines = session.document().lines();
    ASSERT_EQ(lines.size(), 4u);
    const core::LineId description = lines[0]->id;
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0, 0}));
    ASSERT_TRUE(toggleGroup(session, description));
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0, 0}));
    ASSERT_TRUE(renameGroup(session, description, u8"name"));
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 0, 0}));
    ASSERT_TRUE(removeGroup(session, description));
    EXPECT_EQ(states(), (std::vector<int>{1, 1, 0}));
}

TEST_F(ChangedLinesTest, DeletingEveryLineLeavesAnUnchangedDefaultLine)
{
    select({a, b, c}, a);
    ASSERT_TRUE(deleteLines(session));
    ASSERT_EQ(session.document().lines().size(), 1u);
    EXPECT_EQ(states(), (std::vector<int>{0}));
}

TEST_F(ChangedLinesTest, DeleteTextEmptiesTheShownSelectedLinesInOneStep)
{
    ASSERT_TRUE(session.run(Command{"Edit", session.revision(), {b}, [&](core::Document &d) {
        return d.editLine(b, [](core::LineRecord &l) { l.translation = u8"tb"; }, core::ChangeMark::Kept);
    }}));
    select({a, b, c}, a);
    const auto steps = session.historySize();
    const auto shown = [&](core::LineId id) { return id != c; };
    ASSERT_TRUE(deleteText(session, shown));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Deleting text");
    EXPECT_EQ(line(a).text, u8"");
    EXPECT_EQ(line(b).text, u8"");
    EXPECT_EQ(line(b).translation, u8"tb");
    EXPECT_EQ(line(c).text, u8"c");
    EXPECT_EQ(states(), (std::vector<int>{1, 1, 0}));
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

TEST_F(ChangedLinesTest, SwappingMarksBothLines)
{
    edit(c, u8"C");
    save();
    select({a, c}, a);
    ASSERT_TRUE(swapLines(session));
    ASSERT_EQ(session.document().lines()[0]->id, c);
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 1}));
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

}

TEST(ChangedLinesSort, SortingMarksOnlyTheLinesThatLandOnAnotherRow)
{
    auto all = sessionOf({"0:00:03.00", "0:00:01.00", "0:00:05.00"});
    ASSERT_TRUE(sortLines(all, SortKey::Start, false));
    EXPECT_EQ(order(all), "qpr");
    EXPECT_EQ(statesOf(all), (std::vector<int>{1, 1, 0}));
    auto some = sessionOf({"0:00:04.00", "0:00:03.00", "0:00:02.00", "0:00:01.00"});
    const auto lines = some.document().lines();
    some.setSelection(Selection{lines[0]->id, {lines[0]->id, lines[2]->id, lines[3]->id}, lines[0]->id, {}});
    ASSERT_TRUE(sortLines(some, SortKey::Start, true));
    EXPECT_EQ(order(some), "sqrp");
    EXPECT_EQ(statesOf(some), (std::vector<int>{1, 0, 0, 1}));
}

TEST_F(ChangedLinesTest, AddingLinesToAGroupMarksOnlyTheLinesItCopies)
{
    select({b, c}, b);
    ASSERT_TRUE(makeGroups(session));
    const core::LineId description = session.document().lines()[1]->id;
    EXPECT_EQ(states(), (std::vector<int>{0, 0, 0, 0}));
    select({a}, a);
    ASSERT_TRUE(addLinesToGroup(session, description));
    ASSERT_EQ(session.document().lines()[1]->id, a);
    EXPECT_EQ(states(), (std::vector<int>{0, 1, 0, 0}));
}

TEST_F(ChangedLinesTest, SelectLinesMovesMarkTheMovedLines)
{
    SelectLinesSettings move;
    move.find = u8"b";
    move.action = SelectLinesSettings::Action::MoveToBeginning;
    ASSERT_TRUE(selectLines(session, move));
    ASSERT_EQ(session.document().lines()[0]->id, b);
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 0}));
    ASSERT_TRUE(session.undo());
    move.find = u8"a";
    ASSERT_TRUE(selectLines(session, move));
    ASSERT_EQ(session.document().lines()[0]->id, a);
    EXPECT_EQ(states(), (std::vector<int>{1, 0, 0}));
}

TEST_F(ChangedLinesTest, ShiftingTimesMarksTheLinesOnlyWhenTheTimesMove)
{
    edit(b, u8"B");
    save();
    ShiftTimesSettings none;
    none.tagTimes = true;
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
    auto cut = two("0:00:01.00,0:00:03.50", "0:00:03.00,0:00:04.00");
    ShiftTimesSettings correct;
    correct.correctEndTimes = 1;
    ASSERT_TRUE(shiftTimes(cut, correct, context));
    EXPECT_EQ(statesOf(cut), (std::vector<int>{1, 0}));
    auto gap = two("0:00:01.00,0:00:02.00", "0:00:02.30,0:00:03.00");
    ShiftTimesSettings continuous;
    continuous.postprocessor = 16 | 4;
    continuous.thresholdStart = 200;
    continuous.thresholdEnd = 300;
    ASSERT_TRUE(shiftTimes(gap, continuous, context));
    EXPECT_EQ(statesOf(gap), (std::vector<int>{1, 1}));
    auto far = two("0:00:01.00,0:00:02.00", "0:00:05.00,0:00:06.00");
    context.keyframes = {200};
    ShiftTimesSettings snap;
    snap.postprocessor = 16 | 8;
    snap.keyframeBeforeStart = snap.keyframeAfterStart = snap.keyframeBeforeEnd = snap.keyframeAfterEnd = 100;
    ASSERT_TRUE(shiftTimes(far, snap, context));
    EXPECT_EQ(statesOf(far), (std::vector<int>{0, 0}));
}
