#include "docking.h"
#include "window_decorations.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>
// Private engine headers, isolated here: re-initializing the platform for a
// new QML engine has no public entry point (the engine's own
// tests_deinitPlatform does the same, in developer builds only).
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Layout.h>
#include <kddockwidgets/core/MainWindow.h>
#include <kddockwidgets/core/Platform.h>
#include <kddockwidgets/core/TabBar.h>
#include <kddockwidgets/core/indicators/ClassicDropIndicatorOverlay.h>
#include <kddockwidgets/core/views/ClassicIndicatorWindowViewInterface.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/View.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <kddockwidgets/qtquick/views/FloatingWindow.h>
#include <kddockwidgets/qtquick/views/Group.h>
#include <kddockwidgets/qtquick/views/TabBar.h>

#include <QContextMenuEvent>
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QtMath>
#include <qpa/qplatformnativeinterface.h>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace hikari::ui {

namespace {
Q_LOGGING_CATEGORY(lcDocking, "hikari.docking")
QPointer<QQmlEngine> g_attached;
bool g_everAttached = false;

using KDDockWidgets::DropLocation;

// D3: MuseScore's drop feedback (docs/research/musescore-docking.md §4). The
// engine's QtQuick frontend has no Segmented indicators at 2.4.1 (its view
// factory returns no view for them), and their wedge-shaped hot zones are not
// MuseScore's highlight anyway, so the Classic overlay stays and its two
// parts are replaced: the indicator "window" draws nothing and finds the drop
// from where the pointer is (DropZones), and the rubber band is the accent
// highlight over the area the drop takes (DockDropHighlight.qml).
//
// Zones, over the group under the pointer: its header adds a tab, as does
// its middle third (both ways); elsewhere the nearest edge splits it on that
// side. Within kOuterBand of the drop area's own edge, the panel goes along
// that whole edge. A zone the engine does not allow (its
// dropIndicatorVisible) is no drop: the panel stays floating there.
class DropZones final : public KDDockWidgets::Core::ClassicIndicatorWindowViewInterface {
public:
    static constexpr int kOuterBand = 24;

    explicit DropZones(KDDockWidgets::Core::ClassicDropIndicatorOverlay *overlay)
        : m_overlay(overlay)
    {
    }

    void setObjectName(const QString &) override {}
    DropLocation hover(QPoint globalPos) override
    {
        QQuickItem *area = dropArea();
        return area ? locate(area->mapFromGlobal(QPointF(globalPos)).toPoint()) : DropLocation::DropLocation_None;
    }
    // Where a drop at `location` would be made: the zone's middle (tests).
    QPoint posForIndicator(DropLocation location) const override
    {
        QQuickItem *area = dropArea();
        if (!area)
            return {};
        const QRect bounds(0, 0, int(area->width()), int(area->height()));
        const QRect group = m_overlay->hoveredGroupRect();
        const int band = outerBand(bounds);
        QPoint local;
        switch (location) {
        case DropLocation::DropLocation_OutterLeft: local = {bounds.left() + band / 2, bounds.center().y()}; break;
        case DropLocation::DropLocation_OutterRight: local = {bounds.right() - band / 2, bounds.center().y()}; break;
        case DropLocation::DropLocation_OutterTop: local = {bounds.center().x(), bounds.top() + band / 2}; break;
        case DropLocation::DropLocation_OutterBottom: local = {bounds.center().x(), bounds.bottom() - band / 2}; break;
        case DropLocation::DropLocation_Left: local = {group.left() + group.width() / 6, group.center().y()}; break;
        case DropLocation::DropLocation_Right: local = {group.right() - group.width() / 6, group.center().y()}; break;
        case DropLocation::DropLocation_Top:
            local = {group.center().x(), group.top() + std::max(dockchrome::kHeaderHeight + 2, group.height() / 6)};
            break;
        case DropLocation::DropLocation_Bottom: local = {group.center().x(), group.bottom() - group.height() / 6}; break;
        case DropLocation::DropLocation_Center: local = group.center(); break;
        default: return {};
        }
        return area->mapToGlobal(QPointF(local)).toPoint();
    }
    void updatePositions() override {}
    void raise() override {}
    void setVisible(bool) override {}
    void resize(QSize) override {}
    void setGeometry(QRect) override {}
    bool isWindow() const override { return false; }
    void updateIndicatorVisibility() override {}

private:
    QQuickItem *dropArea() const
    {
        QQuickItem *overlay = KDDockWidgets::QtQuick::asQQuickItem(m_overlay->view());
        return overlay ? overlay->parentItem() : nullptr;
    }
    static int outerBand(const QRect &bounds)
    {
        return std::min(kOuterBand, std::min(bounds.width(), bounds.height()) / 6);
    }
    DropLocation allowed(DropLocation location) const
    {
        return m_overlay->dropIndicatorVisible(location) ? location : DropLocation::DropLocation_None;
    }
    DropLocation locate(QPoint local) const
    {
        QQuickItem *area = dropArea();
        const QRect bounds(0, 0, int(area->width()), int(area->height()));
        const int band = outerBand(bounds);
        // Along an edge of the whole area: the nearest one.
        struct Edge {
            int distance;
            DropLocation location;
        };
        const Edge edges[] = {{local.x() - bounds.left(), DropLocation::DropLocation_OutterLeft},
                              {bounds.right() - local.x(), DropLocation::DropLocation_OutterRight},
                              {local.y() - bounds.top(), DropLocation::DropLocation_OutterTop},
                              {bounds.bottom() - local.y(), DropLocation::DropLocation_OutterBottom}};
        const Edge *nearest = std::min_element(std::begin(edges), std::end(edges),
                                               [](const Edge &a, const Edge &b) { return a.distance < b.distance; });
        if (nearest->distance >= 0 && nearest->distance < band && m_overlay->dropIndicatorVisible(nearest->location))
            return nearest->location;
        // Over a group: its header or middle third adds a tab, the rest splits it.
        const QRect group = m_overlay->hoveredGroupRect();
        if (!m_overlay->hoveredGroup() || !group.contains(local) || group.width() <= 0 || group.height() <= 0)
            return DropLocation::DropLocation_None;
        if (local.y() < group.top() + dockchrome::kHeaderHeight)
            return allowed(DropLocation::DropLocation_Center);
        const double fx = double(local.x() - group.left()) / group.width();
        const double fy = double(local.y() - group.top()) / group.height();
        const auto middle = [](double f) { return f >= 1.0 / 3 && f <= 2.0 / 3; };
        if (middle(fx) && middle(fy))
            return allowed(DropLocation::DropLocation_Center);
        const std::pair<double, DropLocation> sides[] = {{fx, DropLocation::DropLocation_Left},
                                                         {1 - fx, DropLocation::DropLocation_Right},
                                                         {fy, DropLocation::DropLocation_Top},
                                                         {1 - fy, DropLocation::DropLocation_Bottom}};
        return allowed(std::min_element(std::begin(sides), std::end(sides))->second);
    }

