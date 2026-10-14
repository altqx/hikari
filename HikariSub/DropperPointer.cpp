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

#include "DropperPointer.h"

#include <wx/imagbmp.h>
#include <wx/log.h>
#include <wx/window.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

// wxLogTrace mask for the asks for the pointer (WXTRACE=hikari-dropper).
#define DROPPER_TRACE "hikari-dropper"

wxCursor DropperCursorFromImage(wxImage image)
{
	if (!image.IsOk())
		return wxCursor(wxCURSOR_CROSS);
	image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, DROPPER_HOTSPOT_X);
	image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, DROPPER_HOTSPOT_Y);
	return wxCursor(image);
}

#ifdef __WXGTK__
namespace {
	// One channel of an X pixel, as 0-255.
	unsigned char Channel(unsigned long pixel, unsigned long mask)
	{
		if (!mask)
			return 0;
		int shift = 0;
		while (!(mask & (1ul << shift)))
			++shift;
		const unsigned long max = mask >> shift;
		return (unsigned char)(((pixel & mask) >> shift) * 255 / max);
	}
}

wxImage ReadScreenArea(const wxRect &area)
{
	GdkDisplay *display = gdk_display_get_default();
	if (!display || !GDK_IS_X11_DISPLAY(display) || area.IsEmpty())
		return wxImage();
	Display *xdisplay = gdk_x11_display_get_xdisplay(display);
	Window root = DefaultRootWindow(xdisplay);
	XWindowAttributes attributes;
	if (!XGetWindowAttributes(xdisplay, root, &attributes))
		return wxImage();
	wxImage image(area.width, area.height);
	const wxRect visible = area.Intersect(wxRect(0, 0, attributes.width, attributes.height));
	if (visible.IsEmpty())
		return image;
	// XGetImage of the root window gives what the screen shows, the windows
	// over it included.
	gdk_x11_display_error_trap_push(display);
	XImage *pixels = XGetImage(xdisplay, root, visible.x, visible.y, visible.width, visible.height,
		AllPlanes, ZPixmap);
	gdk_x11_display_error_trap_pop_ignored(display);
	if (!pixels)
		return wxImage();
	for (int y = 0; y < visible.height; ++y) {
		for (int x = 0; x < visible.width; ++x) {
			const unsigned long pixel = XGetPixel(pixels, x, y);
			image.SetRGB(visible.x - area.x + x, visible.y - area.y + y, Channel(pixel, pixels->red_mask),
				Channel(pixel, pixels->green_mask), Channel(pixel, pixels->blue_mask));
		}
	}
	XDestroyImage(pixels);
	return image;
}

namespace {
	GdkWindow *TopLevelGdkWindow(wxWindow *window)
	{
		GtkWidget *widget = window->GetHandle();
		return widget ? gtk_widget_get_window(widget) : nullptr;
	}

	gboolean OnMapEvent(GtkWidget *, GdkEvent *, gpointer grab)
	{
		static_cast<DropperPointerGrab *>(grab)->Take();
		return FALSE;
	}
}
#endif

#ifndef __WXGTK__
wxImage ReadScreenArea(const wxRect &)
{
	return wxImage();
}
#endif

DropperPointerGrab::DropperPointerGrab(wxWindow *window)
	: m_window(window)
	, m_retry(*this)
{
#ifdef __WXGTK__
	// The window becomes viewable when the window manager maps it, after
	// wxEVT_SHOW: ask for the pointer then.
	if (GtkWidget *widget = window->GetHandle())
		m_mapHandler = g_signal_connect(widget, "map-event", G_CALLBACK(OnMapEvent), this);
#endif
}

DropperPointerGrab::~DropperPointerGrab()
{
	m_retry.Stop();
#ifdef __WXGTK__
	if (m_mapHandler)
		g_signal_handler_disconnect(m_window->GetHandle(), m_mapHandler);
#endif
	if (m_window->HasCapture())
		m_window->ReleaseMouse();
}

bool DropperPointerGrab::Viewable() const
{
	if (!m_window->IsShown())
		return false;
#ifdef __WXGTK__
	// GDK refuses the grab (with a critical warning) on a window that is not
	// mapped yet, and X on one that is not viewable.
	GdkWindow *gdkWindow = TopLevelGdkWindow(m_window);
	return gdkWindow && gdk_window_is_visible(gdkWindow) && gdk_window_is_viewable(gdkWindow);
#else
	// Windows captures the mouse for a window as soon as it is shown.
	return true;
#endif
}

bool DropperPointerGrab::Holds() const
{
	if (!m_window->HasCapture())
		return false;
#ifdef __WXGTK__
	// wx sets its capture whatever GDK answered; GDK registers only a grab
	// the display granted.
	GdkWindow *gdkWindow = TopLevelGdkWindow(m_window);
	if (!gdkWindow)
		return false;
	GdkDisplay *display = gdk_window_get_display(gdkWindow);
	GdkDevice *pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
	return pointer && gdk_display_device_is_grabbed(display, pointer);
#else
	return true;
#endif
}

void DropperPointerGrab::Take()
{
	if (!Viewable()) {
		wxLogTrace(DROPPER_TRACE, "pointer grab waits: window not viewable");
		return;
	}
	if (Holds()) {
		m_retry.Stop();
		return;
	}
	if (m_window->HasCapture())
		m_window->ReleaseMouse();
	m_window->CaptureMouse();
	const bool held = Holds();
	wxLogTrace(DROPPER_TRACE, "pointer grab %s", held ? "held" : "refused");
	if (held) {
		m_retry.Stop();
		return;
	}
	++m_refusals;
	if (m_refusals >= RETRY_TRIES)
		m_retry.Stop();
	else if (!m_retry.IsRunning())
		m_retry.Start(RETRY_INTERVAL_MS);
}

void DropperPointerGrab::Release()
{
	m_retry.Stop();
	m_refusals = 0;
	if (m_window->HasCapture())
		m_window->ReleaseMouse();
}

void DropperPointerGrab::RetryTimer::Notify()
{
	// Hidden meanwhile: the window asks again when it is shown and mapped.
	if (!m_grab.Viewable()) {
		Stop();
		return;
	}
	m_grab.Take();
}
