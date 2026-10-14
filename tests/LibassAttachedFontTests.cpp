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

// A font attached to a script reaches libass as SubtitlesLibass hands it
// over: the library and renderer are made first (ProcessLibassCache), the
// script's fonts go into the library through AttachedFonts when it is read
// (AddAttachedFonts), and the track is GetVisible's text, without [Fonts].
// The fixture's "HH" in HikariAttachedTest must then be drawn as two filled
// boxes, which no installed font draws for "H".

#include "check.h"
#include "AttachmentFixture.h"
#include "../HikariSub/ScriptAttachments.h"

extern "C" {
#include <ass/ass.h>
}

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const int kWidth = 640, kHeight = 360;

struct Drawn {
	// the left part of the frame, where only the "HH" Line draws
	int left = kWidth, top = kHeight, right = -1, bottom = -1;
	long covered = 0;
	std::vector<unsigned char> alpha = std::vector<unsigned char>(kWidth * kHeight);
	// how much of the drawn area is covered
	double Fill() const
	{
		if (right < left)
			return 0;
		return double(covered) / (double(right - left + 1) * (bottom - top + 1));
	}
	bool At(int x, int y) const { return alpha[y * kWidth + x] >= 128; }
};

// Renders `script` at 1 s with the library's fonts plus the system's.
Drawn Render(const std::string &script, bool addAttachedFonts)
{
	Drawn drawn;
	ASS_Library *library = ass_library_init();
	ass_set_message_cb(library, [](int, const char *, va_list, void *) {}, nullptr);
	ASS_Renderer *renderer = ass_renderer_init(library);
	ass_set_fonts(renderer, "Arial", "Arial", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
	ass_set_frame_size(renderer, kWidth, kHeight);
	ass_set_storage_size(renderer, kWidth, kHeight);
	if (addAttachedFonts) {
		size_t added = AttachedFonts::AddSince(0, [&](const AttachedFont &font) {
			ass_add_font(library, const_cast<char *>(font.name.c_str()),
				const_cast<char *>((const char *)font.data->data()), (int)font.data->size());
		});
		CHECK(added >= 1);
	}
	ASS_Track *track = ass_read_memory(library, const_cast<char *>(script.data()), script.size(), nullptr);
	CHECK(track != nullptr);
	if (track) {
		int change = 0;
		for (ASS_Image *img = ass_render_frame(renderer, track, 1000, &change); img; img = img->next) {
			int opacity = 255 - (int)(img->color & 0xFF);
			for (int y = 0; y < img->h; y++) {
				for (int x = 0; x < img->w; x++) {
					int px = img->dst_x + x, py = img->dst_y + y;
					if (px < 0 || py < 0 || px >= 360 || py >= kHeight)
						continue;
					unsigned char a = (unsigned char)(img->bitmap[y * img->stride + x] * opacity / 255);
					unsigned char &at = drawn.alpha[py * kWidth + px];
					at = std::max(at, a);
				}
			}
		}
		ass_free_track(track);
	}
	for (int y = 0; y < kHeight; y++) {
		for (int x = 0; x < 360; x++) {
			if (!drawn.At(x, y))
				continue;
			drawn.covered++;
			drawn.left = std::min(drawn.left, x);
			drawn.right = std::max(drawn.right, x);
			drawn.top = std::min(drawn.top, y);
			drawn.bottom = std::max(drawn.bottom, y);
		}
	}
	ass_renderer_done(renderer);
	ass_library_done(library);
	return drawn;
}

} // namespace

TEST(libass_draws_with_the_scripts_attached_font)
{
	attachment_fixture::Split split = attachment_fixture::SplitScript();
	ScriptAttachments attachments(split.embedded);
	CHECK_EQ(attachments.Fonts().size(), (size_t)1);

	// Before: the whole script, [Fonts] included, as a renderer would have
	// been handed it had GetVisible kept the section: libass reads no font
	// from a track, so another font draws "HH".
	Drawn before = Render(attachment_fixture::Read("attached.ass"), false);
	std::printf("  without the attached font: %ld pixels, %.2f of [%d,%d]-[%d,%d]\n", before.covered,
		before.Fill(), before.left, before.top, before.right, before.bottom);
	CHECK(before.covered > 0);
	CHECK(before.Fill() < 0.75);

	// After: GetVisible's text, and the font added to the library.
	AttachedFonts::Add(attachments.Fonts());
	Drawn after = Render(split.visible, true);
	std::printf("  with the attached font: %ld pixels, %.2f of [%d,%d]-[%d,%d]\n", after.covered, after.Fill(),
		after.left, after.top, after.right, after.bottom);
	// two boxes of 90 by 70 pixels (100 px text, 1000-unit em), 10 apart
	CHECK(after.Fill() > 0.9);
	CHECK(after.right - after.left > 170 && after.right - after.left < 200);
	CHECK(after.bottom - after.top > 60 && after.bottom - after.top < 80);
	// the middle of each box, where an "H" has its counters
	int middle = (after.top + after.bottom) / 2;
	CHECK(after.At(after.left + 45, after.top + 10));
	CHECK(after.At(after.left + 45, after.bottom - 10));
	CHECK(after.At(after.right - 45, middle - 20));
}
