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

// The colour pickers' eyedropper pointer (HikariSub/DropperPointer.cpp): its
// cursor, and the simple picker's pointer grab, against the pinned wxGTK build
// on a real X display, with a second X client of this process standing for
// another application. Needs an X display (xvfb-run).

#include "check.h"
#include "WxGtkApp.h"

#include "../HikariSub/DropperPointer.h"

#include <wx/dialog.h>
#include <wx/image.h>
#include <wx/log.h>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::vector<unsigned char> ReadFile(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Counts the errors wx logs while it lives.
class ErrorCount : public wxLog
{
public:
	ErrorCount() : m_previous(wxLog::SetActiveTarget(this)) {}
	~ErrorCount() override { wxLog::SetActiveTarget(m_previous); }
	int errors = 0;

protected:
	void DoLogRecord(wxLogLevel level, const wxString &, const wxLogRecordInfo &) override
	{
		if (level <= wxLOG_Error)
			++errors;
	}

private:
	wxLog *m_previous;
};

// Another application: an X client of its own, which can hold the pointer.
class OtherClient
{
public:
	OtherClient() : m_display(XOpenDisplay(nullptr)) {}
	~OtherClient()
	{
		if (m_display) {
			if (m_patch)
				XDestroyWindow(m_display, m_patch);
			XUngrabPointer(m_display, CurrentTime);
			XCloseDisplay(m_display);
		}
	}
	bool Ok() const { return m_display != nullptr; }
	// GrabSuccess, or why X refused it (AlreadyGrabbed when this process's
	// window holds the pointer).
	int GrabPointer()
	{
		int result = XGrabPointer(m_display, DefaultRootWindow(m_display), False, ButtonPressMask,
			GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
		XSync(m_display, False);
		return result;
	}
	void UngrabPointer()
	{
		XUngrabPointer(m_display, CurrentTime);
		XSync(m_display, False);
	}
	void MovePointer(wxPoint screen)
	{
		XWarpPointer(m_display, None, DefaultRootWindow(m_display), 0, 0, 0, 0, screen.x, screen.y);
		XSync(m_display, False);
	}
	// Shows a window of one colour (0xRRGGBB on a TrueColor screen) at screen.
	void ShowPatch(const wxRect &screen, unsigned long rgb)
	{
		m_patch = XCreateSimpleWindow(m_display, DefaultRootWindow(m_display), screen.x, screen.y,
			screen.width, screen.height, 0, 0, rgb);
		XSetWindowAttributes attributes;
		attributes.override_redirect = True;
		XChangeWindowAttributes(m_display, m_patch, CWOverrideRedirect, &attributes);
		XMapRaised(m_display, m_patch);
		XSync(m_display, False);
	}
	// The cursor X shows now, as XFixes reports it: hotspot and size, or a
	// width of 0 without XFixes.
	struct ShownCursor {
		int xhot = -1;
		int yhot = -1;
		int width = 0;
		int height = 0;
	};
	ShownCursor Shown()
	{
		ShownCursor shown;
		int eventBase = 0, errorBase = 0;
		if (!XFixesQueryExtension(m_display, &eventBase, &errorBase))
			return shown;
		if (XFixesCursorImage *image = XFixesGetCursorImage(m_display)) {
			shown = { image->xhot, image->yhot, image->width, image->height };
			XFree(image);
		}
		return shown;
	}

private:
	Display *m_display;
	Window m_patch = 0;
};

// A top-level window like the simple picker, shown without a modal loop.
class Picker
{
public:
	Picker() : m_dialog(new wxDialog(nullptr, wxID_ANY, "picker", wxPoint(40, 40), wxSize(200, 120))) {}
	~Picker()
	{
		m_grab.reset();
		m_dialog->Destroy();
		RunEventsUntil([] { return false; }, 20ms);
	}
	wxDialog *Window() { return m_dialog; }
	DropperPointerGrab &Grab()
	{
		if (!m_grab)
			m_grab = std::make_unique<DropperPointerGrab>(m_dialog);
		return *m_grab;
	}
	bool Mapped()
	{
		GdkWindow *window = gtk_widget_get_window(m_dialog->GetHandle());
		return window && gdk_window_is_viewable(window) && gdk_window_is_visible(window);
	}

private:
	wxDialog *m_dialog;
	std::unique_ptr<DropperPointerGrab> m_grab;
};

bool OnX11()
{
	return WxGtkAppReady() && GDK_IS_X11_DISPLAY(gdk_display_get_default());
}

} // namespace

// The hotspot made on Linux is the one of the cursor Windows loads.
TEST(dropper_hotspot_is_the_cursor_files)
{
	std::vector<unsigned char> cur = ReadFile(HIKARISUB_TESTS_SOURCE_DIR "/../HikariSub/Bitmaps/eyedropper.cur");
	CHECK(cur.size() > 14);
	if (cur.size() <= 14)
		return;
	// ICONDIR (6 bytes), then the first ICONDIRENTRY: width, height, colours,
	// reserved, then the hotspot as two little-endian words.
	CHECK_EQ(int(cur[2]), 2);
	CHECK_EQ(int(cur[10] | (cur[11] << 8)), DROPPER_HOTSPOT_X);
	CHECK_EQ(int(cur[12] | (cur[13] << 8)), DROPPER_HOTSPOT_Y);
}

