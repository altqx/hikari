#include "window_frame.h"

#include "docking.h"
#include "window_decorations.h"
#include "window_frame_native.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QQuickItem>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <QtGui/private/qhighdpiscaling_p.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#ifdef HIKARI_WAYLAND_DECORATIONS
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif

namespace hikari::ui {

namespace windowframe {

namespace {
std::optional<Desktop> g_desktop;

QStringList tokens(const QString &list)
{
    QStringList out;
    for (const QString &token : list.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        out.append(token.trimmed().toLower());
    return out;
}

const QStringList &tilingNames()
{
    static const QStringList names{
        QStringLiteral("sway"),     QStringLiteral("hyprland"),  QStringLiteral("niri"),
        QStringLiteral("river"),    QStringLiteral("i3"),        QStringLiteral("bspwm"),
        QStringLiteral("dwm"),      QStringLiteral("dwl"),       QStringLiteral("awesome"),
        QStringLiteral("qtile"),    QStringLiteral("xmonad"),    QStringLiteral("herbstluftwm"),
        QStringLiteral("leftwm"),   QStringLiteral("spectrwm"),  QStringLiteral("wmii"),
        QStringLiteral("ratpoison"), QStringLiteral("stumpwm"),  QStringLiteral("notion"),
    };
    return names;
}

// Set by tiling window managers even inside a desktop session (KDE Plasma with i3).
const QStringList &tilingMarkers()
{
    static const QStringList markers{QStringLiteral("SWAYSOCK"), QStringLiteral("I3SOCK"),
                                     QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"), QStringLiteral("NIRI_SOCKET")};
    return markers;
}
}

Desktop classify(const Environment &environment)
{
    const QString &platform = environment.platform;
    if (platform == QLatin1String("windows"))
        return Desktop::Windows;
    if (!platform.startsWith(QLatin1String("wayland")) && platform != QLatin1String("xcb"))
        return Desktop::Unsupported;
    const QStringList names = tokens(environment.currentDesktop) + tokens(environment.sessionDesktop);
    const bool tilingName = std::any_of(names.begin(), names.end(),
                                        [](const QString &name) { return tilingNames().contains(name); });
    const bool tilingMarker = std::any_of(environment.markers.begin(), environment.markers.end(),
                                          [](const QString &name) { return tilingMarkers().contains(name); });
    const bool tilingGlobals =
        std::any_of(environment.waylandGlobals.begin(), environment.waylandGlobals.end(), [](const QString &name) {
            return name.startsWith(QLatin1String("hyprland_")) || name.startsWith(QLatin1String("zriver_"))
                   || name.startsWith(QLatin1String("river_"));
        });
    if (tilingName || tilingMarker || tilingGlobals)
        return Desktop::Tiling;
    if (names.contains(QLatin1String("kde")))
        return Desktop::Kde;
    if (names.contains(QLatin1String("gnome")) || names.contains(QLatin1String("gnome-classic")))
        return Desktop::Gnome;
    return Desktop::Other;
}

Environment currentEnvironment()
{
    Environment environment;
    environment.platform = QGuiApplication::platformName();
    environment.currentDesktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    environment.sessionDesktop = qEnvironmentVariable("XDG_SESSION_DESKTOP");
    if (environment.sessionDesktop.isEmpty())
        environment.sessionDesktop = qEnvironmentVariable("DESKTOP_SESSION");
    for (const QString &marker : tilingMarkers())
        if (!qEnvironmentVariableIsEmpty(marker.toLatin1().constData()))
            environment.markers.append(marker);
    environment.waylandGlobals = windowdecorations::compositorGlobalNames();
    return environment;
}

Desktop currentDesktop()
{
    if (g_desktop)
        return *g_desktop;
    static const Desktop desktop = classify(currentEnvironment());
    return desktop;
}

bool systemTitleBarByDefault(Desktop desktop)
{
    return desktop != Desktop::Windows && desktop != Desktop::Kde && desktop != Desktop::Gnome;
}

bool usesSystemTitleBar(const QString &setting, Desktop desktop)
{
    if (desktop == Desktop::Unsupported || setting == QLatin1String("system"))
        return true;
    if (setting == QLatin1String("custom"))
        return false;
    return systemTitleBarByDefault(desktop);
}

void overrideDesktop(std::optional<Desktop> desktop)
{
    g_desktop = desktop;
    WindowFrame::notifyDesktopChanged();
}

#ifndef Q_OS_WIN
std::unique_ptr<NativeFrame> createNativeFrame(MainWindowFrame *, QQuickWindow *)
{
    return nullptr;
}
#endif

}

namespace {
std::vector<QPointer<WindowFrame>> &singletons()
{
    static std::vector<QPointer<WindowFrame>> list;
    return list;
}

bool isWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

QList<const QWindow *> &remade()
{
    static QList<const QWindow *> list;
    return list;
}

// _GTK_SHOW_WINDOW_MENU is answered by KWin, mutter and xfwm4.
// MUST KILL: libxcb functions are dlsym'd from the copy Qt loaded instead of linked
bool showX11WindowMenu(QWindow *window, QPointF position)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return false;
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (!x11 || !native || !window->screen())
        return false;
    xcb_connection_t *connection = x11->connection();
    const auto root = quint32(quintptr(native->nativeResourceForScreen("rootwindow", window->screen())));
    struct Cookie {
        unsigned int sequence;
    };
    struct AtomReply {
        quint8 responseType, pad0;
        quint16 sequence;
        quint32 length;
        quint32 atom;
    };
    using InternAtom = Cookie (*)(xcb_connection_t *, quint8, quint16, const char *);
    using InternAtomReply = AtomReply *(*)(xcb_connection_t *, Cookie, void **);
    using UngrabPointer = Cookie (*)(xcb_connection_t *, quint32);
    using SendEvent = Cookie (*)(xcb_connection_t *, quint8, quint32, quint32, const char *);
    using Flush = int (*)(xcb_connection_t *);
    const auto internAtom = reinterpret_cast<InternAtom>(dlsym(RTLD_DEFAULT, "xcb_intern_atom"));
    const auto internAtomReply = reinterpret_cast<InternAtomReply>(dlsym(RTLD_DEFAULT, "xcb_intern_atom_reply"));
    const auto ungrabPointer = reinterpret_cast<UngrabPointer>(dlsym(RTLD_DEFAULT, "xcb_ungrab_pointer"));
    const auto sendEvent = reinterpret_cast<SendEvent>(dlsym(RTLD_DEFAULT, "xcb_send_event"));
    const auto flush = reinterpret_cast<Flush>(dlsym(RTLD_DEFAULT, "xcb_flush"));
    if (!connection || !root || !internAtom || !internAtomReply || !ungrabPointer || !sendEvent || !flush)
        return false;
    static const char name[] = "_GTK_SHOW_WINDOW_MENU";
    AtomReply *reply = internAtomReply(connection, internAtom(connection, 0, sizeof(name) - 1, name), nullptr);
    if (!reply)
        return false;
    const quint32 atom = reply->atom;
    free(reply);
    // The pointer is Qt's while a button is down; the window manager takes it.
    ungrabPointer(connection, 0 /* XCB_CURRENT_TIME */);
    const QPoint at = QHighDpi::toNativeGlobalPosition(window->mapToGlobal(position), window).toPoint();
    // data: device 0 (the core pointer), then the root x and y, as gdk_window_show_window_menu sends.
    struct ClientMessage {
        quint8 responseType, format;
        quint16 sequence;
        quint32 window, type;
        qint32 data[5];
    } message{33 /* XCB_CLIENT_MESSAGE */, 32, 0, quint32(window->winId()), atom, {0, at.x(), at.y(), 0, 0}};
    static_assert(sizeof(ClientMessage) == 32);
    sendEvent(connection, 0, root, 0x00180000 /* SubstructureNotify | SubstructureRedirect */,
              reinterpret_cast<const char *>(&message));
    flush(connection);
    return true;
#else
    Q_UNUSED(window);
    Q_UNUSED(position);
    return false;
#endif
}

bool showWaylandWindowMenu(QWindow *window)
{
#ifdef HIKARI_WAYLAND_DECORATIONS
    auto *platform = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
    if (!platform || !platform->shellSurface())
        return false;
    QtWaylandClient::QWaylandInputDevice *seat = platform->display()->lastInputDevice();
    if (!seat)
        seat = platform->display()->defaultInputDevice();
    return seat && platform->shellSurface()->showWindowMenu(seat);
#else
    Q_UNUSED(window);
    return false;
#endif
}

bool controlAt(const QQuickItem *item, QPointF scenePos, const QQuickItem *moveArea)
{
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(),
                     [](const QQuickItem *a, const QQuickItem *b) { return a->z() > b->z(); });
    for (const QQuickItem *child : children) {
        if (child == moveArea || !child->isVisible() || !child->isEnabled())
            continue;
        if (!child->contains(child->mapFromScene(scenePos)))
            continue;
        // QQuickText accepts the left button for its links but passes presses on.
        const bool takesPresses = child->acceptedMouseButtons() != Qt::NoButton && !child->inherits("QQuickText");
        if (takesPresses || controlAt(child, scenePos, moveArea))
            return true;
    }
    return false;
}

bool under(const QQuickItem *item, QPointF scenePos)
{
    return item && item->isVisible() && item->contains(item->mapFromScene(scenePos));
}
}

WindowFrame::WindowFrame(QObject *parent)
    : QObject(parent)
{
    singletons().emplace_back(this);
}

bool WindowFrame::systemTitleBarByDefault() const
{
    return windowframe::systemTitleBarByDefault(windowframe::currentDesktop());
}

bool WindowFrame::customAvailable() const
{
    return windowframe::currentDesktop() != windowframe::Desktop::Unsupported;
}

bool WindowFrame::nativeResize()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

int WindowFrame::resizeBand()
{
    return dockchrome::kResizeGrip;
}

bool WindowFrame::usesSystemTitleBar(const QString &setting) const
{
    return windowframe::usesSystemTitleBar(setting, windowframe::currentDesktop());
}

bool WindowFrame::remaking(QWindow *window)
{
    return window && remade().contains(window);
}

void WindowFrame::notifyDesktopChanged()
{
    auto &list = singletons();
    list.erase(std::remove(list.begin(), list.end(), nullptr), list.end());
    for (const auto &frame : list)
        emit frame->desktopChanged();
}

MainWindowFrame::MainWindowFrame(QObject *parent)
    : QObject(parent)
{
}

MainWindowFrame::MainWindowFrame(DialogKind, QObject *parent)
    : QObject(parent)
    , m_dialog(true)
{
}

MainWindowFrame::~MainWindowFrame() = default;

DialogFrame::DialogFrame(QObject *parent)
    : MainWindowFrame(DialogKind{}, parent)
{
}

void MainWindowFrame::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    m_native.reset();
    // App-wide: a key a shortcut takes reaches only the focus item, as ShortcutOverride.
    // MUST KILL: the empty m_dialog branch is how a dialog skips the Alt event filter
    if (m_dialog) {
    } else if (m_window && !window) {
        QCoreApplication::instance()->removeEventFilter(this);
    } else if (!m_window && window) {
        QCoreApplication::instance()->installEventFilter(this);
    }
    m_window = window;
    m_applied = false;
    if (m_window)
        m_native = windowframe::createNativeFrame(this, m_window);
    apply();
    emit windowChanged();
}

