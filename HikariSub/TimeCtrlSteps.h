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

#include <cmath>

// The steps a time control takes for a mouse wheel notch or a right-button
// drag, and the MPL2 times it shows.
namespace TimeCtrlSteps {

// MPL2 shows tenths of a second, up to 359999 (9:59:59.9).
constexpr int MPL2_TENTH_MS = 100;
constexpr int MPL2_MAX_MS = 359999 * MPL2_TENTH_MS;
// h:mm:ss times go up to 9:59:59.999.
constexpr int MAX_MS = 35999999;

// The tenths of a second MPL2 shows for a time, rounded up.
inline int Mpl2Tenths(int ms)
{
	return (int)std::ceil(ms * (10.0f / 1000.0f));
}

// One step: a frame for frame numbers, a tenth of a second for MPL2, and for
// h:mm:ss times the hours, minutes, seconds or hundredths under the pointer,
// where the hours' digits end at hoursEnd.
inline int Grad(bool frames, bool mpl2, long pointerPos, int hoursEnd)
{
	if (frames)
		return 1;
	if (mpl2)
		return MPL2_TENTH_MS;
	if (pointerPos < hoursEnd)
		return 3600000;
	if (pointerPos < hoursEnd + 3)
		return 60000;
	if (pointerPos < hoursEnd + 6)
		return 1000;
	return 10;
}

inline int MaxMs(bool mpl2)
{
	return mpl2 ? MPL2_MAX_MS : MAX_MS;
}

} // namespace TimeCtrlSteps
