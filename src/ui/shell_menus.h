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
        QQuickItem *parent = item ? item->parentItem() : nullptr;
        if (!parent || !next || item == next || next->parentItem() != parent)
            return false;
        // QQuickItem emits childrenChanged from removeChild before it clears the child's parentItem.
        const QList<QQuickItem *> siblings = parent->childItems();
        if (!siblings.contains(item) || !siblings.contains(next))
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
