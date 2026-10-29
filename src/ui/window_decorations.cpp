#include "window_decorations.h"

#include <QEvent>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWindow>

#include <algorithm>
#include <memory>

#ifdef HIKARI_WAYLAND_DECORATIONS
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>
#include <qpa/qplatformwindow_p.h>

#include "qwayland-xdg-decoration-unstable-v1.h"

#include <wayland-client.h>
#endif

namespace hikari::ui::windowdecorations {

namespace {
#ifdef HIKARI_WAYLAND_DECORATIONS
Q_LOGGING_CATEGORY(lcDecorations, "hikari.decorations")

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
        // Never freed: torn down after QGuiApplication it would talk to a closed display.
        static DecorationManager *manager = new DecorationManager;
        return manager;
    }
};

// Our own registry on our own queue, so Qt's registry and event queue are left alone.
const QStringList &compositorGlobals()
{
    static const QStringList globals = [] {
        QStringList names;
        auto *app = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
        ::wl_display *display = app ? app->display() : nullptr;
        if (!display)
            return names;
        ::wl_event_queue *queue = wl_display_create_queue(display);
        auto *wrapper = static_cast<::wl_display *>(wl_proxy_create_wrapper(display));
        wl_proxy_set_queue(reinterpret_cast<::wl_proxy *>(wrapper), queue);
        ::wl_registry *registry = wl_display_get_registry(wrapper);
        static const ::wl_registry_listener listener = {
            [](void *data, ::wl_registry *, uint32_t, const char *interface, uint32_t) {
                static_cast<QStringList *>(data)->append(QString::fromUtf8(interface));
            },
            [](void *, ::wl_registry *, uint32_t) {},
        };
        wl_registry_add_listener(registry, &listener, &names);
        wl_display_roundtrip_queue(display, queue);
        wl_registry_destroy(registry);
        wl_proxy_wrapper_destroy(wrapper);
        wl_event_queue_destroy(queue);
        return names;
    }();
    return globals;
}

class Tracker;

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

    SystemFrame frame() const { return m_override.value_or(m_frame); }

    void setFrame(SystemFrame frame)
    {
        const SystemFrame before = this->frame();
        m_frame = frame;
        if (before != this->frame())
            emit notifier()->changed(m_window);
    }

    void setOverride(std::optional<SystemFrame> frame)
    {
        const SystemFrame before = this->frame();
        m_override = frame;
        if (before != this->frame())
            emit notifier()->changed(m_window);
    }

    void watch()
    {
#ifdef HIKARI_WAYLAND_DECORATIONS
        if (m_watching || !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            return;
        m_watching = true;
        // Read now, outside Qt's dispatch of compositor events, where configure needs them.
        compositorGlobals();
        // xdg-decoration: made before the toplevel's first buffer, gone before it; Qt drops the toplevel on hide.
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
        // Late (the toplevel is gone), for a role Qt drops without hiding the window.
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
    SystemFrame m_frame = SystemFrame::None;
    std::optional<SystemFrame> m_override;
#ifdef HIKARI_WAYLAND_DECORATIONS
    bool m_watching = false;
    QPointer<QNativeInterface::Private::QWaylandWindow> m_platform;
    std::unique_ptr<ToplevelDecoration> m_decoration;
#endif
};

#ifdef HIKARI_WAYLAND_DECORATIONS
void ToplevelDecoration::zxdg_toplevel_decoration_v1_configure(uint32_t mode)
{
    const SystemFrame frame = mode == mode_server_side ? serverSideFrame(compositorGlobals()) : SystemFrame::None;
    qCDebug(lcDecorations) << "the compositor decorates" << (mode == mode_server_side ? "server-side" : "client-side")
                           << (frame == SystemFrame::TitleBar ? "with a title bar"
                               : frame == SystemFrame::Border ? "with a border only"
                                                              : "");
    m_tracker->setFrame(frame);
}
#endif
}

void requestNone(QWindow *window)
{
    if (window)
        Tracker::ensure(window)->watch();
}

void release(QWindow *window)
{
    delete Tracker::of(window);
}

QStringList compositorGlobalNames()
{
#ifdef HIKARI_WAYLAND_DECORATIONS
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return compositorGlobals();
#endif
    return {};
}

SystemFrame serverSideFrame(const QStringList &globals)
{
    const bool bordersOnly = std::any_of(globals.begin(), globals.end(), [](const QString &name) {
        return name.startsWith(QLatin1String("hyprland_"));
    });
    return bordersOnly ? SystemFrame::Border : SystemFrame::TitleBar;
}

SystemFrame systemFrame(const QWindow *window)
{
    const Tracker *tracker = Tracker::of(window);
    return tracker ? tracker->frame() : SystemFrame::None;
}

bool hasSystemTitleBar(const QWindow *window)
{
    return systemFrame(window) == SystemFrame::TitleBar;
}

Notifier *notifier()
{
    static Notifier *instance = new Notifier;
    return instance;
}

void overrideSystemFrame(QWindow *window, std::optional<SystemFrame> frame)
{
    if (window)
        Tracker::ensure(window)->setOverride(frame);
}

}
