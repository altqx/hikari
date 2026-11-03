#pragma once

#include <QObject>
#include <QQuickItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace hikari::ui::style {

// The menu every plain text field opens, made once per field from the type setSource names.
class TextMenus : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    static TextMenus *create(QQmlEngine *engine, QJSEngine *js);
    static void setSource(const QString &module, const QString &type);

    Q_INVOKABLE QObject *menuFor(QQuickItem *editor);
    // Clears the menu another style gave field's ContextMenu and returns that ContextMenu,
    // so the caller can answer its requested(position) with menuFor(field).
    Q_INVOKABLE QObject *adopt(QQuickItem *field);
};

}
