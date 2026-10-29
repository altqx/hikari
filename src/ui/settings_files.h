#pragma once

#include "hikari/application/owned_file.h"
#include "hikari/application/settings.h"

#include <toml++/toml.hpp>

#include <QJsonObject>
#include <QString>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace hikari::ui {

class SettingsFiles final : public application::SettingsStorage,
                            public std::enable_shared_from_this<SettingsFiles> {
public:
    static constexpr std::int64_t kVersion = 1;
    enum File { Settings, Hotkeys, State };
    static constexpr int kFiles = 3;

    static std::shared_ptr<SettingsFiles> open(const QString &settingsFile);
    static bool exist(const QString &settingsFile);
    ~SettingsFiles() override;

    std::optional<application::SettingValue> read(const application::SettingDefinition &setting) const override;
    void write(const application::SettingDefinition &setting, const application::SettingValue &value) override;
    void remove(const application::SettingDefinition &setting) override;

    void flush();

    static File fileOf(const application::SettingDefinition &setting);
    static QString fileName(File file);
    QString path(File file) const;

    const std::vector<application::FileProblem> &problems() const { return m_problems; }
    bool converted() const { return m_converted; }
    const std::vector<std::string> &ignored() const { return m_ignored; }

    explicit SettingsFiles(QString folder);

private:
    void load();
    bool convertIni();
    // MUST KILL: put/erase are write/remove that skip recording the change
    void put(const application::SettingDefinition &setting, const application::SettingValue &value);
    void erase(const application::SettingDefinition &setting);
    void mergeFromDisk(File file);
    // MUST KILL: writeConversion must write settings.toml last; its existence marks the conversion done
    bool writeConversion();
    QString legacyIni() const { return m_folder + QStringLiteral("/hikari.ini"); }
    void changed(File file);
    void queueFlush(int delayMs);
    bool writeFile(File file);

    QString m_folder;
    toml::table m_settings;
    toml::table m_hotkeys;
    QJsonObject m_state;
    bool m_dirty[kFiles] = {};
    std::set<std::string> m_changed[kFiles];
    bool m_writable[kFiles] = {true, true, true};
    bool m_flushQueued = false;
    bool m_conversionPending = false;
    bool m_converting = false;
    std::vector<application::FileProblem> m_problems;
    std::vector<std::string> m_ignored;
    bool m_converted = false;
};

}
