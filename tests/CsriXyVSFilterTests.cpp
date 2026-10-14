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

// The xy-VSFilter CSRI renderer the Windows build makes (csri\xy-Vsfilter.dll),
// loaded from HIKARISUB_TEST_XYVSFILTER and called the way SubtitlesVSFilter
// calls it: HikariSub asks for the video format again each time it reuses a
// parsed instance from its cache.

#ifdef _WIN32

#include "check.h"
#include "../HikariSub/csri.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct XyVSFilter
{
	HMODULE module = nullptr;
	csri_rend *(*rendererDefault)() = nullptr;
	csri_inst *(*openMem)(csri_rend *, const void *, size_t, csri_openflag *) = nullptr;
	int (*requestFmt)(csri_inst *, const csri_fmt *) = nullptr;
	void (*render)(csri_inst *, csri_frame *, double) = nullptr;
	void (*close)(csri_inst *) = nullptr;
	csri_rend *renderer = nullptr;

	bool Loaded() const { return renderer != nullptr; }
};

template <typename F>
void Resolve(HMODULE module, const char *name, F &function)
{
	function = reinterpret_cast<F>(::GetProcAddress(module, name));
}

// loaded once and kept: HikariSub does not unload its renderer either
XyVSFilter &Renderer()
{
	static XyVSFilter xy = [] {
		// unbuffered: a renderer that leaks can crash the process on its way out,
		// and the measurements printed so far must not go with it
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		XyVSFilter loaded;
		const wchar_t *path = _wgetenv(L"HIKARISUB_TEST_XYVSFILTER");
		if (!path || !*path) {
			std::printf("HIKARISUB_TEST_XYVSFILTER is not set\n");
			return loaded;
		}
		loaded.module = ::LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!loaded.module) {
			std::printf("cannot load %ls (error %lu)\n", path, ::GetLastError());
			return loaded;
		}
		Resolve(loaded.module, "csri_renderer_default", loaded.rendererDefault);
		Resolve(loaded.module, "csri_open_mem", loaded.openMem);
		Resolve(loaded.module, "csri_request_fmt", loaded.requestFmt);
		Resolve(loaded.module, "csri_render", loaded.render);
		Resolve(loaded.module, "csri_close", loaded.close);
		if (loaded.rendererDefault && loaded.openMem && loaded.requestFmt && loaded.render && loaded.close)
			loaded.renderer = loaded.rendererDefault();
		return loaded;
	}();
	return xy;
}

std::string Header()
{
	return "[Script Info]\r\n"
		"ScriptType: v4.00+\r\n"
		"PlayResX: 640\r\n"
		"PlayResY: 360\r\n"
		"WrapStyle: 0\r\n"
		"\r\n"
		"[V4+ Styles]\r\n"
		"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
		"Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
		"Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
		"Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
		"0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\r\n"
		"\r\n"
		"[Events]\r\n"
		"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
}

// one opaque drawing over the whole 640x360 script, from 0 to 10 s
std::string CoveringScript()
{
	return Header() +
		"Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\\an7\\pos(0,0)\\p1}m 0 0 l 640 0 640 360 0 360{\\p0}\r\n";
}

// thousands of lines, so a parse that is never freed shows in the process's memory
std::string LongScript()
{
	std::string script = Header();
	std::string text(240, 'x');
	char line[128];
	for (int i = 0; i < 6000; i++) {
		int second = i % 3600;
		std::snprintf(line, sizeof(line), "Dialogue: 0,0:%02d:%02d.00,0:%02d:%02d.50,Default,,0,0,0,,line %d ",
			second / 60, second % 60, second / 60, second % 60, i);
		script += line;
		script += text;
		script += "\r\n";
	}
	return script;
}

csri_inst *Open(const std::string &script)
{
	XyVSFilter &xy = Renderer();
	return xy.openMem(xy.renderer, script.data(), script.size(), nullptr);
}

// the process's private bytes, in MB
double PrivateMB()
{
	PROCESS_MEMORY_COUNTERS_EX counters = {};
	counters.cb = sizeof(counters);
	::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
		sizeof(counters));
	return double(counters.PrivateUsage) / (1024.0 * 1024.0);
}

struct Frame
{
	unsigned width;
	unsigned height;
	std::vector<unsigned char> pixels;
	csri_frame frame = {};
	csri_fmt format = {};

