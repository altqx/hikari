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

#include <wx/cursor.h>
#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/timer.h>

class wxWindow;

// The pointer of the colour pickers' screen eyedroppers: their cursor, and the
// simple picker's hold on the pointer while it picks from the screen.

// The tip of the dropper in the 32x32 eyedropper picture, the hotspot of
// Bitmaps/eyedropper.cur.
constexpr int DROPPER_HOTSPOT_X = 4;
constexpr int DROPPER_HOTSPOT_Y = 25;

// The eyedropper cursor made from the eyedropper picture (Bitmaps/eyedropper.png,
// the EYEDROPPER_TOOL resource). Windows has the cursor itself as a resource;
// wxGTK takes a cursor's name for the path of a file, so elsewhere the cursor is
// made from the picture, with the same hotspot.
wxCursor DropperCursorFromImage(wxImage image);

// The pixels the screen shows in area (screen coordinates), with black where
// the area is off the screen. An invalid image where the screen cannot be read
// this way: everywhere but X11. wxScreenDC cannot stand in for it on wxGTK 3,
// whose screen DC reads only the root window's own pixels, without the
// windows over it, so a pick from it was black.
wxImage ReadScreenArea(const wxRect &area);

// Holds the pointer for a top-level window, so that presses outside it come to
// it. wxWindow::CaptureMouse does not say whether it worked, and wxGTK reports
// the capture held although GTK refused it: X grants a grab only on a window
// that is viewable, which a top-level window is not before the window manager
// maps it, and on X11 another client can hold the pointer. So here a grab is
// asked for only once the window is viewable (it is asked again when the window
// is mapped), it counts as held only when GDK holds it, and a refused grab is
// asked again every RETRY_INTERVAL_MS, at most RETRY_TRIES times.
class DropperPointerGrab
{
public:
	static constexpr int RETRY_INTERVAL_MS = 50;
	static constexpr int RETRY_TRIES = 40;

	explicit DropperPointerGrab(wxWindow *window);
	~DropperPointerGrab();
	DropperPointerGrab(const DropperPointerGrab &) = delete;
	DropperPointerGrab &operator=(const DropperPointerGrab &) = delete;

	// Asks for the pointer unless it is held, releasing first a capture wx
	// reports held when it is not. Waits, without asking, while the window is
	// not viewable. Call when the window is shown and when the pointer leaves
	// it for outside it.
	void Take();
	// Lets the pointer go and stops asking.
	void Release();
	// Whether the window holds the pointer, as the platform says.
	bool Holds() const;
	// Whether a refused grab is being asked again.
	bool Retrying() const { return m_retry.IsRunning(); }
	// The grabs asked for and refused since construction or the last Release.
	int Refusals() const { return m_refusals; }

private:
	class RetryTimer : public wxTimer
	{
	public:
		explicit RetryTimer(DropperPointerGrab &grab) : m_grab(grab) {}
		void Notify() override;

	private:
		DropperPointerGrab &m_grab;
	};

	bool Viewable() const;

	wxWindow *m_window;
	RetryTimer m_retry;
	int m_refusals = 0;
	unsigned long m_mapHandler = 0;
};
