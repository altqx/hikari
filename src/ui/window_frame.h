#pragma once

// SH1 (#220): the main window's own title bar (MainTitleBar.qml), after
// MuseScore 4's (docs/research/musescore-shell.md §1 and §7, SH1): the
// application icon, the menus, the layout switcher, the window title and
// the window buttons, drawn by HikariSub on a window the window system
// leaves without a frame. Where the system's title bar is kept (the "Use
// system title bar" setting, on by default on tiling window managers), the
// window keeps the system's frame and the menus and the switcher stay in the
// window's first row.
//
// This is the platform half:
// * which title bar a desktop gets by default (systemTitleBarByDefault):
//   ours on Windows, KDE Plasma and GNOME; the system's anywhere else, and
//   always on a tiling window manager, which is detected from the
//   compositor and the session, never from the decorations it offers (the
//   owner's decision of 2026-10-19);
// * the frame itself (MainWindowFrame): the frameless window, moving,
//   resizing and the window menu through the window system, and on Windows
//   the native frame MuseScore draws (WM_NCCALCSIZE, WM_NCHITTEST), which
//   keeps the drop shadow, Aero Snap, Win+arrows, the system menu and the
//   snap layouts of Windows 11 on the maximise button.

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

namespace hikari::ui {

namespace windowframe {

// "appearance.titleBar": "auto" (the desktop's default), "system" or "custom".
inline constexpr const char *kTitleBarSetting = "appearance.titleBar";

// What the default title bar depends on.
struct Environment {
    QString platform;        // QGuiApplication::platformName()
    QString currentDesktop;  // XDG_CURRENT_DESKTOP, ':'-separated
    QString sessionDesktop;  // XDG_SESSION_DESKTOP, else DESKTOP_SESSION
    QStringList markers;     // the tiling compositors' own variables that are set (SWAYSOCK, ...)
    QStringList waylandGlobals; // the interfaces the Wayland compositor advertises
};

enum class Desktop {
    Windows,
    Kde,
    Gnome,
    Tiling, // sway, Hyprland, niri, river, i3 and the other tiling window managers
    Other,  // any other window system on Wayland or X11
    Unsupported, // a platform where the window cannot go frameless (offscreen, minimal, ...)
};

Desktop classify(const Environment &environment);
Environment currentEnvironment();
Desktop currentDesktop();
// The system's title bar unless the desktop is Windows, KDE Plasma or GNOME.
bool systemTitleBarByDefault(Desktop desktop);
// Whether a window shows the system's title bar for the setting's value.
bool usesSystemTitleBar(const QString &setting, Desktop desktop);

// Tests: answer `desktop` instead of the running one (nullopt: the running one).
void overrideDesktop(std::optional<Desktop> desktop);

// The window system's side of a frame (window_frame_win.cpp on Windows;
// none elsewhere, where Qt's frameless window is the whole of it).
class NativeFrame;

} // namespace windowframe

// The title bar choice for QML (Main.qml, AppearancePage.qml).
class WindowFrame : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Whether the system's title bar is the default here.
    Q_PROPERTY(bool systemTitleBarByDefault READ systemTitleBarByDefault NOTIFY desktopChanged FINAL)
    // Whether the window can draw its own title bar here at all.
    Q_PROPERTY(bool customAvailable READ customAvailable NOTIFY desktopChanged FINAL)
    // Windows resizes the frameless window natively (MainWindowFrame's
    // WM_NCHITTEST), so the title bar draws no resize band of its own.
    Q_PROPERTY(bool nativeResize READ nativeResize CONSTANT FINAL)
    // The resize band inside a frameless window's edges (Linux).
    Q_PROPERTY(int resizeBand READ resizeBand CONSTANT FINAL)
public:
    explicit WindowFrame(QObject *parent = nullptr);

    bool systemTitleBarByDefault() const;
    bool customAvailable() const;
    static bool nativeResize();
    static int resizeBand();
    // Whether the main window keeps the system's title bar for the
    // setting's value ("auto", "system", "custom").
    Q_INVOKABLE bool usesSystemTitleBar(const QString &setting) const;
    // Whether `window` is hidden only while its frame is made again (on
    // Wayland, MainWindowFrame::apply): no closing.
    Q_INVOKABLE static bool remaking(QWindow *window);

    // Tests: the desktop changed (windowframe::overrideDesktop).
    static void notifyDesktopChanged();

signals:
    void desktopChanged();
};

