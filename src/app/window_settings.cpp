#include "hikari/app/window_settings.h"

#include "project_file.h"

namespace hikari::app {

WindowSettings::WindowSettings(application::SettingsStorage &user) : m_storage(user), m_settings(m_storage) {}

WindowSettings::~WindowSettings() = default;

void WindowSettings::setProject(const QString &root, bool trusted)
{
    m_root = root;
    m_project = root.isEmpty() ? nullptr : std::make_shared<ui::ProjectFile>(root);
    m_storage.setProject(m_project, trusted);
}

bool WindowSettings::overridden(std::string_view id) const
{
    const auto *setting = application::findSetting(id);
    return setting && m_storage.fromProject(*setting);
}

std::optional<application::SettingValue> WindowSettings::ProjectWrites::read(const application::SettingDefinition &setting) const
{
    return m_window.m_storage.read(setting);
}

void WindowSettings::ProjectWrites::write(const application::SettingDefinition &setting, const application::SettingValue &value)
{
    if (!m_window.m_project)
        return;
    m_window.m_project->write(setting, value);
    failed = failed || m_window.m_project->lastWriteFailed();
}

void WindowSettings::ProjectWrites::remove(const application::SettingDefinition &setting)
{
    if (!m_window.m_project)
        return;
    m_window.m_project->remove(setting);
    failed = failed || m_window.m_project->lastWriteFailed();
}

}
