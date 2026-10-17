#pragma once // TEMP: video presentation trace, never merged
#ifdef _WIN32
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
// HIKARI_VIDEO_TRACE=<file> appends one line per event: ms since start, thread, text
inline void VideoTrace(const char *fmt, ...)
{
	static FILE *f = [] { const char *p = getenv("HIKARI_VIDEO_TRACE"); return p ? fopen(p, "a") : nullptr; }();
	if (!f)
		return;
	static std::mutex mutex;
	static DWORD start = GetTickCount();
	std::lock_guard<std::mutex> lock(mutex);
	fprintf(f, "%6lu %5lu ", (unsigned long)(GetTickCount() - start), (unsigned long)GetCurrentThreadId());
	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	fputc('\n', f);
	fflush(f);
}
inline const char *VideoTraceWindow(HWND hwnd)
{
	static thread_local char text[160];
	RECT client = {};
	GetClientRect(hwnd, &client);
	POINT origin = { 0, 0 };
	ClientToScreen(hwnd, &origin);
	LONG style = GetWindowLong(hwnd, GWL_STYLE);
	snprintf(text, sizeof(text), "hwnd=%p client=%ldx%ld at %ld,%ld visible=%d iswinvis=%d", (void *)hwnd,
		client.right, client.bottom, origin.x, origin.y, (style & WS_VISIBLE) ? 1 : 0, IsWindowVisible(hwnd) ? 1 : 0);
	return text;
}
#else
inline void VideoTrace(const char *, ...) {}
template <typename T> inline const char *VideoTraceWindow(T) { return ""; }
#endif
