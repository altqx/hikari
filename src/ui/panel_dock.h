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
    /// Deletes a closed dock: KDDW leaves it unparented, so it would outlive its window. An open one goes with its group.
    ~PanelDock() override;

    Q_INVOKABLE void open();
    Q_INVOKABLE void show();

Q_SIGNALS:
    void openedWithoutPlace();

private:
    bool handOver();
};

}
