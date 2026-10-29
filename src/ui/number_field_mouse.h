#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class NumberFieldMouse : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // MUST KILL: value() returns NaN for "keep the current value"
    Q_INVOKABLE double value(const QString &text) const;
    Q_INVOKABLE QVariantMap wheel(double value, int notches, double from, double to) const;
    Q_INVOKABLE QVariantMap dragMove(const QVariantMap &drag, int x, int y, double from, double to) const;
};

}
