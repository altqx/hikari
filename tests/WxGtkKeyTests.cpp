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

// Key codes of wxGTK's key events, from GTK key presses made up here, so that
// the hardware code and the keysym can disagree the way they do with a virtual
// keyboard's keymap. Needs the pinned wxGTK build and an X display.

#include "check.h"
#include "WxGtkApp.h"

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/init.h>
#include <wx/window.h>

#include <gtk/gtk.h>

namespace {

// evdev + 8 hardware codes of the US layout.
constexpr guint16 kKeycodeEscape = 9;
constexpr guint16 kKeycode1 = 10;
constexpr guint16 kKeycode3 = 12;
constexpr guint16 kKeycodeBackspace = 22;
constexpr guint16 kKeycodeTab = 23;
constexpr guint16 kKeycodeReturn = 36;

struct KeyCodes {
	int charHook = -1;
	int keyDown = -1;
};

class KeyTarget
{
public:
	KeyTarget()
	{
		m_initialized = WxGtkAppReady();
		if (!m_initialized)
			return;
		m_frame = new wxFrame(nullptr, wxID_ANY, "keys");
		m_window = new wxWindow(m_frame, wxID_ANY);
		m_frame->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &evt) {
			m_codes.charHook = evt.GetKeyCode();
			evt.Skip();
		});
		m_window->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &evt) { m_codes.keyDown = evt.GetKeyCode(); });
		m_frame->Show();
		m_window->SetFocus();
		while (gtk_events_pending())
			gtk_main_iteration();
	}

	bool Ready() const { return m_initialized && m_window && gtk_widget_get_window(Widget()); }

	// What wx made of a press of the key with this hardware code, when the
	// current keymap puts this keysym on it.
	KeyCodes Press(guint keyval, guint16 hardwareKeycode)
	{
		m_codes = KeyCodes();
		GtkWidget *widget = Widget();
		GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
		event->key.window = GDK_WINDOW(g_object_ref(gtk_widget_get_window(widget)));
		event->key.send_event = TRUE;
		event->key.time = GDK_CURRENT_TIME;
		event->key.state = 0;
		event->key.keyval = keyval;
		event->key.hardware_keycode = hardwareKeycode;
		event->key.length = 0;
		event->key.string = g_strdup("");
		GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(widget));
		if (seat && gdk_seat_get_keyboard(seat))
			gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
		gtk_widget_event(widget, event);
		gdk_event_free(event);
		return m_codes;
	}

private:
	GtkWidget *Widget() const { return static_cast<GtkWidget *>(m_window->GetHandle()); }

	bool m_initialized = false;
	wxFrame *m_frame = nullptr;
	wxWindow *m_window = nullptr;
	KeyCodes m_codes;
};

// Made on first use and never destroyed: the process ends with the frame
// still up, rather than tearing wx down after main() has returned.
KeyTarget &Target()
{
	static KeyTarget *target = new KeyTarget;
	return *target;
}

} // namespace

TEST(wxgtk_key_target_is_ready)
{
	CHECK(Target().Ready());
}

TEST(wxgtk_digit_on_its_us_key_keeps_its_code)
{
	if (!Target().Ready())
		return;
	KeyCodes codes = Target().Press(GDK_KEY_3, kKeycode3);
	CHECK_EQ(codes.charHook, int('3'));
	CHECK_EQ(codes.keyDown, int('3'));
}

// wtype gives the first character it types the code 9, Escape in the US
// layout: typing "3" with it closed the Style manager.
TEST(wxgtk_digit_on_the_escape_code_is_not_escape)
{
	if (!Target().Ready())
		return;
	KeyCodes codes = Target().Press(GDK_KEY_3, kKeycodeEscape);
	CHECK(codes.charHook != WXK_ESCAPE);
	CHECK_EQ(codes.charHook, int('3'));
	CHECK_EQ(codes.keyDown, int('3'));
}

TEST(wxgtk_characters_on_control_key_codes_keep_their_own_codes)
{
	if (!Target().Ready())
		return;
	CHECK_EQ(Target().Press(GDK_KEY_period, kKeycodeTab).keyDown, int('.'));
	CHECK_EQ(Target().Press(GDK_KEY_5, kKeycodeReturn).keyDown, int('5'));
	CHECK_EQ(Target().Press(GDK_KEY_0, kKeycodeBackspace).keyDown, int('0'));
}

// What the US-layout lookup is for: the key that types "&" in the French
// layout is "1" in the US one, and still gives the code '1'.
TEST(wxgtk_non_letter_on_a_us_character_key_gets_the_us_code)
{
	if (!Target().Ready())
		return;
	CHECK_EQ(Target().Press(GDK_KEY_ampersand, kKeycode1).keyDown, int('1'));
}

TEST(wxgtk_real_escape_is_still_escape)
{
	if (!Target().Ready())
		return;
	KeyCodes codes = Target().Press(GDK_KEY_Escape, kKeycodeEscape);
	CHECK_EQ(codes.charHook, int(WXK_ESCAPE));
}
