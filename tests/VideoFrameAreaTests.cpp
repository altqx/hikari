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
#include "../HikariSub/VideoFrameArea.h"

namespace {

// RECT's fields, as the renderer's frame rectangle has them
struct Rect {
	long left, top, right, bottom;
};

// a 4:3 picture in a 16:9 window of 640 x 360: bars of 80 on either side
const Rect pillarboxed = { 80, 0, 560, 360 };
// a 2.39:1 picture in the same window: bars of 46 above and below
const Rect letterboxed = { 0, 46, 640, 314 };

} // namespace

TEST(the_picture_holds_the_pointer)
{
	CHECK(IsOnVideoFrame(pillarboxed, 320, 180));
	CHECK(IsOnVideoFrame(letterboxed, 320, 180));
}

TEST(the_bars_beside_the_picture_are_not_on_it)
{
	CHECK(!IsOnVideoFrame(pillarboxed, 10, 180));
	CHECK(!IsOnVideoFrame(pillarboxed, 79, 180));
	CHECK(!IsOnVideoFrame(pillarboxed, 600, 180));
	CHECK(!IsOnVideoFrame(pillarboxed, 639, 180));
}

TEST(the_bars_above_and_below_the_picture_are_not_on_it)
{
	CHECK(!IsOnVideoFrame(letterboxed, 320, 0));
	CHECK(!IsOnVideoFrame(letterboxed, 320, 45));
	CHECK(!IsOnVideoFrame(letterboxed, 320, 314));
	CHECK(!IsOnVideoFrame(letterboxed, 320, 359));
}

TEST(the_player_panel_under_the_picture_is_not_on_it)
{
	// the window's client area goes on under the video area, to the toolbar,
	// the seek bar and the time display
	const Rect filling = { 0, 0, 640, 360 };
	CHECK(!IsOnVideoFrame(filling, 320, 360));
	CHECK(!IsOnVideoFrame(filling, 320, 420));
	CHECK(!IsOnVideoFrame(filling, -1, 180));
}

TEST(the_left_and_top_edges_are_on_the_picture_the_right_and_bottom_are_not)
{
	CHECK(IsOnVideoFrame(pillarboxed, 80, 0));
	CHECK(IsOnVideoFrame(pillarboxed, 559, 359));
	CHECK(!IsOnVideoFrame(pillarboxed, 560, 180));
	CHECK(!IsOnVideoFrame(pillarboxed, 320, 360));
}

TEST(no_video_has_no_picture_to_be_on)
{
	const Rect none = { 0, 0, 0, 0 };
	CHECK(!IsOnVideoFrame(none, 0, 0));
	CHECK(!IsOnVideoFrame(none, 10, 10));
}
