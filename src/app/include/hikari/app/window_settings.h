#pragma once

#include "hikari/application/settings.h"

#include <QString>

#include <memory>
#include <string_view>

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

private:
    application::LayeredSettingsStorage m_storage;
    application::Settings m_settings;
    QString m_root;
    std::shared_ptr<ui::ProjectFile> m_project;
};

}
