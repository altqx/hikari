#include "screen_sampler.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPixmap>
#include <QScreen>
#include <QUuid>

#include <algorithm>
#include <cmath>

#ifdef HIKARI_SCREEN_PORTAL
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

namespace hikari::ui {

namespace {

Q_LOGGING_CATEGORY(lcDropper, "hikari.dropper", QtInfoMsg)

#ifdef HIKARI_SCREEN_PORTAL
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenshot = QStringLiteral("org.freedesktop.portal.Screenshot");

// The Screenshot portal with PickColor (its interface version 2).
bool portalOffersPickColor()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    auto get = QDBusMessage::createMethodCall(kPortalService, kPortalPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("Get"));
    get << kScreenshot << QStringLiteral("version");
    const QDBusMessage reply = bus.call(get, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    return reply.arguments().first().value<QDBusVariant>().variant().toUInt() >= 2;
}
#endif

// The 7x7 screen pixels around a global point, in device pixels; black
// where the point's surroundings leave the screen (legacy Blit from a
// wxScreenDC).
QImage grabScreen(QPoint global)
{
    constexpr int n = ScreenSampler::kSize;
    QImage cells(n, n, QImage::Format_RGB32);
    cells.fill(Qt::black);
    QScreen *screen = QGuiApplication::screenAt(global);
    if (!screen)
        return cells;
    const QRect geometry = screen->geometry();
    const qreal ratio = screen->devicePixelRatio();
    // A logical square holding n device pixels each way, inside the screen.
    const int margin = int(std::ceil((n / 2 + 1) / ratio));
    const QPoint local = global - geometry.topLeft();
    const QRect wanted(local.x() - margin, local.y() - margin, 2 * margin + 1, 2 * margin + 1);
    const QRect area = wanted & QRect(QPoint(), geometry.size());
    if (area.isEmpty())
        return cells;
    const QPixmap shot = screen->grabWindow(0, area.x(), area.y(), area.width(), area.height());
    if (shot.isNull())
        return cells;
    const QImage image = shot.toImage().convertToFormat(QImage::Format_RGB32);
    const qreal scale = image.width() / qreal(area.width());
    const QPoint centre(int(std::floor((local.x() - area.x()) * scale)), int(std::floor((local.y() - area.y()) * scale)));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const QPoint p = centre + QPoint(x - n / 2, y - n / 2);
            if (image.rect().contains(p))
                cells.setPixel(x, y, image.pixel(p));
        }
    return cells;
}

} // namespace

bool ScreenSampler::isWaylandSession()
{
    return !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
           || qEnvironmentVariable("XDG_SESSION_TYPE").compare(QLatin1String("wayland"), Qt::CaseInsensitive) == 0;
}

ScreenSampler::Route ScreenSampler::routeFor(const QString &platformName, bool waylandSession, bool portalAvailable)
{
    const bool wayland = platformName.startsWith(QLatin1String("wayland"));
    // Offscreen and the other platforms read their own screens.
    if (!wayland && !(waylandSession && platformName == QLatin1String("xcb")))
        return {QStringLiteral("grab"), {}};
    if (portalAvailable)
        return {QStringLiteral("portal"), {}};
    return {{},
            QCoreApplication::translate("HikariSub.Colours",
                                        "Picking a colour from the screen is not available: this Wayland session offers no screenshot portal (xdg-desktop-portal with PickColor).")};
}

ScreenSampler::ScreenSampler(QObject *parent)
    : QObject(parent), m_source(grabScreen),
      m_grab([](QWindow *window, bool grab) { return window->setMouseGrabEnabled(grab); })
{
    m_regrab.setInterval(kRegrabInterval);
    // Runs only after a refusal; hidden again, the window waits for its
    // next expose. Only real asks count against kRegrabTries.
    connect(&m_regrab, &QTimer::timeout, this, [this] {
        if (!m_window || !m_window->isExposed()) {
            m_regrab.stop();
            return;
        }
        ++m_regrabTries;
        takePointer(false);
        if (m_regrabTries >= kRegrabTries)
            m_regrab.stop();
    });
}

ScreenSampler::~ScreenSampler()
{
    stopTracking();
}

void ScreenSampler::resolveRoute() const
{
    if (m_resolved)
        return;
    m_resolved = true;
    const QString platform = QGuiApplication::platformName();
    const bool session = isWaylandSession();
    bool portal = false;
#ifdef HIKARI_SCREEN_PORTAL
    if (routeFor(platform, session, true).name == QLatin1String("portal"))
        portal = portalOffersPickColor();
#endif
    m_route = routeFor(platform, session, portal);
}

