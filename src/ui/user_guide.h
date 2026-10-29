#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class UserGuide : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    Q_INVOKABLE bool hasPage(const QString &kind, const QString &id) const;
    Q_INVOKABLE void open(const QString &kind, const QString &id) { emit openRequested(kind, id); }

signals:
    void openRequested(const QString &kind, const QString &id);
};

}
