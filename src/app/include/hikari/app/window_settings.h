#pragma once

#include "hikari/application/settings.h"

#include <QString>

#include <memory>
#include <string_view>
#include <utility>

namespace hikari::ui {
class ProjectFile;
}

namespace hikari::app {

// A shell window's effective settings: its Project's over the user's (ADR 0030, ADR 0025).
class WindowSettings {
public:
    explicit WindowSettings(application::SettingsStorage &user);
    ~WindowSettings();

    // An empty root clears the Project. The same root reloads its files.
    void setProject(const QString &root, bool trusted);
    const QString &root() const { return m_root; }
    bool trusted() const { return m_storage.trusted(); }
    ui::ProjectFile *project() const { return m_project.get(); }

    application::Settings &settings() { return m_settings; }
    const application::Settings &settings() const { return m_settings; }
    // The Project sets `id` for this window.
    bool overridden(std::string_view id) const;
    // Reads this window's values; writes and removals go to project.toml. Without a Project nothing is written.
    application::SettingsStorage &projectWrites() { return m_projectWrites; }
    // A write or removal through projectWrites() failed since the last call.
    bool takeProjectWriteFailure() { return std::exchange(m_projectWrites.failed, false); }

private:
    class ProjectWrites final : public application::SettingsStorage {
    public:
        explicit ProjectWrites(WindowSettings &window) : m_window(window) {}
        std::optional<application::SettingValue> read(const application::SettingDefinition &setting) const override;
        void write(const application::SettingDefinition &setting, const application::SettingValue &value) override;
        void remove(const application::SettingDefinition &setting) override;

        bool failed = false;

    private:
        WindowSettings &m_window;
    };

    application::LayeredSettingsStorage m_storage;
    application::Settings m_settings;
    QString m_root;
    std::shared_ptr<ui::ProjectFile> m_project;
    ProjectWrites m_projectWrites{*this};
};

}
