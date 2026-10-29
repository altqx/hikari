#pragma once

#include "hikari/application/owned_file.h"
#include "hikari/application/settings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <string>
#include <vector>

namespace hikari::ui {

class SettingsFiles;

class SettingsStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
public:
    explicit SettingsStore(QString file = {}, QObject *parent = nullptr);
    ~SettingsStore() override;

    const QString &file() const { return m_file; }
    application::Settings &settings() { return *m_settings; }
    const application::Settings &settings() const { return *m_settings; }

    Q_INVOKABLE QVariant value(const QString &id) const;
    Q_INVOKABLE bool isSet(const QString &id) const;
    Q_INVOKABLE bool setValue(const QString &id, const QVariant &value);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();
    void sync();
    std::vector<application::FileProblem> problems() const;
    bool converted() const;
    std::vector<std::string> ignored() const;
    static bool filesExist(const QString &file);

    bool boolean(const char *id) const { return m_settings->boolean(id); }
    int integer(const char *id) const { return static_cast<int>(m_settings->integer(id)); }
    qint64 integer64(const char *id) const { return m_settings->integer(id); }
    QString text(const char *id) const;
    QStringList list(const char *id) const;
    bool contains(const char *id) const { return m_settings->isSet(id); }
    void set(const char *id, const QVariant &value) { setValue(QLatin1String(id), value); }

    static QVariant toVariant(const application::SettingValue &value);
    static application::SettingValue fromVariant(const QVariant &value);

signals:
    void changed(const QString &id);

private:
    QString m_file;
    std::shared_ptr<application::SettingsStorage> m_storage;
    std::shared_ptr<SettingsFiles> m_files;
    std::unique_ptr<application::Settings> m_settings;
};

}
