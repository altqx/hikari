// G3: insert, duplicate and delete (legacy SubsGrid at 20d647c4), one undo
// step each, with the legacy timing, run and selection rules.

#include "hikari/application/grid_commands.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/line_formats.h"

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

// a 1-2 s, b 5-6 s (Actor B, margin 7), c 6.5-8 s.
constexpr std::string_view kThree = "[Events]\n"
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                    "Dialogue: 2,0:00:05.00,0:00:06.00,Sign,B,7,0,0,fx,b\n"
                                    "Dialogue: 0,0:00:06.50,0:00:08.00,Default,,0,0,0,,c\n";

struct GridCommandsTest : ::testing::Test {
    EditSession session{load(kThree)};
    core::LineId a{1}, b{2}, c{3};
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
    std::vector<std::u8string> texts() const
    {
        std::vector<std::u8string> out;
        for (const auto *l : session.document().lines())
            out.push_back(l->text);
        return out;
    }
    const core::LineRecord &line(std::size_t i) const { return *session.document().lines()[i]; }
    static std::int64_t ms(core::DocumentTime t) { return t.microseconds() / 1000; }
};

TEST_F(GridCommandsTest, InsertBeforeStartsFourSecondsEarlierAndCopiesTheFields)
{
    select({b}, b);
    const auto steps = session.historySize();
    ASSERT_TRUE(insertLine(session, InsertWhere::Before));
    ASSERT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"", u8"b", u8"c"}));
    const auto &n = line(1);
    EXPECT_EQ(ms(n.start.value), 1000); // 5000 - 4000 (a ends at 2000, before b's start)
    EXPECT_EQ(ms(n.end.value), 5000);
    EXPECT_EQ(n.style, u8"Sign");
    EXPECT_EQ(n.actor, u8"B");
    EXPECT_EQ(n.layer.value, 2);
    EXPECT_EQ(n.effect, u8"fx");
    EXPECT_EQ(session.selection().active, n.id);
    EXPECT_EQ(session.selection().selected, std::set<core::LineId>{n.id});
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Inserting line");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"c"}));
}

TEST_F(GridCommandsTest, InsertBeforeAnOverlappedLineTakesThePreviousEnd)
{
    // Legacy quirk, characterized: the previous Line ends after this one
    // starts, so the new Line runs from 7.0 s to 6.5 s.
    EditSession overlap{load("[Events]\n"
                             "Dialogue: 0,0:00:05.00,0:00:07.00,Default,,0,0,0,,x\n"
                             "Dialogue: 0,0:00:06.50,0:00:08.00,Default,,0,0,0,,y\n")};
    overlap.setSelection(Selection{core::LineId{2}, {core::LineId{2}}, {}, {}});
    ASSERT_TRUE(insertLine(overlap, InsertWhere::Before));
    const auto &n = *overlap.document().lines()[1];
    EXPECT_EQ(ms(n.start.value), 7000);
    EXPECT_EQ(ms(n.end.value), 6500);
}

TEST_F(GridCommandsTest, InsertBeforeTheFirstLineClampsAtZero)
{
    select({a}, a);
    ASSERT_TRUE(insertLine(session, InsertWhere::Before));
    EXPECT_EQ(ms(line(0).start.value), 0);
    EXPECT_EQ(ms(line(0).end.value), 1000);
}

TEST_F(GridCommandsTest, InsertAfterFillsTheGapOrLastsFourSeconds)
{
    select({a}, a);
    ASSERT_TRUE(insertLine(session, InsertWhere::After));
    EXPECT_EQ(ms(line(1).start.value), 2000);
    EXPECT_EQ(ms(line(1).end.value), 5000); // up to b's start
    select({c}, c);
    ASSERT_TRUE(insertLine(session, InsertWhere::After));
    EXPECT_EQ(ms(line(4).start.value), 8000);
    EXPECT_EQ(ms(line(4).end.value), 12000); // no next Line
    EXPECT_EQ(session.selection().active, line(4).id);
}

TEST_F(GridCommandsTest, InsertWithVideoTimeStartsThereForFourSeconds)
{
    select({b}, b);
    ASSERT_TRUE(insertLine(session, InsertWhere::After, 3456));
    EXPECT_EQ(ms(line(2).start.value), 3450); // ZEROIT
    EXPECT_EQ(ms(line(2).end.value), 7450);
    EXPECT_EQ(line(2).text, u8"");
}

