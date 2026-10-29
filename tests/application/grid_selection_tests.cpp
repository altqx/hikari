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
    Selection s = rules.shiftKey(at(2), 1);
    EXPECT_EQ(s.active, L(5));
    EXPECT_EQ(s.anchor, L(2));
    EXPECT_EQ(s.selected, ids({2, 3, 4, 5}));
    s = rules.shiftKey(s, 1);
    EXPECT_EQ(s.selected, ids({2, 3, 4, 5, 6}));
    s = rules.shiftKey(s, -3);
    EXPECT_EQ(s.active, L(1));
    EXPECT_EQ(s.selected, ids({1, 2}));
}

TEST_F(GridSelectionTest, WithoutChangeActiveShiftMovesTheExtent)
{
    rules.setChangeActiveOnSelection(false);
    Selection s = rules.shiftKey(at(2), 1);
    EXPECT_EQ(s.active, L(2));
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
    EXPECT_EQ(s.active, L(5));
    s = rules.ctrlClick(s, L(1));
    EXPECT_EQ(s.selected, ids({5}));
    EXPECT_EQ(s.active, L(1));
    EXPECT_EQ(rules.ctrlClick(s, L(5)).selected, ids({1}));
    EXPECT_EQ(rules.ctrlClick(at(5), L(5)), at(5));
    rules.setChangeActiveOnSelection(false);
    EXPECT_EQ(rules.ctrlClick(at(1), L(5)).active, L(1));
}

TEST_F(GridSelectionTest, ShiftClickReplacesAndCtrlShiftAdds)
{
    Selection s = rules.plain({}, L(6));
    s = rules.ctrlClick(s, L(1));
    Selection replaced = rules.shiftClick(s, L(2), false);
    EXPECT_EQ(replaced.selected, ids({1, 2}));
    EXPECT_EQ(replaced.active, L(2));
    EXPECT_EQ(replaced.anchor, L(1));
    Selection added = rules.shiftClick(s, L(2), true);
    EXPECT_EQ(added.selected, ids({1, 2, 6}));
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

}
