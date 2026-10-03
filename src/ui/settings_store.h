#pragma once

// O1: the settings registry over the INI file the application names (or in
// memory without one). Values live under "<scope>/<SettingId>"; the keys the
// rewrite used before the registry are moved onto their settings once, and
// "registry/schema" records that the move happened.

#include "hikari/application/settings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace hikari::ui {

class SettingsStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
public:
    static constexpr int kSchema = 1;

    explicit SettingsStore(QString file = {}, QObject *parent = nullptr);
    ~SettingsStore() override;

    const QString &file() const { return m_file; }
    application::Settings &settings() { return *m_settings; }
    const application::Settings &settings() const { return *m_settings; }

    Q_INVOKABLE QVariant value(const QString &id) const;
    Q_INVOKABLE bool isSet(const QString &id) const;
    Q_INVOKABLE bool setValue(const QString &id, const QVariant &value);
    Q_INVOKABLE void reset(const QString &id);
    // Legacy "Set default" (config::ResetDefault).
    Q_INVOKABLE void resetAll();

    bool boolean(const char *id) const { return m_settings->boolean(id); }
    int integer(const char *id) const { return static_cast<int>(m_settings->integer(id)); }
    qint64 integer64(const char *id) const { return m_settings->integer(id); }
    QString text(const char *id) const;
    QStringList list(const char *id) const;
    bool contains(const char *id) const { return m_settings->isSet(id); }
    void set(const char *id, const QVariant &value) { setValue(QLatin1String(id), value); }

    // The persisted key of a setting ("profile/grid.hideColumns").
    static QString keyOf(const application::SettingDefinition &setting);
    static QVariant toVariant(const application::SettingValue &value);
    static application::SettingValue fromVariant(const QVariant &value);

signals:
    // A setting's effective value changed.
    void changed(const QString &id);

private:
    void migrateInterimKeys();

    QString m_file;
    std::unique_ptr<application::SettingsStorage> m_storage;
    std::unique_ptr<application::Settings> m_settings;
};

} // namespace hikari::ui