TEST_F(GridCommandsTest, InsertWithFrameTimesCopiesTheSelectedLines)
{
    select({a, c}, c);
    ASSERT_TRUE(insertWithFrameTimes(session, InsertWhere::After, FrameTimes{1001, 1043}));
    ASSERT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"c", u8"a", u8"c"}));
    EXPECT_EQ(ms(line(3).start.value), 1000);
    EXPECT_EQ(ms(line(3).end.value), 1040);
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{line(3).id, line(4).id}));
    EXPECT_EQ(session.selection().active, line(3).id);
    ASSERT_TRUE(session.undo());
    select({b}, b);
    ASSERT_TRUE(insertWithFrameTimes(session, InsertWhere::Before, FrameTimes{2000, 2040}));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"b", u8"c"}));
    EXPECT_EQ(line(1).id, session.selection().active);
}

TEST_F(GridCommandsTest, DuplicateCopiesTheFirstContiguousRun)
{
    select({a, b}, a);
    ASSERT_TRUE(duplicateLines(session));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"a", u8"b", u8"c"}));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{line(2).id, line(3).id}));
    EXPECT_EQ(session.history().back().name, "Duplicating lines");
    ASSERT_TRUE(session.undo());
    // A shown unselected Line ends the run: only a is copied.
    select({a, c}, a);
    ASSERT_TRUE(duplicateLines(session));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"a", u8"b", u8"c"}));
    ASSERT_TRUE(session.undo());
    // A hidden unselected Line does not: a and c are copied after c.
    select({a, c}, a);
    ASSERT_TRUE(duplicateLines(session, [&](core::LineId id) { return id != b; }));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"c", u8"a", u8"c"}));
    ASSERT_TRUE(session.undo());
    // GRID_DUPLICATION_DONT_CHANGE_SELECTION keeps the originals selected.
    select({b}, b);
    ASSERT_TRUE(duplicateLines(session, {}, true));
    EXPECT_EQ(session.selection().selected, std::set<core::LineId>{b});
}

TEST_F(GridCommandsTest, DeleteRemovesTheSelectionAndActivatesTheNextLine)
{
    select({a, b}, b);
    ASSERT_TRUE(deleteLines(session));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"c"}));
    EXPECT_EQ(session.selection().active, c);
    EXPECT_EQ(session.history().back().name, "Deleting lines");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"c"}));
    EXPECT_EQ(line(0).id, a); // identities come back
}

TEST_F(GridCommandsTest, DeletingEveryLineLeavesTheDefaultLine)
{
    select({a, b, c}, a);
    ASSERT_TRUE(deleteLines(session));
    ASSERT_EQ(session.document().lines().size(), 1u);
    EXPECT_EQ(line(0).text, u8"");
    EXPECT_EQ(line(0).style, u8"Default");
    EXPECT_EQ(ms(line(0).end.value), 5000);
    EXPECT_EQ(session.selection().active, line(0).id);
}

TEST_F(GridCommandsTest, CommandsAreRefusedOnAReadOnlyTarget)
{
    select({b}, b);
    session.setReadOnly(true);
    EXPECT_EQ(insertLine(session, InsertWhere::Before).error(), CommandRefusal::ReadOnly);
    EXPECT_EQ(deleteLines(session).error(), CommandRefusal::ReadOnly);
    EXPECT_EQ(texts().size(), 3u);
}

// G5: join, swap and continuous timing.
TEST_F(GridCommandsTest, JoinKeepsTheFirstLineAndOnlyDeletesSelectedOnes)
{
    // J56-selected-only-join: with a and c selected, b between them survives.
    select({a, c}, a);
    ASSERT_TRUE(joinLines(session, JoinKind::Join));
    ASSERT_EQ(texts(), (std::vector<std::u8string>{u8"a\\Nc", u8"b"}));
    EXPECT_EQ(line(0).id, a);
    EXPECT_EQ(ms(line(0).start.value), 1000);
    EXPECT_EQ(ms(line(0).end.value), 8000);
    EXPECT_EQ(line(1).id, b);
    EXPECT_EQ(session.selection().active, a);
    EXPECT_EQ(session.history().back().name, "Joining lines");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b", u8"c"}));
}

TEST_F(GridCommandsTest, JoinSkipsEmptyTextsAfterTheFirst)
{
    EditSession s{load("[Events]\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n"
                       "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,x\n"
                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,\n"
                       "Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,y\n")};
    s.setSelection(Selection{core::LineId{1}, {core::LineId{1}, core::LineId{2}, core::LineId{3}, core::LineId{4}}, {}, {}});
    ASSERT_TRUE(joinLines(s, JoinKind::Join));
    ASSERT_EQ(s.document().lines().size(), 1u);
    EXPECT_EQ(s.document().lines()[0]->text, u8"x\\Ny"); // the empty first takes "x" without a separator
}