QString ScreenSampler::route() const
{
    resolveRoute();
    return m_route.name;
}

QString ScreenSampler::unavailableReason() const
{
    resolveRoute();
    return m_route.reason;
}

void ScreenSampler::setRoute(const Route &route)
{
    m_resolved = true;
    m_route = route;
    emit routeChanged();
}

QVariantList ScreenSampler::sample(int x, int y) const
{
    if (route() != QLatin1String("grab"))
        return {};
    const QImage image = m_source(QPoint(x, y));
    QVariantList cells;
    for (int row = 0; row < kSize; ++row)
        for (int column = 0; column < kSize; ++column) {
            const QRgb p = image.valid(column, row) ? image.pixel(column, row) : qRgb(0, 0, 0);
            cells.append(QVariantMap{{QStringLiteral("r"), qRed(p)}, {QStringLiteral("g"), qGreen(p)},
                                     {QStringLiteral("b"), qBlue(p)}, {QStringLiteral("a"), 0}});
        }
    return cells;
}

QPoint ScreenSampler::cursorPosition() const
{
    return QCursor::pos();
}

QRect ScreenSampler::availableGeometryAt(int x, int y) const
{
    QScreen *screen = QGuiApplication::screenAt(QPoint(x, y));
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect();
}

void ScreenSampler::moveToPointer(QWindow *window, int x, int y) const
{
    if (!window)
        return;
    const QRect rc = availableGeometryAt(x, y);
    const QSize size = window->frameGeometry().size();
    // mst.x = MID(rc.x, mst.x - siz.x / 2, rc.width + rc.x - siz.x)
    const int nx = std::max(rc.x(), std::min(x - size.width() / 2, rc.width() + rc.x() - size.width()));
    int ny = y + 15;
    if (ny + size.height() > rc.height() + rc.y()) {
        ny = ny - size.height() - 30;
        if (ny < rc.y())
            ny = rc.height() + rc.y() - size.height();
    }
    window->setFramePosition(QPoint(nx, ny));
}

bool ScreenSampler::startTracking(QWindow *window, bool passInside)
{
    stopTracking();
    if (!window)
        return false;
    m_window = window;
    m_passInside = passInside;
    window->installEventFilter(this);
    // Offscreen and Wayland grant no pointer grab; the events over the
    // window still come. Under an X11 window manager the window is not
    // viewable yet: the grab waits for its expose.
    m_grabbed = false;
    m_regrabTries = 0;
    takePointer(false);
    showCursor(!passInside || !window->geometry().contains(QCursor::pos()));
    emit trackingChanged();
    return true;
}

void ScreenSampler::stopTracking()
{
    if (!m_window)
        return;
    qCDebug(lcDropper) << m_window << "tracking stops";
    m_regrab.stop();
    m_window->removeEventFilter(this);
    m_grab(m_window, false);
    m_grabbed = false;
    m_window.clear();
    showCursor(false);
    emit trackingChanged();
}

void ScreenSampler::takePointer(bool again)
{
    // X refuses a grab on a window that is not viewable (GrabNotViewable),
    // and Qt's XInput 2 grab reports success all the same: ask only once
    // the window is exposed.
    if (!m_window || !m_window->isExposed() || (m_grabbed && !again))
        return;
    m_grabbed = m_grab(m_window, true);
    qCDebug(lcDropper) << m_window << "pointer grab" << (m_grabbed ? "held" : "refused");
    if (m_grabbed)
        m_regrab.stop();
    else if (!m_regrab.isActive() && m_regrabTries < kRegrabTries)
        m_regrab.start();
}

void ScreenSampler::showCursor(bool dropper)
{
    if (dropper == m_cursorShown)
        return;
    m_cursorShown = dropper;
    // Legacy sets its "eyedropper_cursor"; the cross marks the pixel.
    if (dropper)
        QGuiApplication::setOverrideCursor(Qt::CrossCursor);
    else
        QGuiApplication::restoreOverrideCursor();
}

