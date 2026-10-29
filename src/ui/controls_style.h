#pragma once

#include <QObject>
#include <QQuickStyle>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class ControlsStyle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString name READ name CONSTANT)
public:
    using QObject::QObject;
    static QString name() { return QQuickStyle::name(); }
};

}
