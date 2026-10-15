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

// The libass these tests, and the application, link is the altqx fork in
// Thirdparty/libass, never a distribution's: only the fork has these entry
// points, so this fails to compile or to link against any other. It also
// draws one script with two renderers of one library, the way the video and
// the style previews do.

#include "check.h"

extern "C" {
#include <ass/ass.h>
}

#if !defined(LIBASS_HAS_FRAME_ATLAS) || !defined(LIBASS_HAS_TRACK_EVENTS_MODIFIED)
#error "the tests build against the altqx libass fork in Thirdparty/libass"
#endif

#include <cstring>

namespace {

const char kScript[] =
	"[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n"
	"[V4+ Styles]\n"
	"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
	"Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
	"MarginR, MarginV, Encoding\n"
	"Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n\n"
	"[Events]\n"
	"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
	"Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\\p1}m 0 0 l 300 0 300 100 0 100{\\p0}\n";

int ImageCount(const ASS_Image *image)
{
	int count = 0;
	for (; image; image = image->next)
		++count;
	return count;
}

} // namespace

TEST(LibassIsTheAltqxFork)
{
	CHECK(ass_frame_pack_atlas != nullptr);
	CHECK(ass_track_events_modified != nullptr);
	CHECK(LIBASS_VERSION >= 0x01705010);
}

TEST(TwoRenderersOfOneLibraryDrawOneTrackAtTheirOwnSizes)
{
	ASS_Library *library = ass_library_init();
	CHECK(library != nullptr);
	if (!library)
		return;
	ASS_Track *track = ass_read_memory(library, const_cast<char *>(kScript), sizeof(kScript) - 1, nullptr);
	CHECK(track != nullptr);
	ASS_Renderer *video = ass_renderer_init(library);
	ASS_Renderer *preview = ass_renderer_init(library);
	CHECK(video != nullptr && preview != nullptr);
	if (track && video && preview) {
		ass_set_fonts(video, nullptr, "Arial", 1, nullptr, false);
		ass_set_fonts(preview, nullptr, "Arial", 1, nullptr, false);
		ass_set_cache_limits(preview, 500, 16);
		int change = 0;
		ass_set_frame_size(video, 640, 360);
		CHECK(ImageCount(ass_render_frame(video, track, 1000, &change)) > 0);
		// the preview's size changes leave the video renderer as it was
		ass_set_frame_size(preview, 320, 180);
		CHECK(ImageCount(ass_render_frame(preview, track, 1000, &change)) > 0);
		ass_set_frame_size(preview, 160, 90);
		CHECK(ImageCount(ass_render_frame(preview, track, 1000, &change)) > 0);
		CHECK(ImageCount(ass_render_frame(video, track, 1000, &change)) > 0);
		CHECK_EQ(change, 0);
	}
	if (preview)
		ass_renderer_done(preview);
	if (video)
		ass_renderer_done(video);
	if (track)
		ass_free_track(track);
	ass_library_done(library);
}