	Frame(unsigned w, unsigned h) : width(w), height(h), pixels(size_t(w) * h * 4)
	{
		format.pixfmt = CSRI_F_BGRA;
		format.width = w;
		format.height = h;
		frame.pixfmt = CSRI_F_BGRA;
		frame.planes[0] = pixels.data();
		frame.strides[0] = ptrdiff_t(w) * 4;
	}

	bool Drawn(unsigned x, unsigned y) const
	{
		const unsigned char *pixel = &pixels[(size_t(y) * width + x) * 4];
		return pixel[0] || pixel[1] || pixel[2] || pixel[3];
	}

	void Clear() { std::fill(pixels.begin(), pixels.end(), 0); }
};

// A provider that is not released keeps a rendered frame, and the parsed
// script, alive: several MB per call here. This much growth over all the
// measured calls is below what two of them would leak.
const double kGrowthLimitMB = 8.0;

} // namespace

TEST(xyvsfilter_reused_instance_keeps_its_provider)
{
	XyVSFilter &xy = Renderer();
	CHECK(xy.Loaded());
	if (!xy.Loaded())
		return;
	csri_inst *instance = Open(CoveringScript());
	CHECK(instance != nullptr);
	if (!instance)
		return;
	Frame frame(1280, 720);
	CHECK_EQ(xy.requestFmt(instance, &frame.format), 0);
	xy.render(instance, &frame.frame, 1.0);
	// a provider is a few hundred bytes, so it takes many reuses to show
	int failed = 0;
	double before = PrivateMB();
	for (int i = 0; i < 20000; i++)
		failed += xy.requestFmt(instance, &frame.format) != 0;
	double growth = PrivateMB() - before;
	std::printf("  20000 reuses: private bytes grew %.2f MB\n", growth);
	CHECK_EQ(failed, 0);
	CHECK(growth < 1.0);
	frame.Clear();
	xy.render(instance, &frame.frame, 2.0);
	CHECK(frame.Drawn(1270, 710));
	xy.close(instance);
}

TEST(xyvsfilter_new_format_draws_at_the_new_size)
{
	XyVSFilter &xy = Renderer();
	CHECK(xy.Loaded());
	if (!xy.Loaded())
		return;
	csri_inst *instance = Open(CoveringScript());
	CHECK(instance != nullptr);
	if (!instance)
		return;
	Frame lowRes(640, 360);
	Frame highRes(1280, 720);
	auto alternate = [&] {
		lowRes.Clear();
		CHECK_EQ(xy.requestFmt(instance, &lowRes.format), 0);
		xy.render(instance, &lowRes.frame, 1.0);
		highRes.Clear();
		CHECK_EQ(xy.requestFmt(instance, &highRes.format), 0);
		xy.render(instance, &highRes.frame, 1.0);
	};
	for (int i = 0; i < 3; i++)
		alternate();
	double before = PrivateMB();
	for (int i = 0; i < 30; i++)
		alternate();
	double growth = PrivateMB() - before;
	std::printf("  30 format switches: private bytes grew %.1f MB\n", growth);
	CHECK(growth < kGrowthLimitMB);
	// the drawing covers the whole frame at each size, so the provider was replaced
	CHECK(lowRes.Drawn(630, 350));
	CHECK(highRes.Drawn(1270, 710));
	xy.close(instance);
}

TEST(xyvsfilter_close_frees_the_parsed_script)
{
	XyVSFilter &xy = Renderer();
	CHECK(xy.Loaded());
	if (!xy.Loaded())
		return;
	const std::string script = LongScript();
	Frame frame(640, 360);
	bool opened = true;
	auto openReuseClose = [&] {
		csri_inst *instance = Open(script);
		if (!instance) {
			opened = false;
			return;
		}
		// parsed, drawn once, reused, then evicted, as in HikariSub's cache
		xy.requestFmt(instance, &frame.format);
		xy.render(instance, &frame.frame, 1.0);
		xy.requestFmt(instance, &frame.format);
		xy.close(instance);
	};
	for (int i = 0; i < 2; i++)
		openReuseClose();
	double before = PrivateMB();
	for (int i = 0; i < 15; i++)
		openReuseClose();
	double growth = PrivateMB() - before;
	std::printf("  15 parses: private bytes grew %.1f MB\n", growth);
	CHECK(opened);
	CHECK(growth < kGrowthLimitMB);
}

#endif // _WIN32
