#pragma once

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
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(int monitor READ monitor NOTIFY activeChanged)
    // MUST KILL: onAnotherMonitor means a non-zero monitor was asked for, even if it lands on the main window's
    Q_PROPERTY(bool onAnotherMonitor READ onAnotherMonitor NOTIFY activeChanged)
    // MUST KILL: barMode is a magic int (0 Auto, 1 Always, 2 Never)
    Q_PROPERTY(int barMode READ barMode WRITE setBarMode NOTIFY panelChanged)
    Q_PROPERTY(bool pinned READ pinned NOTIFY panelChanged)
    Q_PROPERTY(bool barShown READ barShown NOTIFY panelChanged)
    Q_PROPERTY(bool cursorHidden READ cursorHidden NOTIFY panelChanged)
    Q_PROPERTY(bool menuOpen READ menuOpen WRITE setMenuOpen NOTIFY panelChanged)
    Q_PROPERTY(bool pointerOverBar READ pointerOverBar WRITE setPointerOverBar NOTIFY panelChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
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

    Q_INVOKABLE void setWindows(QQuickWindow *fullscreen, QWindow *main);
    Q_INVOKABLE void setFocusFallback(QQuickItem *item) { m_focusFallback = item; }
    QQuickWindow *window() const;

    static QList<QScreen *> monitors();
    Q_INVOKABLE int monitorCount() const { return static_cast<int>(monitors().size()); }
    Q_INVOKABLE static bool ownedByMainWindow();
    Q_INVOKABLE int monitorFor(int monitor) const;

    Q_INVOKABLE bool toggle(int monitor = 0);
    Q_INVOKABLE bool showOn(int monitor);
    Q_INVOKABLE bool leave();
    // MUST KILL: enterWhenShown claims the next open request, so it must be called right before that openVideo
    Q_INVOKABLE void enterWhenShown(const QString &path);

    void setIdleInterval(int ms) { m_idle.setInterval(ms); }
    Q_INVOKABLE void cycleBarMode();
    // Qt Quick re-sends a still pointer's hover on each scene change; the same point again is no move.
    Q_INVOKABLE void pointerMoved(QPointF at);
    Q_INVOKABLE QString lineLabel(const QString &text) const;

    Q_INVOKABLE QVariantMap progressBar(qreal clientWidth, qreal textWidth, qreal textHeight) const;

signals:
    void activeChanged();
    void panelChanged();
    void progressChanged();

protected:
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
    QTimer m_idle;
    bool m_awake = false;
    std::optional<QPointF> m_pointer;
    bool m_menuOpen = false;
    bool m_pointerOverBar = false;
    bool m_barShown = false;
    bool m_cursorHidden = false;
    QPointer<QWindow> m_focusWindow;
    QPointer<QQuickItem> m_focusItem;
    QPointer<QQuickItem> m_focusFallback;
    QString m_pendingPath;
    std::uint64_t m_pendingRequest = 0;
    bool m_pendingOpening = false;
    std::uint64_t m_placing = 0;
};

}
