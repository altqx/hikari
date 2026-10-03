// G1: Grid selection gestures over stable LineIds (docs/qt/ux/grid-operations.md;
// legacy SubsGridWindow at 20d647c4). Destinations follow the displayed order;
// range membership is the Document-order interval, hidden Lines included.

#include "hikari/application/grid_selection.h"

#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::application;

namespace {

core::LineId L(std::uint64_t v)
{
    return core::LineId{v};
}

std::set<core::LineId> ids(std::initializer_list<std::uint64_t> v)
{
    std::set<core::LineId> out;
    for (auto x : v)
        out.insert(L(x));
    return out;
}

// Lines 1..6 in Document order; 3 and 4 hidden by a filter.
struct GridSelectionTest : ::testing::Test {
    GridSelection rules{{L(1), L(2), L(3), L(4), L(5), L(6)}, {L(1), L(2), L(5), L(6)}};
    Selection at(std::uint64_t active) const { return rules.plain({}, L(active)); }
};

TEST_F(GridSelectionTest, PlainMovesAndResetsTheRange)
{
    Selection s = at(2);
    EXPECT_EQ(s.active, L(2));
    EXPECT_EQ(s.selected, ids({2}));
    EXPECT_EQ(s.anchor, L(2));
    EXPECT_FALSE(s.extent);
}

TEST_F(GridSelectionTest, ShiftKeyMovesTheActiveLineAndIncludesHiddenLines)
{
    // Change active on selection (the legacy default): the anchor stays.
    Selection s = rules.shiftKey(at(2), 1); // displayed neighbour of 2 is 5
    EXPECT_EQ(s.active, L(5));
    EXPECT_EQ(s.anchor, L(2));
    EXPECT_EQ(s.selected, ids({2, 3, 4, 5})); // the hidden 3 and 4 are members
    s = rules.shiftKey(s, 1);
    EXPECT_EQ(s.selected, ids({2, 3, 4, 5, 6}));
    s = rules.shiftKey(s, -3); // back past the anchor, clamped to the first displayed row
    EXPECT_EQ(s.active, L(1));
    EXPECT_EQ(s.selected, ids({1, 2}));
}

TEST_F(GridSelectionTest, WithoutChangeActiveShiftMovesTheExtent)
{
    rules.setChangeActiveOnSelection(false);
    Selection s = rules.shiftKey(at(2), 1);
    EXPECT_EQ(s.active, L(2)); // stays
    EXPECT_EQ(s.extent, L(5));
    EXPECT_EQ(s.selected, ids({2, 3, 4, 5}));
    s = rules.shiftKey(s, 1);
    EXPECT_EQ(s.extent, L(6));
    EXPECT_EQ(s.selected, ids({2, 3, 4, 5, 6}));
}

TEST_F(GridSelectionTest, CtrlClickTogglesButNeverLeavesNothing)
{
    Selection s = rules.ctrlClick(at(1), L(5));
    EXPECT_EQ(s.selected, ids({1, 5}));
    EXPECT_EQ(s.active, L(5)); // change active
    s = rules.ctrlClick(s, L(1));
    EXPECT_EQ(s.selected, ids({5}));
    EXPECT_EQ(s.active, L(1)); // legacy: active moves even to the deselected Line
    // Deselecting the last selected Line falls back to the active one.
    EXPECT_EQ(rules.ctrlClick(s, L(5)).selected, ids({1}));
    // Ctrl+click on the active Line when it alone is selected changes nothing.
    EXPECT_EQ(rules.ctrlClick(at(5), L(5)), at(5));
    rules.setChangeActiveOnSelection(false);
    EXPECT_EQ(rules.ctrlClick(at(1), L(5)).active, L(1));
}

TEST_F(GridSelectionTest, ShiftClickReplacesAndCtrlShiftAdds)
{
    Selection s = rules.plain({}, L(6));
    s = rules.ctrlClick(s, L(1)); // anchor 1, selected {1, 6}
    Selection replaced = rules.shiftClick(s, L(2), false);
    EXPECT_EQ(replaced.selected, ids({1, 2}));
    EXPECT_EQ(replaced.active, L(2));
    EXPECT_EQ(replaced.anchor, L(1));
    Selection added = rules.shiftClick(s, L(2), true);
    EXPECT_EQ(added.selected, ids({1, 2, 6}));
    // Keyboard selection continues from where the mouse range started.
    EXPECT_EQ(rules.shiftKey(replaced, 1).selected, ids({1, 2, 3, 4, 5}));
}

TEST_F(GridSelectionTest, SelectAllIncludesHiddenLines)
{
    EXPECT_EQ(rules.selectAll(at(2)).selected, ids({1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(rules.selectAll(at(2)).active, L(2));
}

TEST_F(GridSelectionTest, AHiddenOriginStepsToTheNearestShownLine)
{
    EXPECT_EQ(rules.displayedFrom(L(3), 1), L(5));
    EXPECT_EQ(rules.displayedFrom(L(3), -1), L(2));
    EXPECT_EQ(rules.displayedFrom(std::nullopt, 1), L(1));
}

} // namespace
