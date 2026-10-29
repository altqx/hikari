#pragma once

// V5 (#184): the video's fullscreen, as legacy VideoBox::SetFullscreen and
// its Fullscreen frame had it at 20d647c4 (HikariSub/VideoBox.cpp:741-855,
// VideoFullscreen.cpp).
//
// The fullscreen window is a separate top-level window (docs/docking.md:
// "Fullscreen video is a separate presentation surface, not a dock group")
// that the Video panel's picture, visual tools and zoom move into. This
// controller owns its state: whether it is shown and on which monitor
// (GetMonitorRect1), the bar at its bottom (FS1 #275: its mode and when it
// and the pointer hide), the progress bar's text, and what leaving gives
// back: the keyboard focus to the item that had it and the activation to its
// window. The main window's own geometry and state are never touched.

#include <QObject>
#include <cstdint>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

class QScreen;

namespace hikari::ui {

class SettingsStore;
class VideoController;
class VideoViewController;

class VideoFullscreenController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // Legacy m_IsFullscreen.
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // The monitor fullscreen is on, an index of monitors() (primary first).
    Q_PROPERTY(int monitor READ monitor NOTIFY activeChanged)
    // SetFullscreen's argument was another monitor than 0 (legacy
    // m_IsOnAnotherMonitor: the docked video is hidden meanwhile).
    Q_PROPERTY(bool onAnotherMonitor READ onAnotherMonitor NOTIFY activeChanged)
    // FS1 (#275): the bar's mode, application::FullscreenBarMode (0 Auto,
    // 1 Always, 2 Never), the setting video.fullscreenBar (FS1-bar-mode).
    Q_PROPERTY(int barMode READ barMode WRITE setBarMode NOTIFY panelChanged)
    // Always: the bar stays and the picture ends above it (legacy "Show
    // toolbar", m_PanelOnFullscreen).
    Q_PROPERTY(bool pinned READ pinned NOTIFY panelChanged)
    // The bar is shown: pinned, or in Auto while something holds it
    // (FS1-bar-auto-hide).
    Q_PROPERTY(bool barShown READ barShown NOTIFY panelChanged)
    // The pointer is hidden over the picture: nothing held the bar for
    // kFullscreenBarIdleMs (FS1-cursor-hide), in every mode.
    Q_PROPERTY(bool cursorHidden READ cursorHidden NOTIFY panelChanged)
    // What else holds the bar, from the window: one of its menus is open,
    // the pointer is over the bar.
    Q_PROPERTY(bool menuOpen READ menuOpen WRITE setMenuOpen NOTIFY panelChanged)
    Q_PROPERTY(bool pointerOverBar READ pointerOverBar WRITE setPointerOverBar NOTIFY panelChanged)
    // The progress bar's "<time> / <duration>" (RefreshTime).
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    // The video's file name (Fullscreen's Videolabel, tab->VideoName).
    Q_PROPERTY(QString videoName READ videoName NOTIFY progressChanged)
public:
    VideoFullscreenController(VideoController &video, VideoViewController &view, SettingsStore &settings,
                              QObject *parent = nullptr);

    bool active() const { return m_active; }
    int monitor() const { return m_monitor; }
    bool onAnotherMonitor() const { return m_onAnotherMonitor; }
    int barMode() const;
    void setBarMode(int mode);
    bool pinned() const;
    bool barShown() const { return m_barShown; }
    bool cursorHidden() const { return m_cursorHidden; }
    bool menuOpen() const { return m_menuOpen; }
    void setMenuOpen(bool open);
    bool pointerOverBar() const { return m_pointerOverBar; }
    void setPointerOverBar(bool over);
    QString progressText() const;
    QString videoName() const;

    // The fullscreen window (Main's VideoFullscreen) and the main window.
    Q_INVOKABLE void setWindows(QQuickWindow *fullscreen, QWindow *main);
    // What takes the keyboard on leaving when what had it is gone or hidden,
    // or was a menu (the context menu's entries): the Video panel.
    Q_INVOKABLE void setFocusFallback(QQuickItem *item) { m_focusFallback = item; }
    QQuickWindow *window() const;

