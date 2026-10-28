#pragma once

// What a shell menu item needs from Qt Quick Controls' Menu beyond its QML
// API (ShellMenuItem.qml).

#include <QGuiApplication>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class ShellMenus : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // R1-check-items-close: a Menu closes its whole chain when an item
    // without a submenu is triggered (QQuickMenuPrivate::onItemTriggered,
    // connected to the item's triggered when the menu takes the item). Cutting
    // that one connection keeps the menu open for the item; the item's own
    // handlers still run. False when there was none to cut.
    Q_INVOKABLE bool keepOpen(QObject *item) const
    {
        auto *menu = item ? item->property("menu").value<QObject *>() : nullptr;
        return menu && QObject::disconnect(item, SIGNAL(triggered()), menu, nullptr);
    }
    // Whether Shift is down for the input event being handled.
    Q_INVOKABLE bool shiftHeld() const
    {
        return QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    }
};

} // namespace hikari::ui