TEST_F(GridCommandsTest, JoinKeepFirstAndKeepLast)
{
    select({a, b}, a);
    ASSERT_TRUE(joinLines(session, JoinKind::KeepFirst));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"c"}));
    EXPECT_EQ(ms(line(0).start.value), 1000);
    EXPECT_EQ(ms(line(0).end.value), 6000);
    ASSERT_TRUE(session.undo());
    select({a, b}, a);
    ASSERT_TRUE(joinLines(session, JoinKind::KeepLast));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"b", u8"c"}));
    EXPECT_EQ(line(0).id, a); // the first Line survives with the last one's text
    EXPECT_EQ(session.history().back().name, "Joining lines and keeping the last");
}

TEST_F(GridCommandsTest, MergeWithThePreviousOrNextShownLine)
{
    select({b}, b);
    ASSERT_TRUE(joinLines(session, JoinKind::WithNext));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a", u8"b\\Nc"}));
    ASSERT_TRUE(session.undo());
    // With b hidden, c's previous shown Line is a; b survives (J56).
    select({c}, c);
    ASSERT_TRUE(joinLines(session, JoinKind::WithPrevious, [&](core::LineId id) { return id != b; }));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"a\\Nc", u8"b"}));
    select({a}, a);
    EXPECT_FALSE(joinLines(session, JoinKind::WithPrevious)); // nothing before the first Line
}

TEST_F(GridCommandsTest, JoinNeedsTwoToTwentyLines)
{
    select({a}, a);
    EXPECT_FALSE(joinLines(session, JoinKind::Join));
}

TEST_F(GridCommandsTest, SwapTradesPlacesAndKeepsTheActiveRow)
{
    select({a, c}, a);
    ASSERT_TRUE(swapLines(session));
    EXPECT_EQ(texts(), (std::vector<std::u8string>{u8"c", u8"b", u8"a"}));
    EXPECT_EQ(line(0).id, c); // identities move with the Lines
    EXPECT_EQ(line(2).id, a);
    EXPECT_EQ(session.selection().active, c); // the active row stays first
    EXPECT_EQ(session.history().back().name, "Swapping lines");
    select({b}, b);
    EXPECT_FALSE(swapLines(session)); // exactly two
}

TEST_F(GridCommandsTest, ContinuousTimesFollowTheNeighbours)
{
    select({b, c}, b);
    ASSERT_TRUE(makeContinuous(session, true));
    EXPECT_EQ(ms(line(1).start.value), 2000); // a's End
    EXPECT_EQ(ms(line(2).start.value), 6000); // b's End
    EXPECT_EQ(session.history().back().name, "Setting line times as continuous");
    ASSERT_TRUE(session.undo());
    select({a, c}, a);
    ASSERT_TRUE(makeContinuous(session, false));
    EXPECT_EQ(ms(line(0).end.value), 5000); // b's Start
    EXPECT_EQ(ms(line(2).end.value), 8000); // the last Line has no next one
}

} // namespace

namespace {

// Starts 5, 1, 3 and 1 (ties by End), with Styles, Actors, Effects and Layers.
constexpr std::string_view kUnsorted = "[Events]\n"
                                       "Dialogue: 2,0:00:05.00,0:00:06.00,B,y,0,0,0,e2,p\n"
                                       "Dialogue: 1,0:00:01.00,0:00:04.00,A,z,0,0,0,e1,q\n"
                                       "Dialogue: 2,0:00:03.00,0:00:04.00,C,x,0,0,0,e1,r\n"
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,A,w,0,0,0,e3,s\n";

std::u8string order(const EditSession &s)
{
    std::u8string out;
    for (const auto *l : s.document().lines())
        out += l->text;
    return out;
}

} // namespace

TEST(SortLines, EveryKeyFollowsTheLegacyComparators)
{
    const std::pair<SortKey, std::u8string> cases[] = {
        {SortKey::Start, u8"sqrp"},  // 1 (ends 2, 4), 3, 5
        {SortKey::End, u8"sqrp"},    // 2, 4 (starts 1, 3), 6
        {SortKey::Style, u8"qspr"},  // A (starts 1, 1: stable), B, C
        {SortKey::Actor, u8"srpq"},  // w, x, y, z
        {SortKey::Layer, u8"sqrp"},  // 0, 1, 2 (starts 3, 5)
    };
    for (const auto &[key, expected] : cases) {
        EditSession session{load(kUnsorted)};
        ASSERT_TRUE(sortLines(session, key, false)) << static_cast<int>(key);
        EXPECT_EQ(order(session), expected) << static_cast<int>(key);
    }
    // Legacy quirk: Lines with different Effects are ordered by Actor.
    EditSession effect{load(kUnsorted)};
    ASSERT_TRUE(sortLines(effect, SortKey::Effect, false));
    EXPECT_EQ(order(effect), u8"srpq");
}

