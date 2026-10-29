#include "docking.h"
#include "window_decorations.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>
// Private KDDW headers: re-initializing the platform for a new QML engine has no public entry point.
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

// KDDW 2.4.1's QtQuick frontend has no Segmented indicators, so the Classic overlay's window and rubber band are replaced.
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

std::function<std::optional<qreal>(QWindow *)> g_nativeScale;

// Windows can leave a floating window at its pixel size after a live scale change, or partly off-screen after a screen loss.
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
            if (m_known && (!qFuzzyCompare(m_window->devicePixelRatio(), m_dpr) || screenRunsAhead())) {
                if (!m_settle.isActive())
                    m_settle.start();
                break;
            }
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

    // Outside Windows, Qt resizes a window moved to a screen of another scale before it updates its devicePixelRatio.
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

    // Qt emits screenRemoved before it moves the screen's windows; Windows may have moved the window already.
    void screenRemoved(QScreen *screen)
    {
        if (!m_window->isVisible() || (screen != m_window->screen() && screen != m_screen))
            return;
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            return; // the Wayland compositor places windows
        qCDebug(lcDocking) << "screen removed:" << m_window->title() << "was on" << screen->name()
                           << m_window->frameGeometry();
        m_lost.setRemainingTime(dockchrome::kScreenLossWatchMs);
        m_settle.start();
    }

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

    KDDockWidgets::QtQuick::FloatingWindow *view() const
    {
        auto *quick = qobject_cast<QQuickWindow *>(m_window);
        if (QQuickItem *content = quick ? quick->contentItem() : nullptr)
            for (QQuickItem *child : content->childItems())
                if (auto *view = qobject_cast<KDDockWidgets::QtQuick::FloatingWindow *>(child))
                    return view;
        return nullptr;
    }

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
        // After a screen loss KDDW's view can keep its old size; resize it as KDDW's QuickView::updateRootItemSize does.
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
    QDeadlineTimer m_lost;
};

// Not Flag_TitleBarNoFloatButton: KDDW reads it as part of Flag_AutoHideSupport and builds side bars QtQuick cannot.
void configureEngine()
{
    using KDDockWidgets::Config;
    Config &config = Config::self();
    config.setFlags(Config::Flag_HideTitleBarWhenTabsVisible | Config::Flag_AlwaysShowTabs);
    config.setSeparatorThickness(1);
    config.setDraggedWindowOpacity(0.7);
    config.setInternalFlags(config.internalFlags() | Config::InternalFlag_UseTransparentFloatingWindow);
    KDDockWidgets::Core::FloatingWindow::s_windowFlagsOverride =
        Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    // Frameless is not enough on Wayland: the window asks the compositor itself, before it is first shown.
    KDDockWidgets::QtQuick::FloatingWindow::setQuickWindowCreationCallback(
        [](QQuickView *window, KDDockWidgets::QtQuick::MainWindow *) {
            windowdecorations::requestNone(window);
            new ScaleKeeper(window);
        });
}
}

namespace {
std::optional<bool> g_compositing;
}

bool dockchrome::windowSystemComposites()
{
    if (g_compositing)
        return *g_compositing;
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return true; // Wayland and Windows always composite
    // Qt's xcb integration tracks the compositing manager's selection (_NET_WM_CM_S<n>) per screen.
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
    return windowSystemComposites() ? 8 : 0;
}

bool attachDocking(QQmlEngine &engine)
{
    if (g_attached == &engine)
        return true;
    if (g_attached)
        return false;
    if (g_everAttached && KDDockWidgets::Core::Platform::instance()) {
        // KDDW's platform connects focusObjectChanged to itself with qApp as context; that connection would outlive it.
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
    // Not on Windows: Snap never takes the borderless tool window.
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

// Qt runs the last-installed filter first, so this sees the close before KDDW's.
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

}

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

// KDDW sizes only a main window that is its window's root item (no layout propagation in QtQuick).
class LayoutMinimumKeeper final : public QObject {
public:
    explicit LayoutMinimumKeeper(KDDockWidgets::Core::MainWindow *main, KDDockWidgets::QtQuick::View *view)
        : QObject(view)
        , m_main(main)
        , m_view(view)
    {
        setObjectName(QStringLiteral("hikariLayoutMinimumKeeper"));
        // KDDW reports intermediate minimums while a restore re-adds panels one by one.
        m_settle.setSingleShot(true);
        m_settle.setInterval(kSettleMs);
        connect(&m_settle, &QTimer::timeout, this, &LayoutMinimumKeeper::apply);
        connect(view, &KDDockWidgets::QtQuick::View::geometryUpdated, this, &LayoutMinimumKeeper::schedule);
        connect(view, &QQuickItem::widthChanged, this, &LayoutMinimumKeeper::schedule);
        connect(view, &QQuickItem::heightChanged, this, &LayoutMinimumKeeper::schedule);
        connect(view, &QQuickItem::windowChanged, this, &LayoutMinimumKeeper::schedule);
        apply();
    }

    // MUST KILL: LayoutMinimumKeeper is found by object name because it has no Q_OBJECT
    static LayoutMinimumKeeper *of(KDDockWidgets::QtQuick::View *view)
    {
        return view ? static_cast<LayoutMinimumKeeper *>(view->findChild<QObject *>(QStringLiteral("hikariLayoutMinimumKeeper"), Qt::FindDirectChildrenOnly)) : nullptr;
    }

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
}

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
}

int fitFloatingLayouts()
{
    if (!KDDockWidgets::Core::Platform::instance())
        return 0;
    int fitted = 0;
    for (KDDockWidgets::Core::FloatingWindow *floating : KDDockWidgets::DockRegistry::self()->floatingWindows()) {
        KDDockWidgets::Core::DropArea *area = floating->dropArea();
        if (!area || !area->view())
            continue;
        // KDDW lays out a restored floating window under its title bar, hides the bar, and does not follow the area while restoring.
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
        // KDDW takes a restored size as it was saved, below a minimum that has grown since.
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
    // The QtQuick frontend propagates constraints by hand: the group must recompute its own.
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
}

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
    // KDDW's TabBar::moveTabTo moves only its own list (the QtQuick view is a stub), so the model's row moves too.
    auto *bar = qobject_cast<KDDockWidgets::QtQuick::TabBar *>(KDDockWidgets::QtQuick::asQQuickItem(group->tabBar()->view()));
    if (!bar)
        return false;
    group->tabBar()->moveTabTo(from, to);
    bar->dockWidgetModel()->remove(dock);
    bar->dockWidgetModel()->insert(dock, to);
    // Removing the row made its neighbour current in the model only.
    bar->setCurrentIndex(to);
    group->setCurrentDockWidget(dock);
    if (QQuickItem *header = bar->tabBarQmlItem())
        header->setProperty("currentTabIndex", to);
    return group->indexOfDockWidget(dock) == to;
}

}
