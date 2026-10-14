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

// Whether a point of the video window is on the video's picture: the frame
// rectangle the renderer draws (VideoBox::GetVideoRect), not the bars around
// it nor the player's panel under it. A zoomed video still fills that
// rectangle. The right and bottom edges are outside it, as a RECT's are, and
// an empty rectangle (no video yet) holds no point.
template <typename Rect>
bool IsOnVideoFrame(const Rect &frame, int x, int y)
{
	return x >= frame.left && x < frame.right && y >= frame.top && y < frame.bottom;
}
