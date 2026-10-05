#pragma once

// V5 (#184): the video's fullscreen, as legacy VideoBox::SetFullscreen and
// its Fullscreen frame had it at 20d647c4 (HikariSub/VideoBox.cpp:741-855,
// VideoFullscreen.cpp).
//
// The fullscreen window is a separate top-level window (docs/qt/docking.md:
// "Fullscreen video is a separate presentation surface, not a dock group")
// that the Video panel's picture, visual tools and zoom move into. This
// controller owns its state: whether it is shown and on which monitor
// (GetMonitorRect1), the panel at its bottom ("Show toolbar", legacy
// m_PanelOnFullscreen, and the panel's showing under the pointer), the
// progress bar's text, and what leaving gives back: the keyboard focus to
// the item that had it and the activation to its window. The main window's
// own geometry and state are never touched.

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>
#include <QRect>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QScreen;

namespace hikari::ui {

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
    // "Show toolbar": the panel stays and the toolbar row shows
    // (m_PanelOnFullscreen; checked for an indexed video, Fullscreen's
    // constructor, VideoFullscreen.cpp:39-46, 69, 81).
    Q_PROPERTY(bool showToolbar READ showToolbar WRITE setShowToolbar NOTIFY panelChanged)
    // The panel is shown (pinned, or the pointer went below the video).
    Q_PROPERTY(bool panelShown READ panelShown NOTIFY panelChanged)
    // The progress bar's "<time> / <duration>" (RefreshTime).
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    // The video's file name (Fullscreen's Videolabel, tab->VideoName).
    Q_PROPERTY(QString videoName READ videoName NOTIFY progressChanged)
public:
    VideoFullscreenController(VideoController &video, VideoViewController &view, QObject *parent = nullptr);

    bool active() const { return m_active; }
    int monitor() const { return m_monitor; }
    bool onAnotherMonitor() const { return m_onAnotherMonitor; }
    bool showToolbar() const { return m_showToolbar; }
    void setShowToolbar(bool on);
    bool panelShown() const { return m_panelShown; }
    QString progressText() const;
    QString videoName() const;

    // The fullscreen window (Main's VideoFullscreen) and the main window.
    Q_INVOKABLE void setWindows(QQuickWindow *fullscreen, QWindow *main);
    // What takes the keyboard on leaving when what had it is gone or hidden,
    // or was a menu (the context menu's entries): the Video panel.
    Q_INVOKABLE void setFocusFallback(QQuickItem *item) { m_focusFallback = item; }
    QQuickWindow *window() const;

    // The monitors, primary first (MonitorEnumProc1), and how many.
    static QList<QScreen *> monitors();
    Q_INVOKABLE int monitorCount() const { return static_cast<int>(monitors().size()); }
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
    // (fulls) SetFullscreen()`): fullscreen once `path` is shown. Another
    // video shown, or the open failing, drops the request.
    Q_INVOKABLE void enterWhenShown(const QString &path);

    // A pointer move over the fullscreen video at `y` of a `height`-high
    // window whose panel is `panelHeight` high (VideoBox::OnMouseEvent): the
    // panel shows or hides. True when hiding gave the video the keyboard.
    Q_INVOKABLE bool pointerMoved(qreal y, qreal height, qreal panelHeight);

    // RendererVideo::DrawProgressBar's rectangles for a `clientWidth`-wide
    // window and the text's extent: {frame, inner, bar: {x1, x2, y, width},
    // text} with x, y, width, height (rects).
    Q_INVOKABLE QVariantMap progressBar(qreal clientWidth, qreal textWidth, qreal textHeight) const;

signals:
    void activeChanged();
    void panelChanged();
    void progressChanged();

private:
    bool enter(int monitor);
    void place(QScreen *screen);
    void restoreFocus();

    VideoController &m_video;
    VideoViewController &m_view;
    QPointer<QQuickWindow> m_window;
    QPointer<QWindow> m_main;
    bool m_active = false;
    int m_monitor = -1;
    bool m_onAnotherMonitor = false;
    bool m_showToolbar = true;
    bool m_panelShown = true;
    // What had the keyboard before fullscreen (given back on leaving).
    QPointer<QWindow> m_focusWindow;
    QPointer<QQuickItem> m_focusItem;
    QPointer<QQuickItem> m_focusFallback;
    QString m_pendingPath; // enterWhenShown
    bool m_pendingOpening = false;
};

} // namespace hikari::ui
