#pragma once

// Y7: the colour picker's screen dropper (legacy ColorPickerScreenDropper,
// DialogColorPicker::OnDropperMouse and SimpleColorPickerDialog at
// 20d647c4). Legacy blits 7x7 screen pixels around the pointer into the
// dropper (DropFromScreenXY) while it holds the mouse capture, and picks a
// pixel of that capture (SendGetColorEvent).
//
// Routes: "grab" reads the screen through QScreen::grabWindow (Windows,
// X11); "portal" asks the desktop's Screenshot portal to pick one colour
// (Wayland, where a client cannot read the screen); without either the
// sampler reports itself unavailable with the reason, and samples nothing
// (legacy's Wayland build returned early: ColorPicker.cpp:429-434).

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QPointer>
#include <QString>
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
    // The dropper's capture: 7x7 pixels (legacy resx, resy), shown 8 times
    // larger (magnification).
    static constexpr int kSize = 7;
    static constexpr int kMagnification = 8;

    struct Route {
        QString name; // "grab", "portal" or empty
        QString reason; // why sampling is unavailable, when it is
    };
    // The route for a Qt platform plugin, whether the session is a Wayland
    // one (an X11 client there runs on XWayland, whose root window shows no
    // Wayland client: its grab would read black) and whether the desktop
    // offers the Screenshot portal (version 2, PickColor).
    static Route routeFor(const QString &platformName, bool waylandSession, bool portalAvailable);
    // WAYLAND_DISPLAY is set, or XDG_SESSION_TYPE is "wayland".
    static bool isWaylandSession();

    // `pixels` grabs the kSize x kSize pixels centred on a global point
    // (the default reads the screen).
    using Source = std::function<QImage(QPoint global)>;

    explicit ScreenSampler(QObject *parent = nullptr);
    ~ScreenSampler() override;

    QString route() const;
    bool available() const { return !route().isEmpty(); }
    QString unavailableReason() const;
    bool tracking() const { return !m_window.isNull(); }
    bool portalBusy() const { return m_portalBusy; }

    // DropFromScreenXY: the capture around the global point, row by row,
    // {r, g, b} each; empty unless the route is "grab".
    Q_INVOKABLE QVariantList sample(int x, int y) const;
    // The pointer's global position.
    Q_INVOKABLE QPoint cursorPosition() const;
    // The available area of the screen at a global point (legacy
    // MoveToMousePosition's display client area), or the primary screen's.
    Q_INVOKABLE QRect availableGeometryAt(int x, int y) const;

    // Legacy CaptureMouse: `window` grabs the pointer, and its mouse events
    // come as pointerEvent with global positions. With `passInside` the
    // events over the window itself still reach its controls (the simple
    // picker releases the capture over itself), otherwise every event is
    // the dropper's. The eyedropper cursor shows while the pointer is
    // outside (or everywhere without `passInside`).
    Q_INVOKABLE bool startTracking(QWindow *window, bool passInside);
    Q_INVOKABLE void stopTracking();

    // The portal route: the desktop picks one colour; portalPicked or,
    // unless the user cancelled, portalFailed answers.
    Q_INVOKABLE void pickFromPortal();

    // Tests: another pixel source, or another route.
    void setSource(Source source) { m_source = std::move(source); }
    void setRoute(const Route &route);

signals:
    void routeChanged();
    void trackingChanged();
    void portalBusyChanged();
    // kind: 0 move, 1 press, 2 release; `button` the one pressed or
    // released (Qt::MouseButton), `buttons` those held after the event;
    // `inside` whether the point is over the tracked window.
    void pointerEvent(int kind, int x, int y, int button, int buttons, bool inside);
    // The tracked window lost the pointer (legacy wxEVT_MOUSE_CAPTURE_LOST).
    void trackingLost();
    void portalPicked(const QVariantMap &colour);
    void portalFailed(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void portalResponse(uint response, const QVariantMap &results);

private:
    void resolveRoute() const;
    void showCursor(bool dropper);
    void finishPortal();

    Source m_source;
    mutable bool m_resolved = false;
    mutable Route m_route;
    QPointer<QWindow> m_window;
    bool m_passInside = false;
    bool m_cursorShown = false;
    bool m_portalBusy = false;
    QString m_portalPath;
    int m_portalRequests = 0;
};

} // namespace hikari::ui
