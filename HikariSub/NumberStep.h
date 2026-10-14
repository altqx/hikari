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

// The value a mouse wheel turn or a right-button drag moves a number field to:
// value plus step, held to [rangeFrom, rangeTo]. A step past an end stops at
// that end, and a typed value outside the range steps to the end it is past.
// The caller makes no step when the result equals value.
inline double SteppedNumber(double value, double step, double rangeFrom, double rangeTo)
{
	double stepped = value + step;
	if (stepped < rangeFrom)
		return rangeFrom;
	if (stepped > rangeTo)
		return rangeTo;
	return stepped;
}
