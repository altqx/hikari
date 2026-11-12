#include "panel_dock.h"

#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QMetaMethod>

namespace hikari::ui {

PanelDock::~PanelDock()
{
    if (auto *dock = controller(); dock && !dock->isOpen() && !dock->isInSideBar())
        delete dock;
}

void PanelDock::open()
{
    if (!handOver())
        DockWidgetInstantiator::open();
}

void PanelDock::show()
{
    if (!handOver())
        DockWidgetInstantiator::show();
}

bool PanelDock::handOver()
{
    const auto *dock = controller();
    if (!dock || dock->isOpen() || dock->hasPreviousDockedLocation() || KDDockWidgets::LayoutSaver::restoreInProgress()
        || !isSignalConnected(QMetaMethod::fromSignal(&PanelDock::openedWithoutPlace)))
        return false;
    Q_EMIT openedWithoutPlace();
    return true;
}

}
