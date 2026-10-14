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

#include <cstdlib>

using namespace TimeCtrlSteps;

namespace {

// What TimeCtrl does for a wheel notch on an MPL2 time: it reads the shown
// tenths back (SubsTime::ParseMS), moves by the step and shows the result,
// or keeps the time when it would leave the range.
int Mpl2Wheel(int shownTenths, int notches)
{
	Step step = StepAt(false, false, true, 0, 3);
	int ms = shownTenths * MPL2_TENTH_MS;
	WheelStep(ms, notches * step.grad, step.max);
	return Mpl2Tenths(ms);
}

// What TimeCtrl does for a right-button drag on an MPL2 time: it reads the
// shown tenths once, then moves by one step per vertical step and ten per
// sideways step, clamped to the range.
int Mpl2Drag(int shownTenths, int verticalSteps, int sidewaysSteps)
{
	Step step = StepAt(false, false, true, 0, 3);
	int ms = shownTenths * MPL2_TENTH_MS;
	for (int i = 0; i < std::abs(verticalSteps); i++)
		DragStep(ms, verticalSteps < 0 ? -step.grad : step.grad, step.max);
	for (int i = 0; i < std::abs(sidewaysSteps); i++)
		DragStep(ms, (sidewaysSteps < 0 ? -step.grad : step.grad) * 10, step.max);
	return Mpl2Tenths(ms);
}

constexpr int ASS_HOURS_END = 2;
constexpr int SRT_HOURS_END = 3;

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

TEST(shown_frames_step_the_frame_number_by_one_in_every_column)
{
	// The hours column of an ASS, SRT or TMP field: with the Frames box
	// checked, still one frame, not an hour's worth of milliseconds.
	for (int hoursEnd : { ASS_HOURS_END, SRT_HOURS_END }) {
		for (long column : { 0L, 1L, 4L, 7L, 9L }) {
			for (bool mpl2 : { false, true }) {
				Step step = StepAt(true, false, mpl2, column, hoursEnd);
				CHECK(step.frames);
				CHECK_EQ(step.grad, 1);
				CHECK_EQ(step.max, MAX_FRAME);
			}
		}
	}
}

TEST(microdvd_steps_the_frame_number)
{
	Step step = StepAt(false, true, false, 0, SRT_HOURS_END);
	CHECK(step.frames);
	CHECK_EQ(step.grad, 1);
	CHECK_EQ(step.max, MAX_FRAME);
}

TEST(times_step_by_the_unit_of_the_column)
{
	// "0:00:00.00"
	CHECK_EQ(StepAt(false, false, false, 0, ASS_HOURS_END).grad, 3600000);
	CHECK_EQ(StepAt(false, false, false, 2, ASS_HOURS_END).grad, 60000);
	CHECK_EQ(StepAt(false, false, false, 5, ASS_HOURS_END).grad, 1000);
	CHECK_EQ(StepAt(false, false, false, 8, ASS_HOURS_END).grad, 10);
	// "00:00:00,000"
	CHECK_EQ(StepAt(false, false, false, 1, SRT_HOURS_END).grad, 3600000);
	CHECK_EQ(StepAt(false, false, false, 3, SRT_HOURS_END).grad, 60000);
	CHECK_EQ(StepAt(false, false, false, 6, SRT_HOURS_END).grad, 1000);
	CHECK_EQ(StepAt(false, false, false, 9, SRT_HOURS_END).grad, 10);

	Step step = StepAt(false, false, false, 0, ASS_HOURS_END);
	CHECK(!step.frames);
	CHECK_EQ(step.max, MAX_MS);

	Step mpl2 = StepAt(false, false, true, 0, SRT_HOURS_END);
	CHECK(!mpl2.frames);
	CHECK_EQ(mpl2.grad, MPL2_TENTH_MS);
	CHECK_EQ(mpl2.max, MPL2_MAX_MS);
}

TEST(a_drag_moves_a_frame_number_by_one_and_sideways_by_ten)
{
	Step step = StepAt(true, false, false, 0, ASS_HOURS_END);
	int frame = 1234;
	CHECK(DragStep(frame, step.grad, step.max));
	CHECK_EQ(frame, 1235);
	CHECK(DragStep(frame, -step.grad * 10, step.max));
	CHECK_EQ(frame, 1225);
}

TEST(a_drag_stops_at_the_limits)
{
	int frame = 5;
	CHECK(DragStep(frame, -10, MAX_FRAME));
	CHECK_EQ(frame, 0);
	CHECK(!DragStep(frame, -1, MAX_FRAME));
	CHECK_EQ(frame, 0);

	frame = MAX_FRAME - 3;
	CHECK(DragStep(frame, 10, MAX_FRAME));
	CHECK_EQ(frame, MAX_FRAME);
	CHECK(!DragStep(frame, 1, MAX_FRAME));
	CHECK_EQ(frame, MAX_FRAME);

	int ms = MAX_MS;
	CHECK(!DragStep(ms, 36000000, MAX_MS));
	CHECK_EQ(ms, MAX_MS);
	ms = 0;
	CHECK(!DragStep(ms, -3600000, MAX_MS));
	CHECK_EQ(ms, 0);
}

TEST(a_wheel_step_moves_a_frame_number)
{
	Step step = StepAt(true, false, false, 0, SRT_HOURS_END);
	int frame = 1234;
	CHECK(WheelStep(frame, 3 * step.grad, step.max));
	CHECK_EQ(frame, 1237);
	CHECK(WheelStep(frame, -step.grad, step.max));
	CHECK_EQ(frame, 1236);
}

TEST(a_wheel_step_past_a_limit_is_not_made)
{
	int frame = 0;
	CHECK(!WheelStep(frame, -1, MAX_FRAME));
	CHECK_EQ(frame, 0);
	frame = MAX_FRAME;
	CHECK(!WheelStep(frame, 1, MAX_FRAME));
	CHECK_EQ(frame, MAX_FRAME);

	int ms = 500;
	CHECK(!WheelStep(ms, -3600000, MAX_MS));
	CHECK_EQ(ms, 500);
	ms = MAX_MS - 5;
	CHECK(!WheelStep(ms, 10, MAX_MS));
	CHECK_EQ(ms, MAX_MS - 5);
}

TEST(a_wheel_turn_under_one_notch_makes_no_step)
{
	int frame = 100;
	CHECK(!WheelStep(frame, 0, MAX_FRAME));
	CHECK_EQ(frame, 100);
}
