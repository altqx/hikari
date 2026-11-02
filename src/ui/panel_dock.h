#pragma once

#include <kddockwidgets/qtquick/DockWidgetInstantiator.h>

#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

/// A dock whose open() and show() leave a panel with no place to openedWithoutPlace, when something handles it,
/// instead of the engine's floating window.
class PanelDock : public KDDockWidgets::DockWidgetInstantiator {
    Q_OBJECT
    QML_ELEMENT

public:
    using DockWidgetInstantiator::DockWidgetInstantiator;

    Q_INVOKABLE void open();
    Q_INVOKABLE void show();

Q_SIGNALS:
    void openedWithoutPlace();

private:
    bool handOver();
};

}
