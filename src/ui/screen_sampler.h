#pragma once

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace hikari::ui {

class ScreenSampler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the colour picker")
    Q_PROPERTY(QString route READ route NOTIFY routeChanged)
    Q_PROPERTY(bool available READ available NOTIFY routeChanged)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY routeChanged)
    Q_PROPERTY(bool tracking READ tracking NOTIFY trackingChanged)
    Q_PROPERTY(bool portalBusy READ portalBusy NOTIFY portalBusyChanged)
public:
    static constexpr int kSize = 7;
    static constexpr int kMagnification = 8;

    struct Route {
        QString name;
        QString reason;
    };
    // An X11 client in a Wayland session runs on XWayland, whose screen grab reads black.
    static Route routeFor(const QString &platformName, bool waylandSession, bool portalAvailable);
    static bool isWaylandSession();

    using Source = std::function<QImage(QPoint global)>;

    explicit ScreenSampler(QObject *parent = nullptr);
    ~ScreenSampler() override;

    QString route() const;
    bool available() const { return !route().isEmpty(); }
    QString unavailableReason() const;
    bool tracking() const { return !m_window.isNull(); }
    bool portalBusy() const { return m_portalBusy; }

    Q_INVOKABLE QVariantList sample(int x, int y) const;
    Q_INVOKABLE QPoint cursorPosition() const;
    Q_INVOKABLE QRect availableGeometryAt(int x, int y) const;
    Q_INVOKABLE void moveToPointer(QWindow *window, int x, int y) const;

    Q_INVOKABLE bool startTracking(QWindow *window, bool passInside);
    Q_INVOKABLE void stopTracking();
    bool holdsPointer() const { return m_grabbed; }
    bool regrabbing() const { return m_regrab.isActive(); }

    static constexpr int kRegrabInterval = 50;
    static constexpr int kRegrabTries = 40;

    // MUST KILL: pickFromPortal emits portalPicked/portalFailed before portalBusy falls, and listeners rely on that order
    Q_INVOKABLE void pickFromPortal();

    void setSource(Source source) { m_source = std::move(source); }
    void setRoute(const Route &route);
    using PortalRequest = std::function<void()>;
    void setPortalRequest(PortalRequest request) { m_portalRequest = std::move(request); }
    // The xdg-desktop-portal Request response: 0 success, 1 cancelled, 2 other.
    void answerPortal(uint response, double red = 0, double green = 0, double blue = 0);
    void failPortalCall(const QString &message);
    using Grab = std::function<bool(QWindow *window, bool grab)>;
    void setGrab(Grab grab) { m_grab = std::move(grab); }

signals:
    void routeChanged();
    void trackingChanged();
    void portalBusyChanged();
    // MUST KILL: pointerEvent's kind is a magic int (0 move, 1 press, 2 release)
    void pointerEvent(int kind, int x, int y, int button, int buttons, bool inside);
    void trackingLost();
    void portalPicked(const QVariantMap &colour);
    void portalFailed(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void portalResponse(uint response, const QVariantMap &results);

private:
    void resolveRoute() const;
    void takePointer(bool again);
    void showCursor(bool dropper);
    void finishPortal();

    Source m_source;
    mutable bool m_resolved = false;
    mutable Route m_route;
    QPointer<QWindow> m_window;
    bool m_passInside = false;
    Grab m_grab;
    bool m_grabbed = false;
    QTimer m_regrab;
    int m_regrabTries = 0;
    bool m_cursorShown = false;
    bool m_portalBusy = false;
    QString m_portalPath;
    int m_portalRequests = 0;
    PortalRequest m_portalRequest;
};

}
