#pragma once

// CF1 (#488): the settings registry's files in the user folder.
// - settings.toml: what a user sets on purpose. A dotted id is tables and a
//   key ([audio] cacheFilesLimit = 20); lists are arrays; the tag buttons
//   are [[editor.tagButton]] slots and the script dialogs' presets a table
//   of arrays of tables.
// - hotkeys.toml: the changes from the default shortcuts, one table per
//   window ([global] GLOBAL_SAVE_SUBS = "Ctrl+Shift+S"), "" for a default
//   taken away; the scripts' shortcuts in [automation].
// - state.json: what the app remembers: the [workspace] keys (window and
//   dialog places, picker folders), recents and the script dialogs' last
//   answers.
// Each holds only values that differ from the default, under `version = 1`.
// At the first start without settings.toml, hikari.ini is converted into the
// three and becomes hikari.ini.old. A file that does not read is kept as
// <name>.broken and its settings take their defaults (owned_file.h).
//
// Every SettingsStore of the process on one folder shares one SettingsFiles,
// as they shared QSettings' cache before: writes are kept until the next
// event loop pass, flush(), or the last store going. A file is written over
// what it holds by then, with only this process's changes put in, under a
// lock file in the folder: another HikariSub on the folder keeps its own.

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

    // The files in the folder of `settingsFile` (its settings.toml).
    static std::shared_ptr<SettingsFiles> open(const QString &settingsFile);
    // Whether that folder holds settings: one of the files, or hikari.ini.
    static bool exist(const QString &settingsFile);
    ~SettingsFiles() override;

    std::optional<application::SettingValue> read(const application::SettingDefinition &setting) const override;
    void write(const application::SettingDefinition &setting, const application::SettingValue &value) override;
    void remove(const application::SettingDefinition &setting) override;

    // Writes the files that changed.
    void flush();

    static File fileOf(const application::SettingDefinition &setting);
    static QString fileName(File file);
    QString path(File file) const;

    // What this start found: the files that did not read, whether hikari.ini
    // was converted, and what a file holds that is ignored.
    const std::vector<application::FileProblem> &problems() const { return m_problems; }
    bool converted() const { return m_converted; }
    const std::vector<std::string> &ignored() const { return m_ignored; }

    explicit SettingsFiles(QString folder);

private:
    void load();
    bool convertIni();
    // write() and remove() without recording the change.
    void put(const application::SettingDefinition &setting, const application::SettingValue &value);
    void erase(const application::SettingDefinition &setting);
    void mergeFromDisk(File file);
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
    // The settings changed here since each file was last written.
    std::set<std::string> m_changed[kFiles];
    // A broken file that could not be moved aside is never written.
    bool m_writable[kFiles] = {true, true, true};
    bool m_flushQueued = false;
    // hikari.ini was converted but the files could not all be written.
    bool m_conversionPending = false;
    std::vector<application::FileProblem> m_problems;
    std::vector<std::string> m_ignored;
    bool m_converted = false;
};

} // namespace hikari::ui
