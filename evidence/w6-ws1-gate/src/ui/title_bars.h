#pragma once

// K2 on Windows: the title bar Windows draws on a framed window (the main
// window, the dialogs and tools in windows of their own; floating panels are
// frameless) follows the theme, dark under Dark and High contrast black and
// light under the others. Qt makes a window's frame dark only while its own
// colour scheme is dark, and that follows the system unless the application
// asks for one (QStyleHints::setColorScheme). So the theme layer
// (theme.cpp), on Windows, asks Qt for the shown theme's side when the theme
// does not follow the system, and sets every framed window's title bar
// through here as well: when the window is created or shown, when the theme
// changes and when the application palette changes (Qt sets the frame again
// then). Other window
// systems draw their decorations from the desktop's own theme, and setDark
// does nothing there.

#include <functional>
#include <optional>

class QWindow;

namespace hikari::ui::titlebars {

// Whether the window system draws a title bar on `window` that the theme
// sets: a top-level window with a frame (not a popup, a tool tip, a splash
// screen or a frameless window).
bool themed(const QWindow *window);

// Sets the title bar of `window` dark or light: on Windows
// DWMWA_USE_IMMERSIVE_DARK_MODE, once the window has its native window;
// before that, and elsewhere, nothing.
void setDark(QWindow *window, bool dark);

// Whether the title bar of `window` is dark as the window system has it
// (Windows: DwmGetWindowAttribute); nullopt elsewhere or without a native
// window.
std::optional<bool> isDark(QWindow *window);

// Tests: `setter` takes setDark's calls instead of the window system
// (nullptr: the window system again).
using Setter = std::function<void(QWindow *window, bool dark)>;
void overrideSetter(Setter setter);

} // namespace hikari::ui::titlebars
