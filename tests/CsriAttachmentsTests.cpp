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

// The fixture script through VSFilterMod as SubtitlesVSFilter hands it over:
// the attached fonts added to the process when the script is read
// (AttachedFonts, AddFontMemResourceEx), and GetVisible's text, without
// [Fonts] and [Graphics], opened with the script's [Graphics] after its
// events (CsriScriptText). The square filled with \1img(red.png) must be
// red, not the white VSFilterMod draws for an image it does not have, and
// "HH" must be drawn in the attached font's boxes.
// HIKARISUB_TEST_VSFILTERMOD names the VSFilterMod DLL (the package's
// Csri\VSFiltermod.dll); without it the test is skipped.

#include "check.h"
#include "AttachmentFixture.h"
#include "../HikariSub/ScriptAttachments.h"

#define CSRIAPI
#include "../HikariSub/csri.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

const int kWidth = 640, kHeight = 360;

struct Vsfiltermod {
	HMODULE module = nullptr;
	csri_rend *(*rendererDefault)() = nullptr;
	csri_inst *(*openMem)(csri_rend *, const void *, size_t, csri_openflag *) = nullptr;
	int (*requestFmt)(csri_inst *, const csri_fmt *) = nullptr;
	void (*render)(csri_inst *, csri_frame *, double) = nullptr;
	void (*close)(csri_inst *) = nullptr;

	explicit Vsfiltermod(const char *path)
	{
		module = LoadLibraryA(path);
		if (!module)
			return;
		rendererDefault = (csri_rend *(*)())GetProcAddress(module, "csri_renderer_default");
		openMem = (csri_inst *(*)(csri_rend *, const void *, size_t, csri_openflag *))GetProcAddress(module, "csri_open_mem");
		requestFmt = (int (*)(csri_inst *, const csri_fmt *))GetProcAddress(module, "csri_request_fmt");
		render = (void (*)(csri_inst *, csri_frame *, double))GetProcAddress(module, "csri_render");
		close = (void (*)(csri_inst *))GetProcAddress(module, "csri_close");
	}
	bool Loaded() const { return rendererDefault && openMem && requestFmt && render && close; }

	// BGR_ (the only format VSFilterMod takes) onto a black frame, at 1 s
	std::vector<unsigned char> Draw(const std::string &text)
	{
		std::vector<unsigned char> frame((size_t)kWidth * kHeight * 4, 0);
		csri_inst *instance = openMem(rendererDefault(), text.data(), text.size(), nullptr);
		CHECK(instance != nullptr);
		if (!instance)
			return frame;
		csri_fmt fmt = { CSRI_F_BGR_, (unsigned)kWidth, (unsigned)kHeight };
		CHECK_EQ(requestFmt(instance, &fmt), 0);
		csri_frame target = {};
		target.pixfmt = CSRI_F_BGR_;
		target.planes[0] = frame.data();
		target.strides[0] = kWidth * 4;
		render(instance, &target, 1.0);
		close(instance);
		return frame;
	}
};

const unsigned char *Pixel(const std::vector<unsigned char> &frame, int x, int y)
{
	return &frame[((size_t)y * kWidth + x) * 4];
}

// the share of the "HH" Line's drawn area that is lit
double Fill(const std::vector<unsigned char> &frame, long *lit)
{
	int left = kWidth, top = kHeight, right = -1, bottom = -1;
	*lit = 0;
	for (int y = 0; y < kHeight; y++) {
		for (int x = 0; x < 360; x++) {
			if (Pixel(frame, x, y)[1] < 128)
				continue;
			++*lit;
			left = std::min(left, x);
			right = std::max(right, x);
			top = std::min(top, y);
			bottom = std::max(bottom, y);
		}
	}
	return right < left ? 0 : double(*lit) / (double(right - left + 1) * (bottom - top + 1));
}

} // namespace

TEST(vsfiltermod_draws_the_scripts_attached_image_and_font)
{
	const char *path = std::getenv("HIKARISUB_TEST_VSFILTERMOD");
	if (!path || !*path) {
		std::printf("  skipped: HIKARISUB_TEST_VSFILTERMOD is not set\n");
		return;
	}
	Vsfiltermod vsfiltermod(path);
	CHECK(vsfiltermod.Loaded());
	if (!vsfiltermod.Loaded())
		return;
	attachment_fixture::Split split = attachment_fixture::SplitScript();
	ScriptAttachments attachments(split.embedded);

	// Before: GetVisible's text alone, as SubtitlesVSFilter opened it. The
	// "HH" Line is left out: VSFilterMod keeps the fonts it made for the
	// run, so a substitute made now would stand for the family later too
	// (the application adds the fonts before anything of the script is drawn).
	std::string withoutText = split.visible;
	size_t hh = withoutText.find("}HH\r\n");
	CHECK(hh != std::string::npos);
	if (hh == std::string::npos)
		return;
	size_t lineStart = withoutText.rfind("Dialogue:", hh);
	withoutText.erase(lineStart, hh + 5 - lineStart);
	std::vector<unsigned char> before = vsfiltermod.Draw(withoutText);
	const unsigned char *square = Pixel(before, 450, 90);
	std::printf("  without the attachments: the square is %d,%d,%d (BGR)\n", square[0], square[1], square[2]);
	CHECK(square[0] > 200 && square[1] > 200 && square[2] > 200);
	long lit = 0;
	double fill = 0;

	// After: the fonts added to the process, the graphics after the events.
	AttachedFonts::Add(attachments.Fonts());
	std::vector<unsigned char> after = vsfiltermod.Draw(CsriScriptText(split.visible, &attachments));
	square = Pixel(after, 450, 90);
	std::printf("  with the attachments: the square is %d,%d,%d (BGR)\n", square[0], square[1], square[2]);
	CHECK(square[0] < 30 && square[1] < 30 && square[2] > 225);
	// the whole square, its edges aside
	long red = 0;
	for (int y = 45; y < 135; y++)
		for (int x = 405; x < 495; x++) {
			const unsigned char *p = Pixel(after, x, y);
			red += p[0] < 30 && p[1] < 30 && p[2] > 225;
		}
	CHECK_EQ(red, 90L * 90L);
	fill = Fill(after, &lit);
	std::printf("  with the attachments: \"HH\" lights %ld pixels, %.2f of its area\n", lit, fill);
	CHECK(fill > 0.9);
}
