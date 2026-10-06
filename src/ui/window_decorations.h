#pragma once

// D3: floating panel windows draw their own chrome (DockFloatingWindow.qml)
// and ask the window system for none. Qt::FramelessWindowHint does that on
// Windows, X11 and macOS. On Wayland it only stops Qt from asking for a
// server-side frame: Qt creates no xdg-decoration object for a frameless
// window, and a compositor is free to frame a window that says nothing
// (sway does when a border rule matches). So the adapter asks the compositor
// itself, through zxdg_toplevel_decoration_v1.set_mode(client_side), and
// reads the mode it answers. Where the compositor still frames the window
// (server_side), its title bar names the window, and the panel's header
// drops its own title so the name never shows twice (DockTabBar.qml,
// DockTitleBar.qml through Docking.windowSystemTitle).

#include <QObject>

#include <optional>

class QWindow;

namespace hikari::ui::windowdecorations {

// Asks the window system to leave `window` (a frameless tool window not
// shown yet) without a frame of its own, for as long as it lives; on
// Wayland whenever it is shown. Elsewhere this does nothing.
void requestNone(QWindow *window);

// Whether the window system draws its own title bar on `window`: on Wayland,
// a compositor that answered server_side to requestNone.
bool hasSystemTitleBar(const QWindow *window);

// Emits changed() when hasSystemTitleBar changes for any window.
class Notifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void changed(QWindow *window);
};
Notifier *notifier();

// Tests: pretend the window system does (true) or does not (false) frame
// `window`; nullopt asks it again.
void overrideSystemTitleBar(QWindow *window, std::optional<bool> framed);

} // namespace hikari::ui::windowdecorations
