#pragma once

// D3: floating panel windows draw their own chrome (DockFloatingWindow.qml)
// and ask the window system for none. Qt::FramelessWindowHint does that on
// Windows, X11 and macOS. On Wayland it only stops Qt from asking for a
// server-side frame: Qt creates no xdg-decoration object for a frameless
// window, and a compositor is free to frame a window that says nothing
// (sway does when a border rule matches). So the adapter asks the compositor
// itself, through zxdg_toplevel_decoration_v1.set_mode(client_side), and
// reads the mode it answers. Where the compositor still frames the window
// (server_side), the panel draws no shadow of its own inside that frame
// (DockFloatingWindow.qml through Docking.windowSystemFrame). Where that
// frame has a title bar, the title
// bar names the window, and the panel's header drops its own title so the
// name never shows twice (DockTabBar.qml, DockTitleBar.qml through
// Docking.windowSystemTitle). Not every server-side frame has one:
// Hyprland answers server_side to every window and draws only a border
// round it (native gate, 2026-10-13), so there the header keeps the name.

#include <QObject>
#include <QStringList>

#include <optional>

class QWindow;

namespace hikari::ui::windowdecorations {

// Asks the window system to leave `window` (a frameless tool window not
// shown yet) without a frame of its own, for as long as it lives; on
// Wayland whenever it is shown. Elsewhere this does nothing.
void requestNone(QWindow *window);

// What the window system draws round `window` itself.
enum class SystemFrame {
    None, // nothing: the window's own chrome is all there is
    Border, // a border only (Hyprland's server-side decorations)
    TitleBar, // a border and a title bar naming the window
};

// The frame of a compositor that answered server_side, by the Wayland
// globals it advertises: a title bar, except for the compositors known to
// draw only a border (Hyprland, whose globals are named hyprland_*).
SystemFrame serverSideFrame(const QStringList &globals);

// What the window system draws round `window`: on Wayland, the frame of a
// compositor that answered server_side to requestNone; elsewhere None.
SystemFrame systemFrame(const QWindow *window);
// Whether the window system draws its own title bar on `window`.
bool hasSystemTitleBar(const QWindow *window);

// Emits changed() when systemFrame changes for any window.
class Notifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void changed(QWindow *window);
};
Notifier *notifier();

// Tests: pretend the window system draws `frame` round `window`; nullopt
// asks it again.
void overrideSystemFrame(QWindow *window, std::optional<SystemFrame> frame);

} // namespace hikari::ui::windowdecorations
