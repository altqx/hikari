#pragma once

#include <algorithm>

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QKeySequence>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QScreen>
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
    /// How far a shown menu, drawn by `face`, moves to stay on its screen: above `anchor` when it opened below it and
    /// doesn't fit.
    Q_INVOKABLE QPointF shiftOnScreen(QQuickItem *face, QQuickItem *anchor) const
    {
        QQuickWindow *window = anchor ? anchor->window() : nullptr;
        if (!face || !window)
            return {};
        const QPointF shown = face->mapToGlobal(QPointF(0, 0));
        QScreen *screen = QGuiApplication::screenAt(anchor->mapToGlobal(QPointF(0, 0)).toPoint());
        if (!screen)
            screen = window->screen();
        if (!screen)
            return {};
        const QRectF anchored(anchor->mapToGlobal(QPointF(0, 0)), anchor->size());
        return fitted(screen->availableGeometry().toRectF(), anchored, shown, face->size()) - shown;
    }
    static QPointF fitted(const QRectF &available, const QRectF &anchor, QPointF at, QSizeF size)
    {
        if (at.y() + size.height() > available.bottom()) {
            const qreal above = (at.y() >= anchor.bottom() ? anchor.top() : at.y()) - size.height();
            at.setY(above >= available.top() ? above : available.bottom() - size.height());
        }
        at.setY(std::max(at.y(), available.top()));
        at.setX(std::max(std::min(at.x(), available.right() - size.width()), available.left()));
        return at;
    }
    Q_INVOKABLE void menuShowing()
    {
        m_pointerAtShow = QCursor::pos();
        m_pointerMoved = false;
    }
    // Qt Quick hovers a row that appears under a still pointer; native menus take hover only from motion, #1109.
    Q_INVOKABLE void takeHoverOnlyFromMotion(QQuickItem *row)
    {
        if (row)
            row->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_pointerMoved || (event->type() != QEvent::HoverEnter && event->type() != QEvent::HoverMove))
            return QObject::eventFilter(watched, event);
        const QPointF at = static_cast<QHoverEvent *>(event)->globalPosition();
        if (QLineF(at, QPointF(m_pointerAtShow)).length() < kStillPointerSlack)
            return true;
        m_pointerMoved = true;
        return QObject::eventFilter(watched, event);
    }

private:
    static constexpr qreal kStillPointerSlack = 2.0;
    QPoint m_pointerAtShow;
    bool m_pointerMoved = true;
};

}
