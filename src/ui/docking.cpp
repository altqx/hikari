#include "docking.h"

#include <kddockwidgets/KDDockWidgets.h>
// Private engine headers, isolated here: re-initializing the platform for a
// new QML engine has no public entry point (the engine's own
// tests_deinitPlatform does the same, in developer builds only).
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/Platform.h>
#include <kddockwidgets/qtquick/Platform.h>

#include <QGuiApplication>
#include <QPointer>
#include <QQmlEngine>

namespace hikari::ui {

namespace {
QPointer<QQmlEngine> g_attached;
bool g_everAttached = false;
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
    g_attached = &engine;
    g_everAttached = true;
    return true;
}

} // namespace hikari::ui
