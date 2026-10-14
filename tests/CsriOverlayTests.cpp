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
#include "../HikariSub/CsriOverlay.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {

// What VSFilterMod's csri_request_fmt does: it reads the format from fmt and
// takes BGR_ but not BGRA.
struct BgrOnlyRenderer
{
	std::vector<int> asked;
	int Request(const csri_fmt *fmt)
	{
		asked.push_back(fmt->pixfmt);
		return fmt->pixfmt == CSRI_F_BGR_ ? 0 : -1;
	}
};

csri_frame Frame(csri_pixfmt pixfmt)
{
	csri_frame frame = {};
	frame.pixfmt = pixfmt;
	return frame;
}

// One subtitle pixel of a BGR_ renderer: colour (straight) with coverage
// alpha, blended into the frame as VSFilter blends RGB32.
void BlendBgr(unsigned char *dst, const unsigned char colour[3], int alpha)
{
	for (int c = 0; c < 3; c++)
		dst[c] = (unsigned char)((dst[c] * (255 - alpha) + colour[c] * alpha + 127) / 255);
}

// The overlay over a frame pixel as the Direct3D9 renderers blend it for
// VSFilter (straight alpha: D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA).
void Composite(unsigned char *dst, const unsigned char *overlay)
{
	int a = overlay[3];
	for (int c = 0; c < 3; c++)
		dst[c] = (unsigned char)((overlay[c] * a + dst[c] * (255 - a) + 127) / 255);
}

} // namespace

TEST(csri_bgra_refused_falls_back_to_bgr_in_the_format_the_renderer_reads)
{
	BgrOnlyRenderer renderer;
	csri_fmt fmt = { CSRI_F_BGRA, 64, 32 };
	csri_frame frame = Frame(CSRI_F_BGRA);
	bool accepted = RequestCsriFormat(&fmt, &frame,
		[&](const csri_fmt *f) { return renderer.Request(f); });
	CHECK(accepted);
	CHECK_EQ((int)fmt.pixfmt, (int)CSRI_F_BGR_);
	CHECK_EQ((int)frame.pixfmt, (int)CSRI_F_BGR_);
	CHECK_EQ(renderer.asked.size(), (size_t)2);
	CHECK_EQ(renderer.asked.back(), (int)CSRI_F_BGR_);
}

TEST(csri_accepted_format_is_kept)
{
	csri_fmt fmt = { CSRI_F_BGR_, 64, 32 };
	csri_frame frame = Frame(CSRI_F_BGRA);
	int calls = 0;
	bool accepted = RequestCsriFormat(&fmt, &frame,
		[&](const csri_fmt *f) { calls++; return f->pixfmt == CSRI_F_BGRA ? 0 : -1; });
	CHECK(accepted);
	CHECK_EQ(calls, 1);
	CHECK_EQ((int)fmt.pixfmt, (int)CSRI_F_BGRA);
	CHECK_EQ((int)frame.pixfmt, (int)CSRI_F_BGRA);
}

TEST(csri_refused_format_other_than_bgra_has_no_fallback)
{
	csri_fmt fmt = { CSRI_F_BGRA, 64, 32 };
	csri_frame frame = Frame(CSRI_F_YV12);
	int calls = 0;
	bool accepted = RequestCsriFormat(&fmt, &frame,
		[&](const csri_fmt *) { calls++; return -1; });
	CHECK(!accepted);
	CHECK_EQ(calls, 1);
	CHECK_EQ((int)fmt.pixfmt, (int)CSRI_F_YV12);
}

TEST(csri_bgr_overlay_is_not_invisible)
{
	// fully covered text drawn onto a cleared overlay as before: alpha 0
	const unsigned char colour[3] = { 0x30, 0x80, 0xE0 };
	std::vector<unsigned char> black(4, 0), white(4, 255);
	BlendBgr(black.data(), colour, 255);
	BlendBgr(white.data(), colour, 255);
	white[3] = 255;
	CsriOverlayFromBlackAndWhite(black.data(), white.data(), 1);
	CHECK_EQ((int)black[0], 0x30);
	CHECK_EQ((int)black[1], 0x80);
	CHECK_EQ((int)black[2], 0xE0);
	CHECK_EQ((int)black[3], 255);
}

TEST(csri_bgr_overlay_leaves_untouched_pixels_transparent)
{
	std::vector<unsigned char> black(4 * 3, 0), white(4 * 3, 255);
	// the renderer leaves alpha bytes as they were; they must not matter
	black[3] = 0x55;
	CsriOverlayFromBlackAndWhite(black.data(), white.data(), 3);
	for (unsigned char b : black)
		CHECK_EQ((int)b, 0);
}

TEST(csri_bgr_overlay_composites_as_the_renderer_drew_onto_the_frame)
{
	std::srand(11);
	const int pixels = 4096;
	std::vector<unsigned char> black(pixels * 4, 0), white(pixels * 4, 255);
	std::vector<unsigned char> colours(pixels * 3), alphas(pixels), frame(pixels * 4);
	for (auto &c : colours) c = (unsigned char)std::rand();
	for (auto &a : alphas) a = (unsigned char)std::rand();
	for (auto &f : frame) f = (unsigned char)std::rand();
	for (int i = 0; i < pixels; i++) {
		BlendBgr(&black[i * 4], &colours[i * 3], alphas[i]);
		BlendBgr(&white[i * 4], &colours[i * 3], alphas[i]);
	}
	CsriOverlayFromBlackAndWhite(black.data(), white.data(), pixels);
	int worst = 0;
	for (int i = 0; i < pixels; i++) {
		unsigned char expected[4], actual[4];
		std::copy(&frame[i * 4], &frame[i * 4] + 4, expected);
		std::copy(&frame[i * 4], &frame[i * 4] + 4, actual);
		BlendBgr(expected, &colours[i * 3], alphas[i]);
		Composite(actual, &black[i * 4]);
		for (int c = 0; c < 3; c++)
			worst = std::max(worst, std::abs(int(expected[c]) - int(actual[c])));
	}
	CHECK(worst <= 2);
}
