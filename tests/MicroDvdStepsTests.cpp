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
#include "../HikariSub/MicroDvdSteps.h"

#include <climits>
#include <cmath>

using namespace MicroDvdSteps;

namespace {

const float NTSC_FILM = 24000.f / 1001.f;

// The frame SubsTime::raw shows for MicroDVD: a 0 frame with a time is
// taken for one not computed yet and computed from the time.
int ShownFrame(int frame, int ms, float fps)
{
	if (!frame && ms)
		return (int)std::ceil(ms * (fps / 1000.f));
	return frame;
}

// A time field's MicroDVD value after a wheel notch or a drag step.
struct Field {
	int frame;
	int ms;
	void Step(int steps)
	{
		frame = StepFrame(frame, steps);
		ms = FrameTime(frame, NTSC_FILM);
	}
	int Shown() const { return ShownFrame(frame, ms, NTSC_FILM); }
};

Field Typed(int frame) { return { frame, FrameTime(frame, NTSC_FILM) }; }

} // namespace

TEST(MicroDvdStepsWheelDownToZeroShowsZero)
{
	// The wheel used to step only the frame, so frame 1 stepped down kept
	// frame 1's time and showed 1 again.
	Field field = Typed(1);
	CHECK_EQ(field.ms, 41);
	CHECK_EQ(ShownFrame(0, field.ms, NTSC_FILM), 1);

	field.Step(-1);
	CHECK_EQ(field.frame, 0);
	CHECK_EQ(field.ms, 0);
	CHECK_EQ(field.Shown(), 0);
}

TEST(MicroDvdStepsDragChangesTheFrame)
{
	// The drag used to step the time by 1 ms, which moves no frame.
	Field field = Typed(100);
	field.Step(1);
	CHECK_EQ(field.Shown(), 101);
	field.Step(1);
	field.Step(1);
	CHECK_EQ(field.Shown(), 103);
	field.Step(-1);
	CHECK_EQ(field.Shown(), 102);
	// a sideways step is ten frames
	field.Step(10);
	CHECK_EQ(field.Shown(), 112);
	field.Step(-10);
	CHECK_EQ(field.Shown(), 102);
	CHECK_EQ(field.ms, FrameTime(102, NTSC_FILM));
}

TEST(MicroDvdStepsHoldTheFrameInRange)
{
	CHECK_EQ(StepFrame(0, -1), 0);
	CHECK_EQ(StepFrame(5, -10), 0);
	CHECK_EQ(StepFrame(MAX_FRAME, 1), MAX_FRAME);
	CHECK_EQ(StepFrame(MAX_FRAME - 3, 10), MAX_FRAME);
	CHECK_EQ(StepFrame(MAX_FRAME, -1), MAX_FRAME - 1);
	CHECK_EQ(StepFrame(INT_MAX, 1), MAX_FRAME);
	CHECK_EQ(StepFrame(0, -(long long)INT_MAX - 10), 0);

	Field field = Typed(3);
	field.Step(-10);
	CHECK_EQ(field.frame, 0);
	CHECK_EQ(field.Shown(), 0);
}

TEST(MicroDvdStepsTimeMatchesATypedFrame)
{
	// SubsTime::ParseMS: (frame / fps) * 1000, truncated
	for (int frame : { 0, 1, 2, 24, 25, 1000, 86313, 862769 }) {
		int typed = (int)((frame / NTSC_FILM) * (1000.f));
		CHECK_EQ(FrameTime(frame, NTSC_FILM), typed);
		CHECK_EQ(ShownFrame(frame, FrameTime(frame, NTSC_FILM), NTSC_FILM), frame);
	}
	CHECK_EQ(FrameTime(0, 25.f), 0);
	CHECK_EQ(FrameTime(25, 25.f), 1000);
	// the largest frame is past what an int of milliseconds holds
	CHECK_EQ(FrameTime(MAX_FRAME, NTSC_FILM), INT_MAX);
}
