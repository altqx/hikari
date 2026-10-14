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

#pragma once

#include <climits>

// A MicroDVD time is a frame number. The time field's wheel and right-button
// drag step that number, and the time in milliseconds follows the frame, so
// SubsTime::raw never takes a 0 frame for one that was not computed yet.
namespace MicroDvdSteps {

// the largest frame the time field takes
constexpr int MAX_FRAME = 999999999;

// the frame `steps` frames from `frame`, held in 0..MAX_FRAME
inline int StepFrame(int frame, long long steps)
{
	long long next = (long long)frame + steps;
	if (next < 0)
		return 0;
	if (next > MAX_FRAME)
		return MAX_FRAME;
	return (int)next;
}

// the time of a frame at fps, as a typed frame gets it (SubsTime::ParseMS):
// 0 at frame 0, and INT_MAX past what an int holds
inline int FrameTime(int frame, float fps)
{
	float ms = (frame / fps) * (1000.f);
	if (ms >= (float)INT_MAX)
		return INT_MAX;
	return (int)ms;
}

} // namespace MicroDvdSteps
