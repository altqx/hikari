//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

#include "check.h"
#include "../HikariSub/MenuBarHitTest.h"

namespace {

// A bar 26 pixels high with three titles, 20 pixels between them, like
// File (0..44), Edit (44..90) and Video (90..145).
const std::vector<int> widths = { 24, 26, 35 };
const int indent = 20;
const int height = 26;

} // namespace

TEST(a_point_in_the_bar_is_over_the_title_at_its_x)
{
	int start = -1;
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 10, 12, &start), 0);
	CHECK_EQ(start, 0);
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 70, 12, &start), 1);
	CHECK_EQ(start, 44);
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 100, 0, &start), 2);
	CHECK_EQ(start, 90);
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 70, height - 1), 1);
}

TEST(a_point_past_the_last_title_or_at_the_left_edge_is_over_none)
{
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 145, 12), -1);
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 400, 12), -1);
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 0, 12), -1);
}

// The pointer jumps from File into the open File menu, at an x under Edit.
// The Leaving event carries that point; it is not over Edit, so the bar
// keeps File open and the click lands on the File menu's item.
TEST(a_point_below_the_bar_is_over_no_title)
{
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 70, height), -1);
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 70, 357), -1);
}

TEST(a_point_above_the_bar_is_over_no_title)
{
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 70, -1), -1);
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 10, -40), -1);
}

TEST(no_title_is_found_on_an_empty_or_flat_bar)
{
	CHECK_EQ(MenuBarTitleAt({}, indent, height, 10, 12), -1);
	CHECK_EQ(MenuBarTitleAt(widths, indent, 0, 10, 0), -1);
}

TEST(a_miss_leaves_the_title_start_alone)
{
	int start = 7;
	CHECK_EQ(MenuBarTitleAt(widths, indent, height, 70, 300, &start), -1);
	CHECK_EQ(start, 7);
}
