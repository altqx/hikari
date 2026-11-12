#pragma once

#include "hikari/api/api.h"
#include "hikari/plugins/installer.h"

#include <QString>

#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hikari::app {

class PluginUi;

// The files a plugin reaches: pickers, its data/ and cache/ folders, and the folders the user
// grants it. Every path becomes an API file handle only after it is checked to stay inside.
class PluginFiles {
public:
    struct Plugin {
        std::string id;
        std::string caller; // the API caller id
        std::vector<plugins::FileGrantRequest> folders;
    };

    // `installer` may be null: no data/, cache/ or remembered folders then. `project` answers the
    // Project the user works in (unset: never a Project).
    PluginFiles(api::Api *api, plugins::Installer *installer, PluginUi &ui,
                std::function<std::optional<api::ProjectState>()> project = {});

    void serve(const Plugin &plugin, const std::string &operation, const api::Json &params, api::Reply reply);
    // A file dropped on the plugin's panel, granted for reading: {file, name, access}.
    api::Json grantDropped(const Plugin &plugin, const QString &path);
    // The plugin stopped or lost a folder: its handles end, and so do folders granted for this run only.
    void end(const std::string &pluginId, const std::string &caller);

private:
    struct Place {
        std::filesystem::path root;
        bool writable = false;
    };
    void pick(const Plugin &plugin, const api::Json &params, api::Reply reply);
    // Asks the user for the folder unless it is granted; replies with the place or a refusal.
    void folder(const Plugin &plugin, const std::string &id, std::function<void(std::expected<Place, api::Error>)> done);
    void place(const Plugin &plugin, const std::string &name, std::function<void(std::expected<Place, api::Error>)> done);
    void open(const Plugin &plugin, const Place &place, const std::string &path, api::Reply reply);
    void entries(const Place &place, const std::string &path, api::Reply reply);
    api::Json grant(const Plugin &plugin, const std::filesystem::path &path, std::string name, bool writable);

    api::Api *m_api = nullptr;
    plugins::Installer *m_installer = nullptr;
    PluginUi &m_ui;
    std::function<std::optional<api::ProjectState>()> m_project;
    std::map<std::string, std::map<std::string, plugins::FileGrant>> m_thisRun; // by plugin id, then folder id
};

}
