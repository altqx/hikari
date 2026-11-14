#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

/// application/side_bars.h for QML. The side bars travel as their JSON; `second` names the second side bar.
class SideBarModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int minimumWidth READ minimumWidth CONSTANT)

public:
    using QObject::QObject;
    int minimumWidth() const;
    /// Test fixtures only: a first start puts no panels into the side bars. No setting or shipped path calls it.
    static bool setFirstStartEmpty(bool empty);

    Q_INVOKABLE QString defaults(bool project) const;
    Q_INVOKABLE QString migrated(bool project, int windowWidth, const QStringList &rightOfGrid) const;
    /// {first, second}, each {panels, shown, width, hidden}; the defaults for JSON that doesn't read.
    Q_INVOKABLE QVariantMap read(const QString &json) const;
    Q_INVOKABLE bool mayHold(const QString &panel) const;
    Q_INVOKABLE QString moved(const QString &json, const QString &panel, bool second, int index) const;
    Q_INVOKABLE QString removed(const QString &json, const QString &panel) const;
    Q_INVOKABLE QString shown(const QString &json, const QString &panel) const;
    Q_INVOKABLE QString iconClicked(const QString &json, const QString &panel) const;
    Q_INVOKABLE QString toggled(const QString &json, bool second) const;
    Q_INVOKABLE QString setHidden(const QString &json, bool second, bool hidden) const;
    Q_INVOKABLE QString widened(const QString &json, bool second, int width) const;
    /// Whether the first side bar is the one on the left.
    Q_INVOKABLE bool firstOnLeft(const QString &edge, bool rightToLeft) const;
    /// "first", "second", or "" for a plugin panel that stays in the middle.
    Q_INVOKABLE QString pluginSideBar(const QString &area, const QString &edge, bool rightToLeft) const;
};

}