void MainWindowFrame::setCustom(bool custom)
{
    if (m_custom == custom)
        return;
    m_custom = custom;
    apply();
    emit customChanged();
}

void MainWindowFrame::setResizable(bool resizable)
{
    if (m_resizable == resizable)
        return;
    m_resizable = resizable;
    m_applied = false;
    apply();
    emit resizableChanged();
}

void MainWindowFrame::setClosable(bool closable)
{
    if (m_closable == closable)
        return;
    m_closable = closable;
    m_applied = false;
    apply();
    emit closableChanged();
}

void MainWindowFrame::setTitleBar(QQuickItem *item)
{
    if (m_titleBar == item)
        return;
    m_titleBar = item;
    emit itemsChanged();
}

void MainWindowFrame::setMoveArea(QQuickItem *item)
{
    if (m_moveArea == item)
        return;
    m_moveArea = item;
    emit itemsChanged();
}

void MainWindowFrame::setMaximizeButton(QQuickItem *item)
{
    if (m_maximizeButton == item)
        return;
    m_maximizeButton = item;
    emit itemsChanged();
}

void MainWindowFrame::setWindowMenuButton(QQuickItem *item)
{
    if (m_windowMenuButton == item)
        return;
    m_windowMenuButton = item;
    emit itemsChanged();
}

