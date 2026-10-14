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
#include "../HikariSub/MoveTimes.h"

TEST(a_move_without_times_covers_the_whole_line)
{
	CHECK(MoveCoversWholeLine(4, 0, 0));
	CHECK(MoveCoversWholeLine(5, 200, 0));
}

TEST(a_move_whose_times_are_both_zero_or_less_covers_the_whole_line)
{
	// \move(x1,y1,x2,y2,0,0) and the like move over the whole Line in libass
	CHECK(MoveCoversWholeLine(6, 0, 0));
	CHECK(MoveCoversWholeLine(6, -500, 0));
	CHECK(MoveCoversWholeLine(6, 0, -500));
	CHECK(MoveCoversWholeLine(6, -800, -200));
}

TEST(a_move_with_a_time_after_the_line_start_keeps_its_own_times)
{
	CHECK(!MoveCoversWholeLine(6, 0, 1000));
	CHECK(!MoveCoversWholeLine(6, -200, 1000));
	CHECK(!MoveCoversWholeLine(6, 1000, 0));
	CHECK(!MoveCoversWholeLine(6, 300, 300));
}
