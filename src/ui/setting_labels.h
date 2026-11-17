#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class SettingLabels : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // The label in the interface language; empty when the window shows the setting without one, or not at all.
    Q_INVOKABLE static QString of(const QString &setting);
    static QStringList labelled();
};

}