// wxCursor(L"eyedropper_cursor") on wxGTK looked for a file of that name and
// showed "Failed to load image from file" twice: the cursor made from the
// eyedropper picture loads without an error, and X shows it with the
// dropper's tip as its hotspot.
TEST(dropper_cursor_comes_from_the_eyedropper_picture)
{
	if (!OnX11())
		return;
	wxInitAllImageHandlers();
	ErrorCount log;
	wxImage picture(HIKARISUB_TESTS_SOURCE_DIR "/../HikariSub/Bitmaps/eyedropper.png", wxBITMAP_TYPE_PNG);
	CHECK(picture.IsOk());
	wxCursor cursor = DropperCursorFromImage(picture);
	CHECK(cursor.IsOk());
	CHECK_EQ(log.errors, 0);
	// No picture: a stock cursor still, never a broken one.
	CHECK(DropperCursorFromImage(wxImage()).IsOk());

	OtherClient other;
	CHECK(other.Ok());
	if (!other.Ok())
		return;
	Picker picker;
	picker.Window()->Show();
	CHECK(RunEventsUntil([&] { return picker.Mapped(); }, 2000ms));
	picker.Window()->SetCursor(cursor);
	const wxRect client = picker.Window()->GetScreenRect();
	other.MovePointer(client.GetPosition() + wxSize(client.width / 2, client.height / 2));
	RunEventsUntil([&] {
		OtherClient::ShownCursor now = other.Shown();
		return now.xhot == DROPPER_HOTSPOT_X && now.yhot == DROPPER_HOTSPOT_Y;
	}, 2000ms);
	OtherClient::ShownCursor shown = other.Shown();
	CHECK_EQ(shown.width, picture.GetWidth());
	CHECK_EQ(shown.height, picture.GetHeight());
	CHECK_EQ(shown.xhot, DROPPER_HOTSPOT_X);
	CHECK_EQ(shown.yhot, DROPPER_HOTSPOT_Y);
}

// The eyedroppers read the screen with wxScreenDC, which on wxGTK 3 gave black
// over another application's window: ReadScreenArea reads what X shows.
TEST(dropper_reads_the_colour_another_window_shows)
{
	if (!OnX11())
		return;
	OtherClient other;
	CHECK(other.Ok());
	if (!other.Ok())
		return;
	const wxRect patch(300, 200, 60, 40);
	other.ShowPatch(patch, 0x2468AC);
	wxImage shown;
	CHECK(RunEventsUntil([&] {
		shown = ReadScreenArea(wxRect(patch.x + 10, patch.y + 10, 7, 7));
		return shown.IsOk() && shown.GetRed(3, 3) == 0x24;
	}, 2000ms));
	CHECK(shown.IsOk());
	if (!shown.IsOk())
		return;
	CHECK_EQ(shown.GetWidth(), 7);
	CHECK_EQ(shown.GetHeight(), 7);
	for (int y = 0; y < 7; ++y)
		for (int x = 0; x < 7; ++x) {
			CHECK_EQ(int(shown.GetRed(x, y)), 0x24);
			CHECK_EQ(int(shown.GetGreen(x, y)), 0x68);
			CHECK_EQ(int(shown.GetBlue(x, y)), 0xAC);
		}

	// Over the patch's corner, partly off it: the rest is what lies around it.
	wxImage corner = ReadScreenArea(wxRect(patch.x - 3, patch.y - 3, 7, 7));
	CHECK(corner.IsOk());
	if (corner.IsOk()) {
		CHECK_EQ(int(corner.GetBlue(6, 6)), 0xAC);
		CHECK(corner.GetBlue(0, 0) != 0xAC);
	}

	// Off the screen's top-left corner: black there, the screen elsewhere.
	wxImage edge = ReadScreenArea(wxRect(-3, -3, 7, 7));
	CHECK(edge.IsOk());
	if (edge.IsOk()) {
		CHECK_EQ(edge.GetWidth(), 7);
		CHECK_EQ(int(edge.GetRed(0, 0)) + edge.GetGreen(0, 0) + edge.GetBlue(0, 0), 0);
	}
	CHECK(!ReadScreenArea(wxRect()).IsOk());
}

