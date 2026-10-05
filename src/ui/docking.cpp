#include "docking.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
// Private engine headers, isolated here: re-initializing the platform for a
// new QML engine has no public entry point (the engine's own
// tests_deinitPlatform does the same, in developer builds only).
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/MainWindow.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Platform.h>
#include <kddockwidgets/core/indicators/ClassicDropIndicatorOverlay.h>
#include <kddockwidgets/core/views/ClassicIndicatorWindowViewInterface.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/View.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <kddockwidgets/qtquick/views/Group.h>
#include <kddockwidgets/qtquick/views/TabBar.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <qpa/qplatformnativeinterface.h>

#include <algorithm>

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
// through the window system (startSystemResize from the shadow). Dragging a
// header moves them by the engine's drag, which tracks the pointer to show
// the drop highlight and dock; a system move would hand the pointer to the
// window system and lose the drop. Only on Wayland, where the engine cannot
// place windows, does the header's free part move its window through the
// compositor (startSystemMove, DockTabBar.qml), while its title and tabs
// stay the engine's drag (a drag-and-drop there). The engine's Aero-snap
// frame (Flag_AeroSnapWithClientDecos) is not used: it keeps the system's
// frame, which the borderless window has not.
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

bool Docking::platformNeedsSystemMove()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

void Docking::setSystemMove(bool on)
{
    if (on == m_systemMove)
        return;
    m_systemMove = on;
    emit systemMoveChanged();
}

bool Docking::startSystemMove(QQuickItem *from)
{
    QQuickWindow *window = from ? from->window() : nullptr;
    if (!window)
        return false;
    emit systemMoveRequested(window);
    return window->startSystemMove();
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
KDDockWidgets::Core::DockWidget *dockByName(const QString &uniqueName)
{
    auto *registry = KDDockWidgets::Core::Platform::instance() ? KDDockWidgets::DockRegistry::self() : nullptr;
    return registry ? registry->dockByName(uniqueName) : nullptr;
}
} // namespace

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

} // namespace hikari::ui
