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

// The wx application the wxGTK tests share, and a way to run its events.

#pragma once

#include <wx/app.h>
#include <wx/init.h>

#include <gtk/gtk.h>

#include <chrono>
#include <thread>

// Starts wx once, for every wxGTK test: whether it started. It is never torn
// down: the process ends with it up, rather than ending wx after main().
inline bool WxGtkAppReady()
{
	static const bool ready = [] {
		wxApp::SetInstance(new wxApp);
		static int argc = 1;
		static char arg0[] = "hikarisub_wxgtk_tests";
		static char *argv[] = { arg0, nullptr };
		return wxEntryStart(argc, argv) && wxTheApp->CallOnInit();
	}();
	return ready;
}

// Runs GTK's and wx's events until done() holds or the time is up: whether
// done() held.
template <typename Done>
bool RunEventsUntil(Done done, std::chrono::milliseconds limit)
{
	const auto end = std::chrono::steady_clock::now() + limit;
	for (;;) {
		while (gtk_events_pending())
			gtk_main_iteration_do(FALSE);
		if (wxTheApp)
			wxTheApp->ProcessPendingEvents();
		if (done())
			return true;
		if (std::chrono::steady_clock::now() >= end)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}
