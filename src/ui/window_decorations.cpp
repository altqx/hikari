#include "window_decorations.h"

#include <QEvent>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWindow>

#include <memory>

#ifdef HIKARI_WAYLAND_DECORATIONS
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>
#include <qpa/qplatformwindow_p.h>

#include "qwayland-xdg-decoration-unstable-v1.h"
#endif

namespace hikari::ui::windowdecorations {

namespace {
#ifdef HIKARI_WAYLAND_DECORATIONS
Q_LOGGING_CATEGORY(lcDecorations, "hikari.decorations")

// The compositor's zxdg_decoration_manager_v1, bound through Qt's registry
// (inactive where the compositor has none: mutter). Made once and kept for
// the process: tearing it down after QGuiApplication would talk to a closed
// display.
class DecorationManager final
    : public QWaylandClientExtensionTemplate<DecorationManager, &QtWayland::zxdg_decoration_manager_v1::destroy>,
      public QtWayland::zxdg_decoration_manager_v1 {
public:
    DecorationManager()
        : QWaylandClientExtensionTemplate(1)
    {
        initialize();
    }

    static DecorationManager *instance()
    {
        static DecorationManager *manager = new DecorationManager;
        return manager;
    }
};

class Tracker;

// One window's zxdg_toplevel_decoration_v1, for one xdg_toplevel.
class ToplevelDecoration final : public QtWayland::zxdg_toplevel_decoration_v1 {
public:
    ToplevelDecoration(::zxdg_toplevel_decoration_v1 *object, Tracker *tracker)
        : QtWayland::zxdg_toplevel_decoration_v1(object)
        , m_tracker(tracker)
    {
    }
    ~ToplevelDecoration() override
    {
        if (isInitialized())
            destroy();
    }

protected:
    void zxdg_toplevel_decoration_v1_configure(uint32_t mode) override;

private:
    Tracker *const m_tracker;
};
#endif

// Per window: whether the window system frames it, and on Wayland the
// decoration object asking it not to. A child of the window.
class Tracker final : public QObject {
public:
    explicit Tracker(QWindow *window)
        : QObject(window)
        , m_window(window)
    {
        setObjectName(QStringLiteral("hikariWindowDecorations"));
    }
    ~Tracker() override { drop(); }

    static Tracker *of(const QWindow *window)
    {
        if (window) {
            for (QObject *child : window->children())
                if (auto *tracker = dynamic_cast<Tracker *>(child))
                    return tracker;
        }
        return nullptr;
    }
    static Tracker *ensure(QWindow *window)
    {
        Tracker *tracker = of(window);
        return tracker ? tracker : new Tracker(window);
    }

    bool systemTitleBar() const { return m_override.value_or(m_framed); }

    void setFramed(bool framed)
    {
        const bool before = systemTitleBar();
        m_framed = framed;
        if (before != systemTitleBar())
            emit notifier()->changed(m_window);
    }

    void setOverride(std::optional<bool> framed)
    {
        const bool before = systemTitleBar();
        m_override = framed;
        if (before != systemTitleBar())
            emit notifier()->changed(m_window);
    }

    void watch()
    {
#ifdef HIKARI_WAYLAND_DECORATIONS
        if (m_watching || !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            return;
        m_watching = true;
        // The platform window comes with QWindow::create(); its xdg_toplevel
        // with each show. A decoration object is made before the toplevel's
        // first buffer and has to go before the toplevel does: Qt destroys
        // the toplevel when the window hides, after visibleChanged.
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible)
                drop();
        });
        watchPlatformWindow();
#endif
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window && event->type() == QEvent::PlatformSurface) {
            switch (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()) {
            case QPlatformSurfaceEvent::SurfaceCreated:
                watchPlatformWindow();
                break;
            case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed:
                drop();
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void watchPlatformWindow()
    {
#ifdef HIKARI_WAYLAND_DECORATIONS
        using WaylandWindow = QNativeInterface::Private::QWaylandWindow;
        auto *platform = m_window->nativeInterface<WaylandWindow>();
        if (!platform || platform == m_platform)
            return;
        m_platform = platform;
        connect(platform, &WaylandWindow::surfaceRoleCreated, this, &Tracker::create);
        // Late (the toplevel is gone already), for a role Qt drops without
        // hiding the window.
        connect(platform, &WaylandWindow::surfaceRoleDestroyed, this, &Tracker::drop);
#endif
    }

    void create()
    {
#ifdef HIKARI_WAYLAND_DECORATIONS
        DecorationManager *manager = DecorationManager::instance();
        auto *toplevel = m_platform ? m_platform->surfaceRole<::xdg_toplevel>() : nullptr;
        if (!toplevel || !manager->isActive()) {
            qCDebug(lcDecorations) << m_window << "no decoration object: toplevel" << toplevel << "manager"
                                   << manager->isActive();
            return;
        }
        drop();
        m_decoration = std::make_unique<ToplevelDecoration>(manager->get_toplevel_decoration(toplevel), this);
        m_decoration->set_mode(QtWayland::zxdg_toplevel_decoration_v1::mode_client_side);
        qCDebug(lcDecorations) << m_window << "asked for client-side decorations";
#endif
    }

    void drop()
    {
#ifdef HIKARI_WAYLAND_DECORATIONS
        m_decoration.reset();
#endif
    }

    QWindow *const m_window;
    bool m_framed = false;
    std::optional<bool> m_override;
#ifdef HIKARI_WAYLAND_DECORATIONS
    bool m_watching = false;
    QPointer<QNativeInterface::Private::QWaylandWindow> m_platform;
    std::unique_ptr<ToplevelDecoration> m_decoration;
#endif
};

#ifdef HIKARI_WAYLAND_DECORATIONS
void ToplevelDecoration::zxdg_toplevel_decoration_v1_configure(uint32_t mode)
{
    qCDebug(lcDecorations) << "the compositor decorates" << (mode == mode_server_side ? "server-side" : "client-side");
    m_tracker->setFramed(mode == mode_server_side);
}
#endif
} // namespace

void requestNone(QWindow *window)
{
    if (window)
        Tracker::ensure(window)->watch();
}

bool hasSystemTitleBar(const QWindow *window)
{
    const Tracker *tracker = Tracker::of(window);
    return tracker && tracker->systemTitleBar();
}

Notifier *notifier()
{
    static Notifier *instance = new Notifier;
    return instance;
}

void overrideSystemTitleBar(QWindow *window, std::optional<bool> framed)
{
    if (window)
        Tracker::ensure(window)->setOverride(framed);
}

} // namespace hikari::ui::windowdecorations
