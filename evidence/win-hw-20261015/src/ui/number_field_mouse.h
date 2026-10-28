#pragma once

// E4: NumCtrl's mouse (legacy HikariSub/NumCtrl.cpp:243-319) for the Line
// editor's Layer and margin fields (NumberField.qml), through
// application::numberFieldValue, wheelNumberField and dragNumberField.
// Values are NumCtrl's own `value`, which the field keeps.

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
    // OnNumWrite's value of typed text; NaN where NumCtrl keeps its value.
    Q_INVOKABLE double value(const QString &text) const;
    // The wheel from `value` over [from, to]: {value, text}, the value
    // clamped to the range, with text "" when it did not change.
    Q_INVOKABLE QVariantMap wheel(double value, int notches, double from, double to) const;
    // A right-button drag {value, x, y}: each move answers {drag, text}
    // (text "" for no step).
    Q_INVOKABLE QVariantMap dragMove(const QVariantMap &drag, int x, int y, double from, double to) const;
};

} // namespace hikari::ui
