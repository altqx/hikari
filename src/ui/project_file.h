#pragma once

#include "hikari/application/owned_file.h"
#include "hikari/application/settings.h"

#include <toml++/toml.hpp>

#include <QString>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::ui {

// A Project's settings: .hikari/project.toml, with .hikari/project.local.toml over it. Only projectable
// settings are read, and only project.toml is written, one changed key at a time.
class ProjectFile final : public application::SettingsStorage {
public:
    static constexpr std::int64_t kVersion = 1;
    enum File { Shared, Local };

    explicit ProjectFile(QString root);

    QString path(File file) const;
    // The file a projectable setting's value comes from.
    std::optional<File> source(const application::SettingDefinition &setting) const;

    std::optional<application::SettingValue> read(const application::SettingDefinition &setting) const override;
    // A value equal to the default is kept: it still overrides the user's.
    void write(const application::SettingDefinition &setting, const application::SettingValue &value) override;
    void remove(const application::SettingDefinition &setting) override;
    bool lastWriteFailed() const { return m_writeFailed; }
    // Either file has a top-level table named `name`.
    bool hasTable(std::string_view name) const;
    // `fonts = [...]`, resolved against the Project folder; project.local.toml's list replaces project.toml's.
    std::vector<QString> fontFolders() const;

    const std::vector<application::FileProblem> &problems() const { return m_problems; }
    // Settings the files set that a Project may never set; they stay in the file, unused.
    const std::vector<std::string> &refused() const { return m_refused; }
    // `[translation]` entries that look like a key, token, address or other credential; never read or written.
    const std::vector<std::string> &credentials() const { return m_credentials; }

private:
    void load(File file);
    void store(const application::SettingDefinition &setting, const std::optional<application::SettingValue> &value);

    QString m_root;
    toml::table m_tables[2];
    bool m_writable = true;
    bool m_writeFailed = false;
    std::vector<application::FileProblem> m_problems;
    std::vector<std::string> m_refused;
    std::vector<std::string> m_credentials;
};

}