// The main window's frame while it draws its own title bar.
class MainWindowFrame : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    // Ours (frameless, MainTitleBar.qml) or the system's.
    Q_PROPERTY(bool custom READ custom WRITE setCustom NOTIFY customChanged FINAL)
    // The title bar; its free parts move the window (hitAt).
    Q_PROPERTY(QQuickItem *titleBar READ titleBar WRITE setTitleBar NOTIFY itemsChanged FINAL)
    // The title bar's background area that takes the pointer for moving on
    // Linux; on Windows the window system moves the window from there.
    Q_PROPERTY(QQuickItem *moveArea READ moveArea WRITE setMoveArea NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QQuickItem *maximizeButton READ maximizeButton WRITE setMaximizeButton NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QQuickItem *windowMenuButton READ windowMenuButton WRITE setWindowMenuButton NOTIFY itemsChanged FINAL)
    // The maximise button's pointer state where the window system tracks it
    // (Windows: the button is the frame's, for the snap layouts).
    Q_PROPERTY(bool maximizeHovered READ maximizeHovered NOTIFY maximizeStateChanged FINAL)
    Q_PROPERTY(bool maximizePressed READ maximizePressed NOTIFY maximizeStateChanged FINAL)
    // Alt held down (the menus underline their letters while it is, MS1 §1
    // "Menu bar"); a press and release of Alt alone is altTapped.
    Q_PROPERTY(bool altHeld READ altHeld NOTIFY altHeldChanged FINAL)
    // Whether the window can be resized and maximised (a dialog's choice,
    // DialogFrame; the main window always can).
    Q_PROPERTY(bool resizable READ resizable WRITE setResizable NOTIFY resizableChanged FINAL)
    // PU7 (#309): whether the window system's frame offers Close (a
    // dialog's choice: false for a wait it cannot end).
    Q_PROPERTY(bool closable READ closable WRITE setClosable NOTIFY closableChanged FINAL)
    // A dialog's frame (DialogFrame): no Minimise, and no Alt handling.
    Q_PROPERTY(bool dialog READ dialog CONSTANT FINAL)
public:
    enum Hit {
        Client,           // a control: the pointer is the window's
        Caption,          // a free part of the title bar: moves, double-click maximises
        MaximizeButton,   // the maximise button (Windows: the snap layouts)
        WindowMenuButton, // the application icon: the window menu
    };
    Q_ENUM(Hit)

    explicit MainWindowFrame(QObject *parent = nullptr);
    ~MainWindowFrame() override;

    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    bool custom() const { return m_custom; }
    void setCustom(bool custom);
    QQuickItem *titleBar() const { return m_titleBar; }
    void setTitleBar(QQuickItem *item);
    QQuickItem *moveArea() const { return m_moveArea; }
    void setMoveArea(QQuickItem *item);
    QQuickItem *maximizeButton() const { return m_maximizeButton; }
    void setMaximizeButton(QQuickItem *item);
    QQuickItem *windowMenuButton() const { return m_windowMenuButton; }
    void setWindowMenuButton(QQuickItem *item);
    bool maximizeHovered() const { return m_maximizeHovered; }
    bool maximizePressed() const { return m_maximizePressed; }
    bool altHeld() const { return m_altHeld; }
    bool resizable() const { return m_resizable; }
    void setResizable(bool resizable);
    bool closable() const { return m_closable; }
    void setClosable(bool closable);
    bool dialog() const { return m_dialog; }

    // What the title bar has at `position` (window coordinates): a control
    // is any item that takes mouse buttons, other than the move area.
    Q_INVOKABLE Hit hitAt(QPointF position) const;
    // The window system's window menu at `position` (window coordinates):
    // the system menu on Windows, the compositor's on Wayland
    // (xdg_toplevel.show_window_menu) and the window manager's on X11
    // (_GTK_SHOW_WINDOW_MENU). False where there is none.
    Q_INVOKABLE bool showWindowMenu(QPointF position);
    // Maximised to normal, anything else to maximised.
    Q_INVOKABLE void toggleMaximized();

    // Windows: the pointer state of the maximise button (MainWindowFrame's
    // native filter, window_frame_win.cpp).
    void setMaximizeState(bool hovered, bool pressed);

signals:
    void windowChanged();
    void customChanged();
    void itemsChanged();
    void maximizeStateChanged();
    // Tests: showWindowMenu was asked (whether or not the system has one).
    void windowMenuRequested(QPointF position);
    void altHeldChanged();
    void resizableChanged();
    void closableChanged();
    // Alt pressed and released with nothing between: the menus take the
    // keyboard. The release goes no further (on Windows Qt's menu bar would
    // take it too, and the system would enter its menu mode).
    void altTapped();

protected:
    // A dialog's frame (DialogFrame).
    struct DialogKind {};
    MainWindowFrame(DialogKind, QObject *parent);
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void apply();
    void setAltHeld(bool held);

    QPointer<QQuickWindow> m_window;
    bool m_custom = false;
    bool m_applied = false;
    QPointer<QQuickItem> m_titleBar;
    QPointer<QQuickItem> m_moveArea;
    QPointer<QQuickItem> m_maximizeButton;
    QPointer<QQuickItem> m_windowMenuButton;
    bool m_maximizeHovered = false;
    bool m_maximizePressed = false;
    bool m_altHeld = false;
    bool m_altAlone = false;
    bool m_resizable = true;
    bool m_closable = true;
    const bool m_dialog = false;
    std::unique_ptr<windowframe::NativeFrame> m_native;
};

// DG1 (#222): a dialog's frame (DialogWindow.qml), the main window's (SH1)
// with a dialog's buttons: Close, and Maximise only while the dialog can be
// resized (`resizable`); never Minimise. The window is a dialog to the
// window system as well: on Windows no minimise box, and no sizing frame or
// maximise box while it cannot be resized, so Aero Snap and the system menu
// leave a fixed-size dialog its size; on Wayland and X11 the resize band
// (MainWindowEdges) only while it can be resized. Alt is the main window's
// (its menus), not a dialog's.
class DialogFrame : public MainWindowFrame {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit DialogFrame(QObject *parent = nullptr);
};

} // namespace hikari::ui
