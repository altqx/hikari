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
    Q_PROPERTY(BarPosition barPosition READ barPosition WRITE setBarPosition NOTIFY panelChanged)
    Q_PROPERTY(bool stripReserved READ stripReserved NOTIFY panelChanged)
    Q_PROPERTY(bool overlays READ overlays NOTIFY panelChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool barShown READ barShown NOTIFY panelChanged)
    Q_PROPERTY(bool cursorHidden READ cursorHidden NOTIFY panelChanged)
    Q_PROPERTY(bool menuOpen READ menuOpen WRITE setMenuOpen NOTIFY panelChanged)
    Q_PROPERTY(bool pointerOverBar READ pointerOverBar WRITE setPointerOverBar NOTIFY panelChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY progressChanged)
    Q_PROPERTY(QString videoSize READ videoSize NOTIFY progressChanged)
    Q_PROPERTY(bool seeking READ seeking NOTIFY progressChanged)
    // -1: none.
    Q_PROPERTY(int failedFrame READ failedFrame NOTIFY progressChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY progressChanged)
    Q_PROPERTY(QString lengthText READ lengthText NOTIFY progressChanged)
public:
    enum class BarPosition { Below, Over };
    Q_ENUM(BarPosition)
    enum class Mode { Simple, Typesetting };
    Q_ENUM(Mode)

    VideoFullscreenController(VideoController &video, VideoViewController &view, SettingsStore &settings,
                              QObject *parent = nullptr);

    bool active() const { return m_active; }
    int monitor() const { return m_monitor; }
    bool onAnotherMonitor() const { return m_onAnotherMonitor; }
    int barMode() const;
    void setBarMode(int mode);
    BarPosition barPosition() const;
    void setBarPosition(BarPosition position);
    bool stripReserved() const;
    bool overlays() const;
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    bool barShown() const { return m_barShown; }
    bool cursorHidden() const { return m_cursorHidden; }
    bool menuOpen() const { return m_menuOpen; }
    void setMenuOpen(bool open);
    bool pointerOverBar() const { return m_pointerOverBar; }
    void setPointerOverBar(bool over);
    QString progressText() const;
    QString videoName() const;
    QString videoSize() const;
    bool seeking() const;
    int failedFrame() const;
    QString positionText() const;
    QString lengthText() const;
    Q_INVOKABLE QList<int> keyframeTicks(int width) const;

    Q_INVOKABLE void setWindows(QQuickWindow *fullscreen, QWindow *main);
    Q_INVOKABLE void setFocusFallback(QQuickItem *item) { m_focusFallback = item; }
    QQuickWindow *window() const;

    static QList<QScreen *> monitors();
    Q_INVOKABLE int monitorCount() const { return static_cast<int>(monitors().size()); }
    Q_INVOKABLE int monitorEntries() const;
    Q_INVOKABLE static bool ownedByMainWindow();
    Q_INVOKABLE int monitorFor(int monitor) const;

    Q_INVOKABLE bool toggle(int monitor = 0);
    Q_INVOKABLE bool toggleTypesetting();
    Q_INVOKABLE bool showOn(int monitor);
    Q_INVOKABLE bool showOnScreen(int screen);
    Q_INVOKABLE bool leave();
    // MUST KILL: enterWhenShown claims the next open request, so it must be called right before that openVideo
    Q_INVOKABLE void enterWhenShown(const QString &path);

    void setIdleInterval(int ms) { m_idle.setInterval(ms); }
    Q_INVOKABLE void cycleBarMode();
    // Qt Quick re-sends a still pointer's hover on each scene change; the same point again is no move.
    Q_INVOKABLE void pointerMoved(QPointF at);
    // Space plays and pauses without showing the strip.
    Q_INVOKABLE void keyPressed(int key);
    Q_INVOKABLE QString lineLabel(const QString &text) const;

    Q_INVOKABLE QVariantMap progressBar(qreal clientWidth, qreal textWidth, qreal textHeight) const;

signals:
    void activeChanged();
    void panelChanged();
    void modeChanged();
    void keyframesChanged();
    void progressChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool enter(int monitor);
    void place(QScreen *screen);
    void restoreFocus();
    void hideWindow();
    void updateBar();
    void wake();
    void checkKeyframes();

    VideoController &m_video;
    VideoViewController &m_view;
    SettingsStore &m_settings;
    QPointer<QQuickWindow> m_window;
    QPointer<QWindow> m_main;
    bool m_active = false;
    int m_monitor = -1;
    bool m_onAnotherMonitor = false;
    Mode m_mode = Mode::Simple;
    const int *m_keyframesData = nullptr;
    std::size_t m_keyframesSize = 0;
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
