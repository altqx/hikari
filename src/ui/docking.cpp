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
#include <kddockwidgets/core/Platform.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/View.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <kddockwidgets/qtquick/views/TabBar.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

#include <algorithm>

namespace hikari::ui {

namespace {
Q_LOGGING_CATEGORY(lcDocking, "hikari.docking")
QPointer<QQmlEngine> g_attached;
bool g_everAttached = false;

// The engine's views with Hikari's title and tab bars.
class ViewFactory : public KDDockWidgets::QtQuick::ViewFactory {
public:
    QUrl titleBarFilename() const override { return QUrl(QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockTitleBar.qml")); }
    QUrl tabbarFilename() const override { return QUrl(QStringLiteral("qrc:/qt/qml/Hikari/Ui/DockTabBar.qml")); }
};
} // namespace

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
    // A title bar is reachable when this much of its top strip is on a screen.
    constexpr int kStrip = 30, kMinimumWidth = 80;
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

bool Docking::floatTab(QObject *tabBar, int index)
{
    auto *bar = qobject_cast<KDDockWidgets::QtQuick::TabBar *>(tabBar);
    KDDockWidgets::Core::DockWidget *dock = bar ? bar->dockWidgetModel()->dockWidgetAt(index) : nullptr;
    if (!dock || dock->isFloating())
        return false;
    dock->setFloating(true);
    return true;
}

QObject *Docking::mainDropArea(const QString &uniqueName) const
{
    auto *registry = KDDockWidgets::Core::Platform::instance() ? KDDockWidgets::DockRegistry::self() : nullptr;
    KDDockWidgets::Core::MainWindow *main = registry ? registry->mainWindowByName(uniqueName) : nullptr;
    KDDockWidgets::Core::DropArea *dropArea = main ? main->dropArea() : nullptr;
    return dropArea ? KDDockWidgets::QtQuick::asQQuickItem(dropArea->view()) : nullptr;
}

} // namespace hikari::ui
