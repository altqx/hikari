#pragma once

#include <QGuiApplication>
#include <QObject>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class ShellMenus : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // QQuickMenuPrivate connects each item's triggered() to closing the menu chain; cutting it keeps the menu open.
    Q_INVOKABLE bool keepOpen(QObject *item) const
    {
        auto *menu = item ? item->property("menu").value<QObject *>() : nullptr;
        return menu && QObject::disconnect(item, SIGNAL(triggered()), menu, nullptr);
    }
    Q_INVOKABLE bool stackBefore(QQuickItem *item, QQuickItem *next) const
    {
        if (!item || !next || item == next || item->parentItem() != next->parentItem())
            return false;
        item->stackBefore(next);
        return true;
    }
    Q_INVOKABLE bool shiftHeld() const
    {
        return QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    }
};

}
