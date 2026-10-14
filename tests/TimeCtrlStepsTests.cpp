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
#include "../HikariSub/TimeCtrlSteps.h"

#include <algorithm>

using namespace TimeCtrlSteps;

namespace {

// What TimeCtrl does for a wheel notch on an MPL2 time: it reads the shown
// tenths back (SubsTime::ParseMS), moves by the step and shows the result,
// or keeps the time when it would leave the range.
int Mpl2Wheel(int shownTenths, int notches)
{
	int ms = shownTenths * MPL2_TENTH_MS;
	int moved = ms + notches * Grad(false, true, 0, 3);
	if (moved < 0 || moved > MaxMs(true))
		return shownTenths;
	return Mpl2Tenths(moved);
}

// What TimeCtrl does for a right-button drag on an MPL2 time: it reads the
// shown tenths once, then moves by one step per vertical step and ten per
// sideways step, clamped to the range.
int Mpl2Drag(int shownTenths, int verticalSteps, int sidewaysSteps)
{
	int grad = Grad(false, true, 0, 3);
	int ms = shownTenths * MPL2_TENTH_MS + verticalSteps * grad + sidewaysSteps * grad * 10;
	return Mpl2Tenths(std::clamp(ms, 0, MaxMs(true)));
}

} // namespace

TEST(mpl2_shows_tenths_rounded_up)
{
	CHECK_EQ(Mpl2Tenths(0), 0);
	CHECK_EQ(Mpl2Tenths(1000), 10);
	CHECK_EQ(Mpl2Tenths(999), 10);
	CHECK_EQ(Mpl2Tenths(1001), 11);
	CHECK_EQ(Mpl2Tenths(MPL2_MAX_MS), 359999);
}

TEST(mpl2_wheel_moves_one_tenth_each_way)
{
	CHECK_EQ(Mpl2Wheel(10, -1), 9);
	CHECK_EQ(Mpl2Wheel(10, 1), 11);
	CHECK_EQ(Mpl2Wheel(10, -3), 7);
	// every shown value reads back as a whole number of tenths, so a notch
	// down never rounds back up to the same value
	for (int tenths = 1; tenths <= 359999; tenths += 997)
		CHECK_EQ(Mpl2Wheel(tenths, -1), tenths - 1);
	CHECK_EQ(Mpl2Wheel(359999, -1), 359998);
}

TEST(mpl2_wheel_stays_in_range)
{
	CHECK_EQ(Mpl2Wheel(0, -1), 0);
	CHECK_EQ(Mpl2Wheel(359999, 1), 359999);
	CHECK_EQ(Mpl2Wheel(359998, 1), 359999);
}

TEST(mpl2_drag_moves_one_tenth_per_step_and_ten_sideways)
{
	CHECK_EQ(Mpl2Drag(10, 1, 0), 11);
	CHECK_EQ(Mpl2Drag(10, -1, 0), 9);
	CHECK_EQ(Mpl2Drag(10, 0, 1), 20);
	CHECK_EQ(Mpl2Drag(10, 0, -1), 0);
	CHECK_EQ(Mpl2Drag(10, 0, -2), 0);
	CHECK_EQ(Mpl2Drag(359995, 0, 1), 359999);
	CHECK_EQ(Mpl2Drag(359999, 1, 0), 359999);
}

TEST(other_formats_keep_their_steps)
{
	// frame numbers, MicroDVD and frames shown for MPL2
	CHECK_EQ(Grad(true, false, 0, 3), 1);
	// ASS: h:mm:ss.cc with the hours ending at 2
	CHECK_EQ(Grad(false, false, 0, 2), 3600000);
	CHECK_EQ(Grad(false, false, 3, 2), 60000);
	CHECK_EQ(Grad(false, false, 6, 2), 1000);
	CHECK_EQ(Grad(false, false, 9, 2), 10);
	// SRT and TMPlayer: hh:mm:ss with the hours ending at 3
	CHECK_EQ(Grad(false, false, 2, 3), 3600000);
	CHECK_EQ(Grad(false, false, 5, 3), 60000);
	CHECK_EQ(Grad(false, false, 8, 3), 1000);
	CHECK_EQ(Grad(false, false, 10, 3), 10);
	CHECK_EQ(MaxMs(false), 35999999);
}
