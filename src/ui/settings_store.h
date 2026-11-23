#pragma once

#include "hikari/application/owned_file.h"
#include "hikari/application/settings.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
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
    // Why settings.toml can't be read after a hand edit; empty while it reads.
    Q_PROPERTY(QString fileProblem READ fileProblem NOTIFY fileProblemChanged)
public:
    explicit SettingsStore(QString file = {}, QObject *parent = nullptr);
    ~SettingsStore() override;

    const QString &file() const { return m_file; }
    application::Settings &settings() { return *m_settings; }
    const application::Settings &settings() const { return *m_settings; }
    application::SettingsStorage &storage() { return *m_storage; }

    Q_INVOKABLE QVariant value(const QString &id) const;
    Q_INVOKABLE bool isSet(const QString &id) const;
    Q_INVOKABLE bool setValue(const QString &id, const QVariant &value);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();
    // Its stored value isn't the default.
    Q_INVOKABLE bool differs(const QString &id) const;
    Q_INVOKABLE QStringList changedSettings() const;
    // "settings" or "hotkeys"; empty without files.
    Q_INVOKABLE QString filePath(const QString &file) const;
    QString fileProblem() const { return m_fileProblem; }
    void reloadFile();
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
    void fileProblemChanged();

private:
    QString m_file;
    std::shared_ptr<application::SettingsStorage> m_storage;
    std::shared_ptr<SettingsFiles> m_files;
    std::unique_ptr<application::Settings> m_settings;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
    QString m_fileProblem;
};

}