void MainWindowFrame::setMaximizeState(bool hovered, bool pressed)
{
    if (m_maximizeHovered == hovered && m_maximizePressed == pressed)
        return;
    m_maximizeHovered = hovered;
    m_maximizePressed = pressed;
    emit maximizeStateChanged();
}

bool MainWindowFrame::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window) {
        if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(event)->key() != Qt::Key_Alt) {
            const auto *item = qobject_cast<QQuickItem *>(watched);
            if (item && item->window() == m_window)
                m_altAlone = false;
        }
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::KeyPress: {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Alt) {
            if (!key->isAutoRepeat()) {
                // Window systems differ on whether Alt's own modifier bit is set.
                m_altAlone = (key->modifiers() & ~Qt::AltModifier) == Qt::NoModifier;
                setAltHeld(true);
            }
        } else {
            m_altAlone = false;
        }
        break;
    }
    case QEvent::KeyRelease: {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Alt || key->isAutoRepeat())
            break;
        setAltHeld(false);
        if (m_altAlone && (key->modifiers() & ~Qt::AltModifier) == Qt::NoModifier) {
            m_altAlone = false;
            emit altTapped();
            // Swallowed: on Windows the system would enter its menu mode too.
            return true;
        }
        m_altAlone = false;
        break;
    }
    case QEvent::ShortcutOverride:
        if (static_cast<QKeyEvent *>(event)->key() != Qt::Key_Alt)
            m_altAlone = false;
        break;
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        m_altAlone = false;
        break;
    case QEvent::FocusOut:
    case QEvent::Hide:
        m_altAlone = false;
        setAltHeld(false);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void MainWindowFrame::setAltHeld(bool held)
{
    if (m_altHeld == held)
        return;
    m_altHeld = held;
    emit altHeldChanged();
}

