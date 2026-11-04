#pragma once

#include <QQuickItem>
#include <QStringList>

namespace menu_rows {

/// The rows a Menu shows, in order: each item's objectName, a submenu's own objectName, and "|" for a separator.
inline QStringList shown(QObject *menu)
{
    QStringList out;
    const int count = menu->property("count").toInt();
    for (int i = 0; i < count; ++i) {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
        if (!item || item->height() <= 0)
            continue;
        if (item->inherits("QQuickMenuSeparator")) {
            out << QStringLiteral("|");
            continue;
        }
        auto *sub = item->property("subMenu").value<QObject *>();
        out << (sub ? sub->objectName() : item->objectName());
    }
    return out;
}

}
