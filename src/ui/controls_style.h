#pragma once

// The Qt Quick Controls style in use, for QML that works around one style's
// look (ShellMenuItem.qml).

#include <QObject>
#include <QQuickStyle>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class ControlsStyle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // "Windows", "Fusion", ...
    Q_PROPERTY(QString name READ name CONSTANT)
public:
    using QObject::QObject;
    static QString name() { return QQuickStyle::name(); }
};

} // namespace hikari::ui
