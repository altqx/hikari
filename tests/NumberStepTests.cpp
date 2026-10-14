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
#include "../HikariSub/NumberStep.h"

#include <vector>

namespace {

// A margin field: 0 to 9999.
constexpr double marginFrom = 0;
constexpr double marginTo = 9999;

// What NumCtrl does with a run of steps: each one that moves the value is a
// step the field shows and reports; one that leaves it as it is, is none.
struct Field {
	double value;
	int reported = 0;

	void Step(double step)
	{
		double stepped = SteppedNumber(value, step, marginFrom, marginTo);
		if (stepped == value)
			return;
		value = stepped;
		++reported;
	}
};

} // namespace

TEST(a_step_inside_the_range_moves_by_the_step)
{
	CHECK_EQ(SteppedNumber(10, 1, marginFrom, marginTo), 11.0);
	CHECK_EQ(SteppedNumber(10, -1, marginFrom, marginTo), 9.0);
	CHECK_EQ(SteppedNumber(10, 10, marginFrom, marginTo), 20.0);
	CHECK_EQ(SteppedNumber(10, -10, marginFrom, marginTo), 0.0);
	CHECK_EQ(SteppedNumber(2.5, 1, 0, 100), 3.5);
}

TEST(a_step_past_an_end_stops_at_that_end)
{
	CHECK_EQ(SteppedNumber(9998, 3, marginFrom, marginTo), marginTo);
	CHECK_EQ(SteppedNumber(2, -10, marginFrom, marginTo), marginFrom);
	// The layer field's negative end
	CHECK_EQ(SteppedNumber(-9999995, -10, -10000000, 10000000), -10000000.0);
}

TEST(a_step_at_an_end_leaves_the_value)
{
	CHECK_EQ(SteppedNumber(marginTo, 1, marginFrom, marginTo), marginTo);
	CHECK_EQ(SteppedNumber(marginFrom, -1, marginFrom, marginTo), marginFrom);
	CHECK_EQ(SteppedNumber(marginFrom, -10, marginFrom, marginTo), marginFrom);
}

TEST(a_typed_value_past_an_end_steps_to_that_end)
{
	// OnNumWrite keeps a typed 12000 as the value; either direction brings it
	// into the range
	CHECK_EQ(SteppedNumber(12000, 1, marginFrom, marginTo), marginTo);
	CHECK_EQ(SteppedNumber(12000, -1, marginFrom, marginTo), marginTo);
	CHECK_EQ(SteppedNumber(-5, -1, 0, 100), 0.0);
	CHECK_EQ(SteppedNumber(-5, 1, 0, 100), 0.0);
}

TEST(the_wheel_turns_back_at_once_after_turning_past_an_end)
{
	// Before, the value went on counting past 9999 while the field still showed
	// 9999, and the wheel came back only after as many notches the other way
	Field field{ 9998 };
	field.Step(1);
	field.Step(1);
	field.Step(1);
	field.Step(1);
	CHECK_EQ(field.value, marginTo);
	CHECK_EQ(field.reported, 1);
	field.Step(-1);
	CHECK_EQ(field.value, 9998.0);
	CHECK_EQ(field.reported, 2);
}

TEST(a_typed_value_past_an_end_can_be_dragged_one_at_a_time)
{
	// Before, a vertical drag from a typed 12000 refused every step
	Field field{ 12000 };
	field.Step(-1);
	CHECK_EQ(field.value, marginTo);
	field.Step(-1);
	CHECK_EQ(field.value, 9998.0);
	CHECK_EQ(field.reported, 2);
}
