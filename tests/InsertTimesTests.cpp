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
#include "../HikariSub/InsertTimes.h"

TEST(insert_before_an_overlapped_line_takes_the_four_seconds_before)
{
	// the Line before runs 5.00-7.00 and the active Line 6.50-8.00: starting
	// the new Line at 7.00 would end it at 6.50, before its Start
	CHECK(!InsertBeforeFillsGap(7000, 6500));
}

TEST(insert_before_a_line_touching_the_one_before_takes_the_four_seconds_before)
{
	CHECK(!InsertBeforeFillsGap(6500, 6500));
}

TEST(insert_before_fills_the_gap_after_the_line_before)
{
	CHECK(InsertBeforeFillsGap(5000, 6500));
	CHECK(InsertBeforeFillsGap(0, 10000));
}
