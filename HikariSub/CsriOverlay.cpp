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

#include "CsriOverlay.h"

#include <algorithm>

bool RequestCsriFormat(csri_fmt *fmt, csri_frame *frame,
	const std::function<int(const csri_fmt *)> &request)
{
	fmt->pixfmt = frame->pixfmt;
	if (!request(fmt))
		return true;
	if (frame->pixfmt != CSRI_F_BGRA)
		return false;
	// the renderer reads the format from fmt, so both have to change
	fmt->pixfmt = frame->pixfmt = CSRI_F_BGR_;
	return !request(fmt);
}

void CsriOverlayFromBlackAndWhite(unsigned char *black, const unsigned char *white, size_t pixels)
{
	for (size_t i = 0; i < pixels; i++) {
		unsigned char *p = black + i * 4;
		const unsigned char *w = white + i * 4;
		// how much of the white shows through, summed over the channels
		int through = 0;
		for (int c = 0; c < 3; c++)
			through += (std::max)(0, int(w[c]) - int(p[c]));
		int a = 255 - (through + 1) / 3;
		if (a <= 0) {
			p[0] = p[1] = p[2] = p[3] = 0;
			continue;
		}
		for (int c = 0; c < 3; c++)
			p[c] = (unsigned char)(std::min)(255, (int(p[c]) * 255 + a / 2) / a);
		p[3] = (unsigned char)a;
	}
}

void SetCsriFramePlane(csri_frame *frame, unsigned char *buffer, unsigned width,
	unsigned height, int bytesPerPixel, bool swapped)
{
	ptrdiff_t line = (ptrdiff_t)width * bytesPerPixel;
	if (swapped && height > 0) {
		frame->planes[0] = buffer + line * (ptrdiff_t)(height - 1);
		frame->strides[0] = -line;
	}
	else {
		frame->planes[0] = buffer;
		frame->strides[0] = line;
	}
}
