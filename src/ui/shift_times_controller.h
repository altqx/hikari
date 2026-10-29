#pragma once

#include "hikari/application/shift_times.h"
#include "settings_store.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace hikari::ui {

class ShiftTimesController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(QVariantMap settings READ settingsMap WRITE setSettingsMap NOTIFY changed)
    Q_PROPERTY(QStringList profiles READ profileNames NOTIFY changed)
    Q_PROPERTY(QStringList collapsedSections READ collapsedSections WRITE setCollapsedSections NOTIFY
                   collapsedSectionsChanged)
public:
    explicit ShiftTimesController(QString settingsFile = {}, QObject *parent = nullptr);
    explicit ShiftTimesController(SettingsStore &settings, QObject *parent = nullptr);

    const application::ShiftTimesSettings &settings() const { return m_settings; }
    QVariantMap settingsMap() const;
    void setSettingsMap(const QVariantMap &map);
    QStringList profileNames() const;
    Q_INVOKABLE void saveProfile(const QString &name);
    Q_INVOKABLE void loadProfile(const QString &name);
    Q_INVOKABLE void removeProfile(const QString &name);
    QStringList collapsedSections() const { return m_collapsed; }
    void setCollapsedSections(const QStringList &ids);

signals:
    void changed();
    void collapsedSectionsChanged();

private:
    void load();
    void assign(const QVariantMap &map);
    void save() const;

    std::unique_ptr<SettingsStore> m_ownedStore;
    SettingsStore *m_store;
    application::ShiftTimesSettings m_settings;
    QStringList m_profiles;
    QStringList m_collapsed;
};

}