void MainWindowFrame::apply()
{
    if (!m_window)
        return;
    const bool frameless = m_window->flags().testFlag(Qt::FramelessWindowHint);
    if (frameless != m_custom || !m_applied) {
        m_applied = true;
        // KWin draws its own frame round a frameless Wayland window unless asked not to.
        if (isWayland()) {
            if (m_custom)
                windowdecorations::requestNone(m_window);
            else
                windowdecorations::release(m_window);
        }
        // Qt enables Windows' system menu items (Alt+Space) from these hints.
        const Qt::WindowFlags buttons = !m_dialog ? Qt::WindowMinMaxButtonsHint
                                        : m_resizable ? Qt::WindowMaximizeButtonHint
                                                      : Qt::WindowFlags();
        // Under CustomizeWindowHint only the hints given show: no Close without WindowCloseButtonHint.
        const Qt::WindowFlags close = m_closable ? Qt::WindowCloseButtonHint : Qt::CustomizeWindowHint;
        const Qt::WindowFlags hints = Qt::WindowTitleHint | Qt::WindowSystemMenuHint | buttons | close;
        const Qt::WindowFlags dropped = m_dialog ? (Qt::WindowMinMaxButtonsHint & ~buttons)
                                                       | (m_closable ? Qt::CustomizeWindowHint : Qt::WindowCloseButtonHint)
                                                 : Qt::WindowFlags();
        const Qt::WindowFlags flags = m_custom   ? (m_window->flags() & ~dropped) | Qt::FramelessWindowHint | hints
                                      : m_dialog ? (m_window->flags() & ~dropped & ~Qt::FramelessWindowHint) | hints
                                                 : m_window->flags() & ~Qt::FramelessWindowHint;
        if (flags != m_window->flags()) {
            // A Wayland toplevel takes its decoration only when created: hide and show it again.
            const QWindow::Visibility shown = m_window->visibility();
            const bool remake = isWayland() && m_window->isVisible();
            if (remake) {
                remade().append(m_window);
                m_window->hide();
            }
            m_window->setFlags(flags);
            if (remake) {
                m_window->setVisibility(shown);
                remade().removeOne(m_window);
            }
        }
    }
    if (m_native)
        m_native->apply(m_custom);
}

MainWindowFrame::Hit MainWindowFrame::hitAt(QPointF position) const
{
    if (!m_custom || !m_titleBar || !m_titleBar->isVisible() || m_titleBar->window() != m_window)
        return Client;
    if (!under(m_titleBar, position))
        return Client;
    if (under(m_maximizeButton, position))
        return MaximizeButton;
    if (under(m_windowMenuButton, position))
        return WindowMenuButton;
    return controlAt(m_titleBar, position, m_moveArea) ? Client : Caption;
}

bool MainWindowFrame::showWindowMenu(QPointF position)
{
    emit windowMenuRequested(position);
    if (!m_window)
        return false;
    if (m_native)
        return m_native->showWindowMenu(position);
    if (isWayland())
        return showWaylandWindowMenu(m_window);
    return showX11WindowMenu(m_window, position);
}

void MainWindowFrame::toggleMaximized()
{
    if (!m_window)
        return;
    if (m_window->visibility() == QWindow::Maximized)
        m_window->showNormal();
    else
        m_window->showMaximized();
}

}