TEST(SortLines, OneStepThatKeepsIdentitiesAndUndoes)
{
    EditSession session{load(kUnsorted)};
    const auto before = session.document().lines();
    const core::LineId p = before[0]->id;
    const auto steps = session.historySize();
    ASSERT_TRUE(sortLines(session, SortKey::Start, false));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Sorting subtitles");
    EXPECT_EQ(session.document().lines()[3]->id, p);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(order(session), u8"pqrs");
    // An order that does not change adds no step.
    ASSERT_TRUE(session.redo());
    const auto again = session.historySize();
    ASSERT_TRUE(sortLines(session, SortKey::Start, false));
    EXPECT_EQ(session.historySize(), again);
}

TEST(SortLines, SelectedLinesSortAmongTheirOwnRowsAndTheSelectionKeepsItsRows)
{
    EditSession session{load(kUnsorted)};
    const auto lines = session.document().lines();
    // Rows 0 and 3 (p at 5 s, s at 1 s); row 0 active.
    session.setSelection(Selection{lines[0]->id, {lines[0]->id, lines[3]->id}, lines[0]->id, {}});
    ASSERT_TRUE(sortLines(session, SortKey::Start, true));
    EXPECT_EQ(order(session), u8"sqrp");
    const auto after = session.document().lines();
    EXPECT_EQ(session.selection().active, after[0]->id); // row 0, now s
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{after[0]->id, after[3]->id}));
    session.setSelection(Selection{});
    EXPECT_FALSE(sortLines(session, SortKey::Start, true));
}

TEST(SortLines, TextKeysUseTheGivenCollation)
{
    EditSession session{load(kUnsorted)};
    // Reverse collation: C, B, A (A's tie stays stable by Start).
    ASSERT_TRUE(sortLines(session, SortKey::Style, false,
                          [](std::u8string_view a, std::u8string_view b) { return b.compare(a); }));
    EXPECT_EQ(order(session), u8"rpqs");
}

TEST_F(GridCommandsTest, FpsFromVideoScalesFromTheFirstSelectedLine)
{
    // a 1-2 s and b 5-6 s selected; the video shows 9 s: the distance from a
    // doubles, so b starts at 9 s.
    select({a, b}, a);
    const auto steps = session.historySize();
    ASSERT_TRUE(setFpsFromVideo(session, 9000));
    EXPECT_EQ(ms(line(0).start.value), 1000);
    EXPECT_EQ(ms(line(0).end.value), 3000);
    EXPECT_EQ(ms(line(1).start.value), 9000);
    EXPECT_EQ(ms(line(1).end.value), 11000);
    EXPECT_EQ(ms(line(2).start.value), 12000); // unselected Lines too
    EXPECT_EQ(ms(line(2).end.value), 15000);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Setting FPS from video");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(ms(line(1).start.value), 5000);
    // Exactly two shown Lines with different Starts.
    select({a}, a);
    EXPECT_FALSE(setFpsFromVideo(session, 9000));
    select({a, b}, a);
    EXPECT_FALSE(setFpsFromVideo(session, 9000, [&](core::LineId id) { return id != b; }));
    EditSession same{load("[Events]\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n"
                          "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,y\n")};
    same.setSelection(Selection{core::LineId{1}, {core::LineId{1}, core::LineId{2}}, {}, {}});
    EXPECT_FALSE(setFpsFromVideo(same, 9000));
}

TEST_F(GridCommandsTest, NewFpsScalesEveryTime)
{
    ASSERT_TRUE(setNewFps(session, 25, 23.976));
    // 1000 * 25 / 23.976 = 1042.7, truncated.
    EXPECT_EQ(ms(line(0).start.value), 1042);
    EXPECT_EQ(ms(line(2).end.value), 8341);
    EXPECT_EQ(session.history().back().name, "Setting custom FPS");
    EXPECT_FALSE(setNewFps(session, 0, 25));
}

TEST(NewFps, MicroDvdFramesFollowTheDocumentsOwnRate)
{
    const std::string_view text = "{24}{48}first\n";
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    auto document = core::loadLineFormats(bytes).document;
    ASSERT_EQ(document.format(), core::SubtitleFormat::MicroDvd);
    EditSession unknown{document};
    EXPECT_EQ(setNewFps(unknown, 25, 50).error(), CommandRefusal::Invalid); // C01: no rate yet
    document.setFrameRate(*core::FrameRate::make(25, 1));
    EditSession session{document};
    ASSERT_TRUE(setNewFps(session, 25, 50));
    const auto &l = *session.document().lines()[0];
    EXPECT_EQ(l.start.value.microseconds(), 480'000); // 960 ms * 0.5
    EXPECT_EQ(l.startFrame, 12);
    EXPECT_EQ(l.endFrame, 24);
}