// The picker asked for the grab in wxEVT_SHOW, before the window was mapped:
// GDK refused it (Gdk-CRITICAL "has not been made visible"), wx reported the
// capture held all the same and nothing asked again, so presses outside never
// reached the picker. The grab now waits for the map, and holds the pointer
// against another application.
TEST(dropper_grab_waits_for_the_map_and_holds_the_pointer)
{
	if (!OnX11())
		return;
	OtherClient other;
	CHECK(other.Ok());
	Picker picker;
	picker.Window()->Show();
	// What OnShow does; the window is not mapped yet.
	picker.Grab().Take();
	if (!picker.Mapped()) {
		CHECK(!picker.Window()->HasCapture());
		CHECK_EQ(picker.Grab().Refusals(), 0);
	}
	CHECK(RunEventsUntil([&] { return picker.Grab().Holds(); }, 2000ms));
	CHECK_EQ(picker.Grab().Refusals(), 0);
	CHECK(!picker.Grab().Retrying());
	if (other.Ok())
		CHECK_EQ(other.GrabPointer(), AlreadyGrabbed);

	// Let go: the other application can have it.
	picker.Grab().Release();
	CHECK(!picker.Window()->HasCapture());
	if (other.Ok()) {
		CHECK_EQ(other.GrabPointer(), GrabSuccess);
		other.UngrabPointer();
	}
}

// Another application holds the pointer when the picker asks: the grab is
// refused, wx still reports the capture, and the grab is asked again every
// 50 ms until it holds.
TEST(dropper_grab_is_asked_again_while_refused)
{
	if (!OnX11())
		return;
	OtherClient other;
	CHECK(other.Ok());
	if (!other.Ok())
		return;
	Picker picker;
	picker.Window()->Show();
	CHECK(RunEventsUntil([&] { return picker.Mapped(); }, 2000ms));
	CHECK_EQ(other.GrabPointer(), GrabSuccess);
	picker.Grab().Take();
	CHECK(picker.Window()->HasCapture());
	CHECK(!picker.Grab().Holds());
	CHECK_EQ(picker.Grab().Refusals(), 1);
	CHECK(picker.Grab().Retrying());
	CHECK(RunEventsUntil([&] { return picker.Grab().Refusals() >= 3; }, 2000ms));
	CHECK(!picker.Grab().Holds());

	other.UngrabPointer();
	CHECK(RunEventsUntil([&] { return picker.Grab().Holds(); }, 2000ms));
	CHECK(!picker.Grab().Retrying());
	CHECK_EQ(other.GrabPointer(), AlreadyGrabbed);
}

// The pointer leaving the picker asks again also when wx reports the capture
// held, since wxGTK reports it held when X refused it.
TEST(dropper_grab_takes_again_a_capture_wx_reports_held)
{
	if (!OnX11())
		return;
	OtherClient other;
	CHECK(other.Ok());
	if (!other.Ok())
		return;
	Picker picker;
	picker.Window()->Show();
	CHECK(RunEventsUntil([&] { return picker.Mapped(); }, 2000ms));
	CHECK_EQ(other.GrabPointer(), GrabSuccess);
	// What the picker did before: wx's capture, refused by X.
	picker.Window()->CaptureMouse();
	CHECK(picker.Window()->HasCapture());
	CHECK(!picker.Grab().Holds());
	other.UngrabPointer();

	picker.Grab().Take();
	CHECK(picker.Grab().Holds());
	CHECK_EQ(other.GrabPointer(), AlreadyGrabbed);
}

// A pointer another application never lets go: the picker stops asking after
// RETRY_TRIES refusals instead of asking for ever.
TEST(dropper_grab_stops_asking_after_its_tries)
{
	if (!OnX11())
		return;
	OtherClient other;
	CHECK(other.Ok());
	if (!other.Ok())
		return;
	Picker picker;
	picker.Window()->Show();
	CHECK(RunEventsUntil([&] { return picker.Mapped(); }, 2000ms));
	CHECK_EQ(other.GrabPointer(), GrabSuccess);
	picker.Grab().Take();
	CHECK(RunEventsUntil([&] { return !picker.Grab().Retrying(); },
		std::chrono::milliseconds(4 * DropperPointerGrab::RETRY_TRIES * DropperPointerGrab::RETRY_INTERVAL_MS)));
	CHECK_EQ(picker.Grab().Refusals(), DropperPointerGrab::RETRY_TRIES);
	CHECK(!picker.Grab().Holds());
}

// Hidden while it asks again: the timer stops rather than grab the pointer
// for a hidden window.
TEST(dropper_grab_stops_asking_when_hidden)
{
	if (!OnX11())
		return;
	OtherClient other;
	CHECK(other.Ok());
	if (!other.Ok())
		return;
	Picker picker;
	picker.Window()->Show();
	CHECK(RunEventsUntil([&] { return picker.Mapped(); }, 2000ms));
	CHECK_EQ(other.GrabPointer(), GrabSuccess);
	picker.Grab().Take();
	CHECK(picker.Grab().Retrying());
	picker.Window()->Hide();
	other.UngrabPointer();
	CHECK(RunEventsUntil([&] { return !picker.Grab().Retrying(); }, 1000ms));
	CHECK(!picker.Grab().Holds());
	CHECK_EQ(other.GrabPointer(), GrabSuccess);
}