    KDDockWidgets::Core::ClassicDropIndicatorOverlay *const m_overlay;
};

// The drop highlight, in the drop area over the area the drop takes; the
// engine sizes, shows and raises it (the Classic overlay's rubber band).
class DropHighlight final : public KDDockWidgets::QtQuick::View {
public:
    explicit DropHighlight(QQuickItem *parent)
        : View(nullptr, KDDockWidgets::Core::ViewType::RubberBand, parent)
    {
        setVisible(false);
        setZ(1000);
        QQuickItem *visual = createItem(KDDockWidgets::QtQuick::Platform::instance()->qmlEngine(),
                                        QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockDropHighlight.qml"));
        if (visual) {
            visual->setParent(this);
            visual->setParentItem(this);
        }
    }
};

// The engine's views with Hikari's headers, and (K2) its separators, group
// frames and floating windows in the theme's colours.
class ViewFactory : public KDDockWidgets::QtQuick::ViewFactory {
public:
    QUrl titleBarFilename() const override { return QUrl(QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockTitleBar.qml")); }
    QUrl tabbarFilename() const override { return QUrl(QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockTabBar.qml")); }
    QUrl separatorFilename() const override { return QUrl(QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockSeparator.qml")); }
    QUrl groupFilename() const override { return QUrl(QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockGroup.qml")); }
    QUrl floatingWindowFilename() const override
    {
        return QUrl(QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockFloatingWindow.qml"));
    }
    KDDockWidgets::Core::ClassicIndicatorWindowViewInterface *
    createClassicIndicatorWindow(KDDockWidgets::Core::ClassicDropIndicatorOverlay *overlay,
                                 KDDockWidgets::Core::View *) const override
    {
        return new DropZones(overlay);
    }
    KDDockWidgets::Core::View *createRubberBand(KDDockWidgets::Core::View *parent) const override
    {
        return new DropHighlight(KDDockWidgets::QtQuick::asQQuickItem(parent));
    }
};

std::function<std::optional<qreal>(QWindow *)> g_nativeScale; // tests

// dockchrome::kScaleSettleMs: a floating window's size in device-independent
// pixels across a change of its screen's scale. Qt keeps a window's size
// when Windows asks it to (WM_DPICHANGED); a live change of the display
// scale left the floating Line editor's window at its pixel size instead
// (native gate at 100 to 125 %), so its size became 80 % of the panel's
// minimum and the engine, which keeps the panel at least that big, drew it
// cut off. The keeper remembers the window's size and its scales while
// they settle, and after a change of Qt's device pixel ratio gives the
// window that size back.
//
// dockchrome::kScreenLossWatchMs: a floating window whose screen goes away
// (a monitor unplugged, Windows' "PC screen only") is placed whole on the
// main window's screen at its size before the loss. The Windows gate
// (2026-10-15, a panel on a 150 % monitor beside a 100 % one) saw the window
// system move the window onto the remaining screen at a pixel size, then the
// window come back at its size in device-independent pixels but with only
// its left 173 px on the screen, or as a window larger than its panel. The
// keeper places the window once the window system has stopped moving it,
// and again if it moves it within kScreenLossWatchMs of the loss.
class ScaleKeeper final : public QObject {
public:
    explicit ScaleKeeper(QWindow *window)
        : QObject(window)
        , m_window(window)
    {
        window->installEventFilter(this);
        m_settle.setSingleShot(true);
        m_settle.setInterval(dockchrome::kScaleSettleMs);
        connect(&m_settle, &QTimer::timeout, this, &ScaleKeeper::settle);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &ScaleKeeper::screenRemoved);
    }

    bool eventFilter(QObject *, QEvent *event) override
    {
        // After a screen loss, whatever the window system does to the window
        // waits for it to finish, and none of it is a size to keep.
        if (!m_lost.hasExpired()) {
            switch (event->type()) {
            case QEvent::Show:
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::DevicePixelRatioChange:
                m_settle.start();
                return false;
            default:
                break;
            }
        }
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
            // A device pixel ratio that changed without its event (the
            // window's screen's scale taken another way) settles as well,
            // and so does a resize for a new screen's scale.
            if (m_known && (!qFuzzyCompare(m_window->devicePixelRatio(), m_dpr) || screenRunsAhead())) {
                if (!m_settle.isActive())
                    m_settle.start();
                break;
            }
            // Not while a scale change settles, nor when the window system
            // has a new scale that Qt has not taken yet (its resize for the
            // new scale, read at the old one). A resize of the user's own
            // within kScaleSettleMs of a change is not kept either: the
            // window takes its size from before the change.
            if (!m_settle.isActive() && (!m_known || !runsAhead()))
                remember();
            break;
        case QEvent::DevicePixelRatioChange:
            if (m_known)
                m_settle.start();
            break;
        default:
            break;
        }
        return false;
    }

private:
    bool runsAhead() const
    {
        const std::optional<qreal> native = dockchrome::nativeScale(m_window);
        return native && m_native && !qFuzzyCompare(*native, *m_native)
            && qFuzzyCompare(m_window->devicePixelRatio(), m_dpr);
    }

    // The window on a screen of another scale than it was remembered at,
    // before it takes that scale. Outside Windows, Qt resizes a window that
    // moved to such a screen first (its pixels at the new screen's scale:
    // QWindowPrivate's screenChanged handler) and updates its device pixel
    // ratio after; that resize, read at the old ratio, is not the size to
    // keep. The CI's offscreen runs on Linux: a 600 x 220 panel moved onto
    // a 150 % screen was kept at 400 x 147.
    bool screenRunsAhead() const
    {
        const QScreen *screen = m_window->screen();
        return screen && screen != m_screen && !qFuzzyCompare(screen->devicePixelRatio(), m_dpr);
    }

    void remember()
    {
        m_size = m_window->size();
        m_dpr = m_window->devicePixelRatio();
        m_native = dockchrome::nativeScale(m_window);
        m_screen = m_window->screen();
        m_known = true;
    }

    // Qt emits screenRemoved before it moves the screen's windows to the
    // primary screen; on Windows the window system may have moved the
    // window to another screen already, so the screen it was remembered on
    // counts as well.
    void screenRemoved(QScreen *screen)
    {
        if (!m_window->isVisible() || (screen != m_window->screen() && screen != m_screen))
            return;
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            return; // the compositor places windows (docs/docking.md)
        qCDebug(lcDocking) << "screen removed:" << m_window->title() << "was on" << screen->name()
                           << m_window->frameGeometry();
        m_lost.setRemainingTime(dockchrome::kScreenLossWatchMs);
        m_settle.start();
    }

    // The main window's screen (the primary one without it).
    QScreen *returnScreen() const
    {
        const QList<QScreen *> screens = QGuiApplication::screens();
        QWindow *main = m_window->transientParent();
        if (main && screens.contains(main->screen()))
            return main->screen();
        if (QScreen *primary = QGuiApplication::primaryScreen())
            return primary;
        return screens.isEmpty() ? nullptr : screens.first();
    }

    // The window whole on `screen`, at `size` (without its frame), as close
    // to where it is as fits.
    void place(QScreen *screen, QSize size)
    {
        const QRect available = screen->availableGeometry();
        const QMargins margins = m_window->frameMargins();
        size = size.boundedTo(available.size().shrunkBy(margins));
        const QRect frame = m_window->frameGeometry();
        const QSize outer = size.grownBy(margins);
        const int x = std::clamp(frame.x(), available.left(), std::max(available.left(), available.right() + 1 - outer.width()));
        const int y = std::clamp(frame.y(), available.top(), std::max(available.top(), available.bottom() + 1 - outer.height()));
        const QRect want(QPoint(x + margins.left(), y + margins.top()), size);
        qCDebug(lcDocking) << "returned to" << screen->name() << m_window->title() << m_window->geometry() << "->" << want;
        if (want != m_window->geometry())
            m_window->setGeometry(want);
    }

    // The engine's floating window view (a child of the window's content item).
    KDDockWidgets::QtQuick::FloatingWindow *view() const
    {
        auto *quick = qobject_cast<QQuickWindow *>(m_window);
        if (QQuickItem *content = quick ? quick->contentItem() : nullptr)
            for (QQuickItem *child : content->childItems())
                if (auto *view = qobject_cast<KDDockWidgets::QtQuick::FloatingWindow *>(child))
                    return view;
        return nullptr;
    }

    // The window's minimum size for its panels: the engine's floating window
    // view's (a child of the window's content item), with whatever of the
    // window lies outside the view, and at least the window's own minimum.
    QSize minimum() const
    {
        QSize least = m_window->minimumSize();
        if (KDDockWidgets::QtQuick::FloatingWindow *view = this->view()) {
            const QSize outside(std::max(0, m_window->width() - qCeil(view->width())),
                                std::max(0, m_window->height() - qCeil(view->height())));
            least = least.expandedTo(view->minSize() + outside);
        }
        return least;
    }

    void settle()
    {
        if (!m_window->isVisible()) {
            m_known = false;
            m_lost = QDeadlineTimer();
            return;
        }
        const bool lost = !m_lost.hasExpired();
        QScreen *screen = lost ? returnScreen() : m_window->screen();
        QSize want = (m_known ? m_size : m_window->size()).expandedTo(minimum());
        if (screen)
            want = want.boundedTo(screen->availableGeometry().size());
        qCDebug(lcDocking) << (lost ? "screen loss:" : "scale change:") << m_window->title() << "dpr" << m_dpr << "->"
                           << m_window->devicePixelRatio() << "size" << m_window->size() << "->" << want;
        if (lost && screen)
            place(screen, want);
        else if (want != m_window->size())
            m_window->resize(want);
        // The panel fills its window again: after a screen loss the engine's
        // view can keep a size from before it (the gate's 800 x 672 window
        // with the panel in its top-left two thirds). As the engine's own
        // window does on a resize (its QuickView::updateRootItemSize).
        if (KDDockWidgets::QtQuick::FloatingWindow *view = this->view();
            view && static_cast<QQuickItem *>(view)->size() != QSizeF(m_window->size()))
            view->KDDockWidgets::Core::View::setSize(m_window->size());
        remember();
    }

    QWindow *const m_window;
    QTimer m_settle;
    bool m_known = false;
    QSize m_size;
    qreal m_dpr = 1;
    std::optional<qreal> m_native;
    QPointer<QScreen> m_screen;
    QDeadlineTimer m_lost; // watching for the window system after a screen loss
};

// MuseScore's engine setup (its dockmodule.cpp, research §0): one header row
// (the title bar hides while tabs show, and a group always shows its tabs:
// DockTabBar.qml draws a lone panel's as a title bar), 1-pixel separators,
// and floating panels as tool windows. Hikari's headers draw no float button,
// so Flag_TitleBarNoFloatButton is not needed; the engine would also read it
// as part of Flag_AutoHideSupport (a mask test) and build side bars, which
// the QtQuick frontend cannot.
//
// Floating windows, on every platform: borderless tool windows,
// transparent around a drawn shadow (DockFloatingWindow.qml), resized
// through the window system (startSystemResize from the shadow). A
// header's title or tabs move them by the engine's drag, which tracks the
// pointer to show the drop highlight and dock (on Wayland, where the engine
// cannot place windows, a drag-and-drop). On Wayland and X11 the rest of
// the header moves the window through the window system (startSystemMove,
// DockSystemMoveArea.qml), so its snapping and tiling apply there; a system
// move hands the pointer to the window system, so that part does not dock
// (the user's choice of 2026-10-12, docs/docking.md). On Windows the
// whole header is the engine's drag (the owner's choice of 2026-10-15):
// Snap never takes the borderless tool window, so the split only cost
// docking there (Docking::platformUsesSystemMove). The engine's
// Aero-snap frame (Flag_AeroSnapWithClientDecos) is not used: it keeps the
// system's frame, which the borderless window has not.
void configureEngine()
{
    using KDDockWidgets::Config;
    Config &config = Config::self();
    config.setFlags(Config::Flag_HideTitleBarWhenTabsVisible | Config::Flag_AlwaysShowTabs);
    config.setSeparatorThickness(1);
    // The drop highlight is drawn in the window under the dragged panel:
    // the dragged window is see-through while it moves (where the window
    // system composites), as with the engine's own in-window indicators.
    config.setDraggedWindowOpacity(0.7);
    config.setInternalFlags(config.internalFlags() | Config::InternalFlag_UseTransparentFloatingWindow);
    KDDockWidgets::Core::FloatingWindow::s_windowFlagsOverride =
        Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    // Frameless is not enough on Wayland (window_decorations.h): the window
    // asks the compositor itself, before it is first shown. And the window
    // keeps its size across a change of its screen's scale.
    KDDockWidgets::QtQuick::FloatingWindow::setQuickWindowCreationCallback(
        [](QQuickView *window, KDDockWidgets::QtQuick::MainWindow *) {
            windowdecorations::requestNone(window);
            new ScaleKeeper(window);
        });
}
} // namespace

namespace {
std::optional<bool> g_compositing; // tests
} // namespace

bool dockchrome::windowSystemComposites()
{
    if (g_compositing)
        return *g_compositing;
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return true; // Wayland and Windows always composite
    // Qt's xcb integration tracks the compositing manager's selection
    // (_NET_WM_CM_S<n>) per screen.
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    QScreen *screen = QGuiApplication::primaryScreen();
    return native && screen && native->nativeResourceForScreen(QByteArrayLiteral("compositingenabled"), screen);
}

void dockchrome::overrideCompositing(std::optional<bool> composites)
{
    g_compositing = composites;
}

std::optional<qreal> dockchrome::nativeScale(QWindow *window)
{
    if (g_nativeScale)
        return g_nativeScale(window);
#ifdef Q_OS_WIN
    // handle(), not winId(): winId() would make the native window now.
    if (window && window->handle())
        if (const UINT dpi = GetDpiForWindow(reinterpret_cast<HWND>(window->winId())))
            return qreal(dpi) / USER_DEFAULT_SCREEN_DPI;
#else
    Q_UNUSED(window);
#endif
    return std::nullopt;
}

void dockchrome::overrideNativeScale(std::function<std::optional<qreal>(QWindow *)> scale)
{
    g_nativeScale = std::move(scale);
}

int dockchrome::floatingShadow()
{
    // MuseScore's DOCK_WINDOW_SHADOW, drawn in the window's transparent
    // margin; none where that margin would come out opaque.
    return windowSystemComposites() ? 8 : 0;
}

bool attachDocking(QQmlEngine &engine)
{
    if (g_attached == &engine)
        return true;
    if (g_attached)
        return false; // one engine at a time
    if (g_everAttached && KDDockWidgets::Core::Platform::instance()) {
        // The previous engine is gone, and its docks with it. The platform
        // connects focusObjectChanged to a lambda on itself with qApp as the
        // context; that connection would outlive it, so it goes first.
        QObject::disconnect(qApp, &QGuiApplication::focusObjectChanged, qApp, nullptr);
        delete KDDockWidgets::DockRegistry::self();
        delete KDDockWidgets::Core::Platform::instance();
    }
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
    KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
    KDDockWidgets::Config::self().setViewFactory(new ViewFactory);
    configureEngine();
    g_attached = &engine;
    g_everAttached = true;
    return true;
}

int keepFloatingPanelsOnScreen()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")) || !KDDockWidgets::Core::Platform::instance())
        return 0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return 0;
    // A header is reachable when this much of its window's top strip (the
    // drawn shadow, then the header) is on a screen.
    const int kStrip = dockchrome::floatingShadow() + dockchrome::kHeaderHeight;
    constexpr int kMinimumWidth = 80;
    int moved = 0;
    for (auto *floating : KDDockWidgets::DockRegistry::self()->floatingWindows()) {
        QQuickItem *item = KDDockWidgets::QtQuick::asQQuickItem(floating->view());
        QWindow *window = item ? item->window() : nullptr;
        if (!window || !window->isVisible())
            continue;
        const QRect frame = window->frameGeometry();
        const QRect strip(frame.topLeft(), QSize(frame.width(), std::min(frame.height(), kStrip)));
        const bool reachable = std::any_of(screens.begin(), screens.end(), [&](QScreen *s) {
            const QRect on = s->availableGeometry().intersected(strip);
            return on.width() >= std::min(kMinimumWidth, strip.width()) && on.height() > 0;
        });
        qCDebug(lcDocking) << "floating window" << window->title() << frame << "reachable" << reachable << "screens"
                           << [&] {
                                  QStringList out;
                                  for (QScreen *s : screens)
                                      out << QStringLiteral("%1 %2,%3 %4x%5").arg(s->name()).arg(s->availableGeometry().x())
                                                 .arg(s->availableGeometry().y()).arg(s->availableGeometry().width())
                                                 .arg(s->availableGeometry().height());
                                  return out;
                              }();
        if (reachable)
            continue;
        // Onto the main window's screen (the primary one without it), as
        // close to where it was as fits.
        QWindow *main = window->transientParent();
        QScreen *target = main && screens.contains(main->screen()) ? main->screen() : QGuiApplication::primaryScreen();
        if (!target)
            target = screens.first();
        const QRect available = target->availableGeometry();
        const QMargins frameMargins = window->frameMargins();
        QSize size = window->size();
        size.setWidth(std::min(size.width(), available.width() - frameMargins.left() - frameMargins.right()));
        size.setHeight(std::min(size.height(), available.height() - frameMargins.top() - frameMargins.bottom()));
        if (size != window->size())
            window->resize(size);
        const QSize outer = size.grownBy(frameMargins);
        const int x = std::clamp(frame.x(), available.left(), std::max(available.left(), available.right() + 1 - outer.width()));
        const int y = std::clamp(frame.y(), available.top(), std::max(available.top(), available.bottom() + 1 - outer.height()));
        window->setFramePosition(QPoint(x, y));
        qCDebug(lcDocking) << "moved onto" << target->name() << "at" << QPoint(x, y);
        ++moved;
    }
    return moved;
}

Docking::Docking(QObject *parent)
    : QObject(parent)
{
    connect(windowdecorations::notifier(), &windowdecorations::Notifier::changed, this, [this] {
        ++m_windowTitleRevision;
        emit windowSystemTitlesChanged();
    });
}

bool Docking::windowSystemTitle(QWindow *window) const
{
    return windowdecorations::hasSystemTitleBar(window);
}

bool Docking::windowSystemFrame(QWindow *window) const
{
    return windowdecorations::systemFrame(window) != windowdecorations::SystemFrame::None;
}

int Docking::windowShadow(QWindow *window) const
{
    return windowSystemFrame(window) ? 0 : floatingShadow();
}

bool Docking::platformUsesSystemMove(QStringView platformName)
{
    // Not on Windows: Snap never takes the borderless tool window (the VM
    // gate and real hardware, 2026-10-15), and a press right of the title
    // that never docks lost the owner's first drag to dock.
    return platformName.startsWith(u"wayland") || platformName == u"xcb";
}

bool Docking::platformUsesSystemMove()
{
    return platformUsesSystemMove(QGuiApplication::platformName());
}

void Docking::setSystemMove(bool on)
{
    if (on == m_systemMove)
        return;
    m_systemMove = on;
    emit systemMoveChanged();
}

namespace {

// Installed after the engine's own filter on the window, so it sees the
// close first (Qt runs the filter installed last first).
class CloseGuard : public QObject {
public:
    CloseGuard(QQuickWindow *window, QJSValue admit) : QObject(window), m_admit(std::move(admit)) {}

    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::Close || !m_admit.isCallable())
            return false;
        const QJSValue admitted = m_admit.call();
        if (admitted.isError()) {
            qCWarning(lcDocking) << "the close guard failed:" << admitted.toString();
            return false;
        }
        if (admitted.toBool())
            return false;
        event->ignore();
        return true;
    }

private:
    QJSValue m_admit;
};

} // namespace

void Docking::guardClose(QQuickWindow *window, const QJSValue &admit)
{
    if (window)
        window->installEventFilter(new CloseGuard(window, admit));
}

bool Docking::startSystemMove(QQuickItem *from)
{
    QQuickWindow *window = from ? from->window() : nullptr;
    if (!window)
        return false;
    emit systemMoveRequested(window);
    return window->startSystemMove();
}

void Docking::requestKeyboardContextMenu()
{
    QWindow *window = QGuiApplication::focusWindow();
    if (!window)
        return;
    // No position: Qt Quick asks the items at the focused item.
    QContextMenuEvent request(QContextMenuEvent::Keyboard, QPoint(), QPoint());
    QGuiApplication::sendEvent(window, &request);
}

QString Docking::dockNameAt(QObject *tabBar, int index) const
{
    auto *bar = qobject_cast<KDDockWidgets::QtQuick::TabBar *>(tabBar);
    KDDockWidgets::Core::DockWidget *dock = bar ? bar->dockWidgetModel()->dockWidgetAt(index) : nullptr;
    return dock ? dock->uniqueName() : QString();
}

QObject *Docking::mainDropArea(const QString &uniqueName) const
{
    auto *registry = KDDockWidgets::Core::Platform::instance() ? KDDockWidgets::DockRegistry::self() : nullptr;
    KDDockWidgets::Core::MainWindow *main = registry ? registry->mainWindowByName(uniqueName) : nullptr;
    KDDockWidgets::Core::DropArea *dropArea = main ? main->dropArea() : nullptr;
    return dropArea ? KDDockWidgets::QtQuick::asQQuickItem(dropArea->view()) : nullptr;
}

bool Docking::resizeInLayout(const QString &uniqueName, int left, int top, int right, int bottom)
{
    auto *registry = KDDockWidgets::Core::Platform::instance() ? KDDockWidgets::DockRegistry::self() : nullptr;
    KDDockWidgets::Core::DockWidget *dock = registry ? registry->dockByName(uniqueName) : nullptr;
    if (!dock || dock->isFloating() || !dock->isOpen())
        return false;
    dock->resizeInLayout(left, top, right, bottom);
    return true;
}

namespace {
KDDockWidgets::Core::MainWindow *mainWindowByName(const QString &uniqueName)
{
    auto *registry = KDDockWidgets::Core::Platform::instance() ? KDDockWidgets::DockRegistry::self() : nullptr;
    return registry ? registry->mainWindowByName(uniqueName) : nullptr;
}

// Decision 63: the window holding a main window's docking area is never
// smaller than the docked layout's minimum, so no docked panel is cut off
// past the window's edge. The engine sizes only a main window that is its
// window's root item ("there's no layout propagation" in QtQuick), and the
// DockingArea sits in an ApplicationWindow under its menu bar, so the
// window's minimum stayed 0 x 0 (the Windows gate, 2026-10-16: the window
// at 480 wide with the Grid and the Line editor past its right edge).
// The layout's view reports every change of its minimum (a panel docked,
// floated, shown or hidden, a panel's own minimum changed) as
// geometryUpdated; the window's minimum is that minimum plus whatever of the
// window lies outside the docking area (the menu bar, the status bar),
// within the screen's available area. A window already smaller (a size set
// by a program, a restored geometry) grows to it.
class LayoutMinimumKeeper final : public QObject {
public:
    explicit LayoutMinimumKeeper(KDDockWidgets::Core::MainWindow *main, KDDockWidgets::QtQuick::View *view)
        : QObject(view)
        , m_main(main)
        , m_view(view)
    {
        setObjectName(QStringLiteral("hikariLayoutMinimumKeeper"));
        // A layout change reports several intermediate minimums (Reset layout
        // and a restore take panels out and put them back one by one), and
        // the window would grow to the largest of them, so the window follows
        // the layout once it has been still for a moment.
        m_settle.setSingleShot(true);
        m_settle.setInterval(kSettleMs);
        connect(&m_settle, &QTimer::timeout, this, &LayoutMinimumKeeper::apply);
        connect(view, &KDDockWidgets::QtQuick::View::geometryUpdated, this, &LayoutMinimumKeeper::schedule);
        connect(view, &QQuickItem::widthChanged, this, &LayoutMinimumKeeper::schedule);
        connect(view, &QQuickItem::heightChanged, this, &LayoutMinimumKeeper::schedule);
        connect(view, &QQuickItem::windowChanged, this, &LayoutMinimumKeeper::schedule);
        apply();
    }

    static LayoutMinimumKeeper *of(KDDockWidgets::QtQuick::View *view)
    {
        // The class has no Q_OBJECT, so it is found by its object name.
        return view ? static_cast<LayoutMinimumKeeper *>(view->findChild<QObject *>(QStringLiteral("hikariLayoutMinimumKeeper"), Qt::FindDirectChildrenOnly)) : nullptr;
    }

    // The window's minimum as the layout needs it; empty while the docking
    // area has no size yet.
    QSize wanted() const
    {
        QQuickWindow *window = item()->window();
        if (!window || m_view->width() <= 0 || m_view->height() <= 0)
            return {};
        const QSize outside(std::max(0, window->width() - qCeil(m_view->width())),
                            std::max(0, window->height() - qCeil(m_view->height())));
        QSize least = m_main->layout()->layoutMinimumSize() + outside;
        if (QScreen *screen = window->screen())
            least = least.boundedTo(screen->availableGeometry().size());
        return least;
    }

    void apply()
    {
        if (KDDockWidgets::LayoutSaver::restoreInProgress()) {
            m_settle.start();
            return;
        }
        QQuickWindow *window = item()->window();
        const QSize least = wanted();
        if (!window || least.isEmpty())
            return;
        if (window->minimumSize() != least) {
            qCDebug(lcDocking) << "main window minimum" << window->minimumSize() << "->" << least;
            window->setMinimumSize(least);
        }
        if (window->width() < least.width() || window->height() < least.height())
            window->resize(window->size().expandedTo(least));
    }

private:
    // KDDockWidgets' View hides QQuickItem::window() with its own.
    QQuickItem *item() const { return m_view; }

    void schedule() { m_settle.start(); }

    static constexpr int kSettleMs = 100;
    KDDockWidgets::Core::MainWindow *m_main;
    KDDockWidgets::QtQuick::View *m_view;
    QTimer m_settle;
};

KDDockWidgets::QtQuick::View *mainWindowView(KDDockWidgets::Core::MainWindow *main)
{
    return main ? qobject_cast<KDDockWidgets::QtQuick::View *>(KDDockWidgets::QtQuick::asQQuickItem(main->view())) : nullptr;
}
} // namespace

bool Docking::followLayoutMinimum(const QString &mainName)
{
    KDDockWidgets::Core::MainWindow *main = mainWindowByName(mainName);
    KDDockWidgets::QtQuick::View *view = mainWindowView(main);
    if (!view)
        return false;
    if (LayoutMinimumKeeper *keeper = LayoutMinimumKeeper::of(view))
        keeper->apply();
    else
        new LayoutMinimumKeeper(main, view);
    return true;
}

QSize Docking::layoutMinimumSize(const QString &mainName) const
{
    KDDockWidgets::Core::MainWindow *main = mainWindowByName(mainName);
    return main ? QSize(main->layout()->layoutMinimumSize()) : QSize();
}

namespace {
KDDockWidgets::Core::DockWidget *dockByName(const QString &uniqueName)
{
    auto *registry = KDDockWidgets::Core::Platform::instance() ? KDDockWidgets::DockRegistry::self() : nullptr;
    return registry ? registry->dockByName(uniqueName) : nullptr;
}
} // namespace

int fitFloatingLayouts()
{
    if (!KDDockWidgets::Core::Platform::instance())
        return 0;
    int fitted = 0;
    for (KDDockWidgets::Core::FloatingWindow *floating : KDDockWidgets::DockRegistry::self()->floatingWindows()) {
        KDDockWidgets::Core::DropArea *area = floating->dropArea();
        if (!area || !area->view())
            continue;
        // The engine builds a floating window's layout at its area's size
        // then, with the window's title bar shown (there are no groups yet
        // to tell it otherwise), hides the title bar once the layout has a
        // lone group, and does not follow its area's resizes while the
        // restore runs: the layout kept the area's height under the title
        // bar, 35 pixels short of it.
        const QSize areaSize = area->view()->size();
        const QSize layoutSize = area->layoutSize();
        const QQuickItem *item = KDDockWidgets::QtQuick::asQQuickItem(floating->view());
        qCDebug(lcDocking) << "restored floating window" << (item && item->window() ? item->window()->title() : QString())
                           << "layout" << layoutSize << "area" << areaSize;
        if (areaSize.isEmpty() || layoutSize == areaSize)
            continue;
        area->setLayoutSize(areaSize);
        ++fitted;
    }
    return fitted;
}

void refreshDockConstraints()
{
    if (!KDDockWidgets::Core::Platform::instance())
        return;
    for (KDDockWidgets::Core::Group *group : KDDockWidgets::DockRegistry::self()->groups()) {
        auto *view = qobject_cast<KDDockWidgets::QtQuick::Group *>(KDDockWidgets::QtQuick::asQQuickItem(group->view()));
        if (!view)
            continue;
        view->updateConstraints();
        // The engine takes a restored size as it was saved, below a minimum
        // that has grown since: grow the group across its bottom (or right)
        // edge, else its top (or left) one, as a separator drag would.
        KDDockWidgets::Core::DockWidget *dock = group->currentDockWidget();
        if (!dock || !dock->isOpen() || dock->isFloating())
            continue;
        const QSize need = view->minSize();
        for (int pass = 0; pass < 2; ++pass) {
            const QSize have = dock->sizeInLayout();
            const int dw = need.width() - have.width(), dh = need.height() - have.height();
            if (dw <= 0 && dh <= 0)
                break;
            if (pass == 0)
                dock->resizeInLayout(0, 0, std::max(0, dw), std::max(0, dh));
            else
                dock->resizeInLayout(std::max(0, dw), std::max(0, dh), 0, 0);
        }
    }
}

bool Docking::setMinimumSize(const QString &uniqueName, QSize size)
{
    KDDockWidgets::Core::DockWidget *dock = dockByName(uniqueName);
    if (!dock)
        return false;
    // The dock widget's own minimum: the engine takes the larger of it and
    // its guest's. The QtQuick frontend propagates constraints by hand: the
    // group holding the panel recomputes its own (header included) and has
    // the layout honour them.
    dock->view()->setMinimumSize(size);
    for (KDDockWidgets::Core::Group *group : KDDockWidgets::DockRegistry::self()->groups()) {
        if (group->containsDockWidget(dock)) {
            if (auto *view = qobject_cast<KDDockWidgets::QtQuick::Group *>(KDDockWidgets::QtQuick::asQQuickItem(group->view())))
                view->updateConstraints();
        }
    }
    return true;
}

QSize Docking::minimumSize(const QString &uniqueName) const
{
    KDDockWidgets::Core::DockWidget *dock = dockByName(uniqueName);
    return dock ? dock->view()->minSize() : QSize();
}

void Docking::setPanelHeader(const QString &uniqueName, bool horizontal, QQuickItem *toolbar)
{
    m_headers.insert(uniqueName, Header{horizontal, toolbar});
    ++m_revision;
    emit headersChanged();
}

bool Docking::hasPlace(const QString &uniqueName) const
{
    if (!KDDockWidgets::Core::Platform::instance())
        return false;
    const auto *dock = KDDockWidgets::DockRegistry::self()->dockByName(uniqueName);
    return dock && (dock->isOpen() || dock->hasPreviousDockedLocation());
}

bool Docking::isHorizontal(const QString &uniqueName) const
{
    return m_headers.value(uniqueName).horizontal;
}

QQuickItem *Docking::toolbar(const QString &uniqueName) const
{
    return m_headers.value(uniqueName).toolbar;
}

void Docking::requestMenu(const QString &uniqueName, QQuickItem *anchor)
{
    if (!uniqueName.isEmpty())
        emit menuRequested(uniqueName, anchor);
}

void Docking::setOpenMenu(const QString &uniqueName)
{
    if (uniqueName == m_openMenu)
        return;
    m_openMenu = uniqueName;
    emit openMenuChanged();
}

bool Docking::toggleFloating(const QString &uniqueName)
{
    KDDockWidgets::Core::DockWidget *dock = dockByName(uniqueName);
    if (!dock)
        return false;
    return dock->setFloating(!dock->isFloating());
}

namespace {
KDDockWidgets::Core::Group *groupOf(KDDockWidgets::Core::DockWidget *dock)
{
    if (!dock)
        return nullptr;
    for (KDDockWidgets::Core::Group *group : KDDockWidgets::DockRegistry::self()->groups())
        if (group->containsDockWidget(dock))
            return group;
    return nullptr;
}
} // namespace

int Docking::tabIndex(const QString &uniqueName) const
{
    KDDockWidgets::Core::DockWidget *dock = dockByName(uniqueName);
    KDDockWidgets::Core::Group *group = groupOf(dock);
    return group ? group->indexOfDockWidget(dock) : -1;
}

int Docking::tabCount(const QString &uniqueName) const
{
    KDDockWidgets::Core::Group *group = groupOf(dockByName(uniqueName));
    return group ? group->dockWidgetCount() : 0;
}

bool Docking::moveTab(const QString &uniqueName, int step)
{
    KDDockWidgets::Core::DockWidget *dock = dockByName(uniqueName);
    KDDockWidgets::Core::Group *group = groupOf(dock);
    if (!group || step == 0)
        return false;
    const int from = group->indexOfDockWidget(dock);
    const int to = from + step;
    if (from < 0 || to < 0 || to >= group->dockWidgetCount())
        return false;
    // The engine's TabBar::moveTabTo moves the tab in its own list only (the
    // pinned QtQuick frontend's view of it is a stub), so the tab bar's model
    // takes the panel out and back in at its new place. Taking the panel out
    // of its group and back in does not work: the engine's reparenting on
    // insertion takes a panel out of the group it is in.
    auto *bar = qobject_cast<KDDockWidgets::QtQuick::TabBar *>(KDDockWidgets::QtQuick::asQQuickItem(group->tabBar()->view()));
    if (!bar)
        return false;
    group->tabBar()->moveTabTo(from, to);
    bar->dockWidgetModel()->remove(dock);
    bar->dockWidgetModel()->insert(dock, to);
    // While the panel's row was out the header made its neighbour current
    // (in the model, not in the engine): the panel is current again in both,
    // and the header's index follows it.
    bar->setCurrentIndex(to);
    group->setCurrentDockWidget(dock);
    if (QQuickItem *header = bar->tabBarQmlItem())
        header->setProperty("currentTabIndex", to);
    return group->indexOfDockWidget(dock) == to;
}

} // namespace hikari::ui