bool ScreenSampler::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window)
        return QObject::eventFilter(watched, event);
    int kind = -1;
    switch (event->type()) {
    case QEvent::MouseMove:
        kind = 0;
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        kind = 1;
        break;
    case QEvent::MouseButtonRelease:
        kind = 2;
        break;
    case QEvent::WindowDeactivate:
        qCDebug(lcDropper) << m_window << "deactivated: tracking lost";
        emit trackingLost();
        return false;
    // The window became viewable (its expose; the window manager maps its
    // frame and then activates it): ask for the grab again, since a grab
    // Qt reported held may have been refused.
    case QEvent::Expose:
    case QEvent::WindowActivate:
        takePointer(true);
        return QObject::eventFilter(watched, event);
    // The pointer left it for outside the window, not for a child or a
    // popup over it: legacy OnLeaveWindow captures the mouse again then.
    // This asks even when Qt reports the grab held, as that report can be
    // wrong.
    case QEvent::Leave:
        if (!m_window->geometry().contains(QCursor::pos()))
            takePointer(true);
        return QObject::eventFilter(watched, event);
    default:
        return QObject::eventFilter(watched, event);
    }
    auto *mouse = static_cast<QMouseEvent *>(event);
    const QPoint global = mouse->globalPosition().toPoint();
    const bool inside = QRect(QPoint(), m_window->size()).contains(mouse->position().toPoint());
    showCursor(!m_passInside || !inside);
    emit pointerEvent(kind, global.x(), global.y(), int(mouse->button()), int(mouse->buttons()), inside);
    // Over its own window the simple picker's controls take the event.
    return !(m_passInside && inside);
}

void ScreenSampler::pickFromPortal()
{
    if (m_portalBusy)
        return;
    if (route() != QLatin1String("portal")) {
        emit portalFailed(available() ? QCoreApplication::translate("HikariSub.Colours", "The desktop's screenshot portal is not in use here.")
                                      : unavailableReason());
        return;
    }
    if (m_portalRequest) {
        m_portalBusy = true;
        emit portalBusyChanged();
        m_portalRequest();
        return;
    }
#ifdef HIKARI_SCREEN_PORTAL
    auto bus = QDBusConnection::sessionBus();
    // Subscribe to the request's Response before asking (handle_token).
    const QString token = QStringLiteral("hikari_pick%1_%2")
                              .arg(++m_portalRequests)
                              .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    m_portalPath = kPortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
    bus.connect(kPortalService, m_portalPath, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(portalResponse(uint, QVariantMap)));
    m_portalBusy = true;
    emit portalBusyChanged();
    auto call = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kScreenshot, QStringLiteral("PickColor"));
    call << QString() << QVariantMap{{QStringLiteral("handle_token"), token}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError())
            failPortalCall(w->error().message());
    });
#else
    emit portalFailed(unavailableReason());
#endif
}

void ScreenSampler::portalResponse(uint response, const QVariantMap &results)
{
#ifdef HIKARI_SCREEN_PORTAL
    if (response != 0 || !results.contains(QStringLiteral("color"))) {
        answerPortal(response == 1 ? 1 : 2);
        return;
    }
    double r = 0, g = 0, b = 0;
    const QDBusArgument argument = results.value(QStringLiteral("color")).value<QDBusArgument>();
    argument.beginStructure();
    argument >> r >> g >> b;
    argument.endStructure();
    answerPortal(0, r, g, b);
#else
    Q_UNUSED(response)
    Q_UNUSED(results)
#endif
}

void ScreenSampler::answerPortal(uint response, double red, double green, double blue)
{
    if (!m_portalBusy)
        return;
    // The answer goes out first and portalBusy falls after it: a listener
    // that stops listening once the request is over (portalBusyChanged)
    // would otherwise miss the answer emitted in the same call.
    if (response == 0) {
        auto channel = [](double v) { return std::clamp(int(std::lround(v * 255)), 0, 255); };
        emit portalPicked({{QStringLiteral("r"), channel(red)},
                           {QStringLiteral("g"), channel(green)},
                           {QStringLiteral("b"), channel(blue)},
                           {QStringLiteral("a"), 0}});
    } else if (response != 1) { // 1: the user cancelled, as a dropper released without a pick
        emit portalFailed(QCoreApplication::translate("HikariSub.Colours", "The desktop could not pick a colour from the screen."));
    }
    finishPortal();
}

void ScreenSampler::failPortalCall(const QString &message)
{
    if (!m_portalBusy)
        return;
    emit portalFailed(message);
    finishPortal();
}

void ScreenSampler::finishPortal()
{
#ifdef HIKARI_SCREEN_PORTAL
    if (!m_portalPath.isEmpty())
        QDBusConnection::sessionBus().disconnect(kPortalService, m_portalPath, QStringLiteral("org.freedesktop.portal.Request"),
                                                 QStringLiteral("Response"), this,
                                                 SLOT(portalResponse(uint, QVariantMap)));
    m_portalPath.clear();
#endif
    m_portalBusy = false;
    emit portalBusyChanged();
}

} // namespace hikari::ui
