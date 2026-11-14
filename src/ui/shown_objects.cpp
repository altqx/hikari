#include "shown_objects.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QWindow>

namespace hikari::ui {
namespace {

void collect(QQuickItem *item, QSet<QObject *> &seen, QList<QObject *> &out)
{
    const auto add = [&](QObject *object) {
        if (!seen.contains(object)) {
            seen.insert(object);
            out << object;
        }
    };
    add(item);
    for (QObject *child : item->findChildren<QObject *>(Qt::FindDirectChildrenOnly))
        if (!qobject_cast<QQuickItem *>(child)) {
            add(child);
            for (QObject *inner : child->findChildren<QObject *>())
                add(inner);
        }
    for (QQuickItem *child : item->childItems())
        collect(child, seen, out);
}

}

QList<QObject *> shownObjects(QWindow *window)
{
    QList<QObject *> out;
    if (!window)
        return out;
    QSet<QObject *> seen;
    for (QObject *object : window->findChildren<QObject *>()) {
        seen.insert(object);
        out << object;
    }
    if (auto *quick = qobject_cast<QQuickWindow *>(window)) {
        QQuickItem *top = quick->contentItem();
        while (top && top->parentItem())
            top = top->parentItem();
        if (top)
            collect(top, seen, out);
    }
    return out;
}

}
