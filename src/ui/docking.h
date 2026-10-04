#pragma once

// D1: Hikari's adapter around the docking engine (KDDockWidgets, QtQuick
// frontend; docs/qt/docking.md). The engine is process-wide and serves one
// QML engine; nothing outside this adapter talks to it directly.

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace hikari::ui {

// Binds the docking engine to the QML engine that is about to load the shell.
// Call it before loading Main. The production shell has one engine for its
// lifetime. When an earlier engine has been destroyed (tests create one per
// case) the docking engine is re-initialized for the new one; while an
// earlier engine still lives, the call does nothing and returns false.
// The engine draws Hikari's title and tab bars (DockTitleBar.qml,
// DockTabBar.qml), which name their buttons for assistive technology.
bool attachDocking(QQmlEngine &engine);

// Keeps every floating panel window's title bar on an available screen
// (docs/qt/docking.md: removed monitors return floating groups to the main
// screen). On Wayland the compositor places windows and nothing is moved.
// Returns how many windows were moved.
int keepFloatingPanelsOnScreen();

// Engine views the shell's QML needs (DockTabBar.qml, Main.qml).
class Docking : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // Floats the panel of tab `index` of `tabBar` (the engine's TabBarView).
    Q_INVOKABLE bool floatTab(QObject *tabBar, int index);
    // The drop area view of the main window `uniqueName` (the DockingArea's).
    // On Wayland the engine drags panels with real drag-and-drop and takes a
    // drop through a QML DropArea whose dropAreaCpp property names this view.
    Q_INVOKABLE QObject *mainDropArea(const QString &uniqueName) const;
};

} // namespace hikari::ui