    // The monitors (GetMonitorRect1's list: Qt's screens, the primary first), and how many.
    static QList<QScreen *> monitors();
    Q_INVOKABLE int monitorCount() const { return static_cast<int>(monitors().size()); }
    // Whether the fullscreen window has the main window as its transient
    // parent on this platform (not on Wayland: V5-wayland-parent).
    Q_INVOKABLE static bool ownedByMainWindow();
    // The monitor SetFullscreen(monitor) takes with the main window where it is.
    Q_INVOKABLE int monitorFor(int monitor) const;

    // VideoBox::SetFullscreen(monitor): leaves fullscreen when it is shown,
    // else shows it on `monitor` (0: the main window's). Nothing without a
    // video (`if (!renderer) return`). True when the state changed.
    Q_INVOKABLE bool toggle(int monitor = 0);
    // The context menu's monitors (VideoBox.cpp:1053-1056: m_IsFullscreen =
    // false, then SetFullscreen(i)): shown, or moved, on monitor `monitor`.
    Q_INVOKABLE bool showOn(int monitor);
    // Leaving (SetFullscreen with m_IsFullscreen set).
    Q_INVOKABLE bool leave();
    // video.fullScreenOnStart (OpenFile(path, fulls), LoadVideo's `if
    // (fulls) SetFullscreen()`): fullscreen once `path` is shown by the next
    // openVideo, which the caller makes right after this. Like legacy's
    // argument it lives for that one open only: another open, another video
    // shown, or the open failing or cancelled drops it.
    Q_INVOKABLE void enterWhenShown(const QString &path);

    // kFullscreenBarIdleMs; tests shorten it.
    void setIdleInterval(int ms) { m_idle.setInterval(ms); }
    // VIDEO_FULLSCREEN_BAR: Auto, then Always, then Never, then Auto.
    Q_INVOKABLE void cycleBarMode();
    // A pointer move over the fullscreen window with no button held, at
    // `at` in the window: the bar (in Auto) and the pointer show for
    // kFullscreenBarIdleMs more. Qt Quick sends a still pointer a hover each
    // frame the scene changes (a playing video): the same point again is no
    // move.
    Q_INVOKABLE void pointerMoved(QPointF at);
    // FS1-current-line: a Line's text as the bar's label shows it.
    Q_INVOKABLE QString lineLabel(const QString &text) const;

    // RendererVideo::DrawProgressBar's rectangles for a `clientWidth`-wide
    // window and the text's extent: {frame, inner, bar: {x1, x2, y, width},
    // text} with x, y, width, height (rects).
    Q_INVOKABLE QVariantMap progressBar(qreal clientWidth, qreal textWidth, qreal textHeight) const;

signals:
    void activeChanged();
    void panelChanged();
    void progressChanged();

protected:
    // The pointer leaving the fullscreen window: a return at the same point is a move.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool enter(int monitor);
    void place(QScreen *screen);
    void restoreFocus();
    void hideWindow();
    void updateBar();

    VideoController &m_video;
    VideoViewController &m_view;
    SettingsStore &m_settings;
    QPointer<QQuickWindow> m_window;
    QPointer<QWindow> m_main;
    bool m_active = false;
    int m_monitor = -1;
    bool m_onAnotherMonitor = false;
    QTimer m_idle;        // kFullscreenBarIdleMs after the last move
    bool m_awake = false; // a move less than that ago
    std::optional<QPointF> m_pointer; // where the last move was, in the window (none once it left)
    bool m_menuOpen = false;
    bool m_pointerOverBar = false;
    bool m_barShown = false;
    bool m_cursorHidden = false;
    // What had the keyboard before fullscreen (given back on leaving).
    QPointer<QWindow> m_focusWindow;
    QPointer<QQuickItem> m_focusItem;
    QPointer<QQuickItem> m_focusFallback;
    QString m_pendingPath; // enterWhenShown
    std::uint64_t m_pendingRequest = 0; // the open it belongs to
    bool m_pendingOpening = false;
    std::uint64_t m_placing = 0; // place()'s calls: a later one drops an earlier one's second step
};

} // namespace hikari::ui
