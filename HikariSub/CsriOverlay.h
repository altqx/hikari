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

#include "csri.h"

#include <cstddef>
#include <functional>

// Asks for the frame's format; when BGRA is refused (VSFilterMod takes only
// BGR_), asks for BGR_. request returns 0 when a format is accepted, as
// csri_request_fmt does. Afterwards fmt and frame both name the format that
// was asked for last; returns whether it was accepted.
bool RequestCsriFormat(csri_fmt *fmt, csri_frame *frame,
	const std::function<int(const csri_fmt *)> &request);

// A BGR_ renderer blends into the frame and writes no alpha. Given the same
// instant drawn onto black (black) and onto white (white), both pixels*4
// bytes, turns black into the overlay with straight alpha that composites as
// that drawing did: alpha is 255 minus how much of the white shows through,
// and the colour is the black drawing divided by the alpha.
void CsriOverlayFromBlackAndWhite(unsigned char *black, const unsigned char *white, size_t pixels);
