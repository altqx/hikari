#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
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
    Q_INVOKABLE QString standardKeyText(int key) const
    {
        const QList<QKeySequence> keys = QKeySequence::keyBindings(QKeySequence::StandardKey(key));
        return keys.isEmpty() ? QString() : keys.first().toString(QKeySequence::NativeText);
    }
    // The editor's own key handling runs the row, so a row does what its key does in that field.
    Q_INVOKABLE void pressKeys(QQuickItem *target, int key) const
    {
        const QList<QKeySequence> keys = QKeySequence::keyBindings(QKeySequence::StandardKey(key));
        if (!target || keys.isEmpty())
            return;
        const QKeyCombination combination = keys.first()[0];
        QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
        QKeyEvent release(QEvent::KeyRelease, combination.key(), combination.keyboardModifiers());
        QCoreApplication::sendEvent(target, &press);
        QCoreApplication::sendEvent(target, &release);
    }
    Q_INVOKABLE QString keyText(const QString &portable) const
    {
        return QKeySequence(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
    }
    Q_INVOKABLE bool shiftHeld() const
    {
        return QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    }
};

}
