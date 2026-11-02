#pragma once

#include "hikari/api/api.h"
#include "hikari/app/plugin_manager.h"
#include "hikari/backends/plugin_process.h"
#include "hikari/plugins/installer.h"

#include <QObject>
#include <QVariantList>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace hikari::ui {
class LogController;
}

namespace hikari::app {

// One hikari-plugin-helper per enabled plugin, and the commands they contribute.
class PluginRuntime : public QObject {
    Q_OBJECT
    // [{id: "plugin.<plugin id>.<command>", plugin, pluginName, title, titles: [pluginName, title]}],
    // by plugin name, then in manifest order.
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)

public:
    struct Wiring {
        QString helperPath;
        PluginManager::Folders folders;
        api::Api *api = nullptr;
        ui::LogController *log = nullptr;
        std::function<QString()> language; // "" for English
        backends::PluginProcess::Limits limits;
        std::size_t memoryLimit = 256u << 20;
    };

    explicit PluginRuntime(Wiring wiring, QObject *parent = nullptr);
    ~PluginRuntime() override;

    // Starts a helper for each enabled plugin that has none or changed version, and stops the rest.
    Q_INVOKABLE void sync();
    QVariantList commands() const { return m_commands; }
    // False when no running plugin has the command.
    Q_INVOKABLE bool run(const QString &commandId);
    Q_INVOKABLE void forceStop(const QString &pluginId);
    backends::PluginProcess *process(const QString &pluginId) const;

signals:
    void commandsChanged();

private:
    struct Running;
    std::unique_ptr<Running> launch(const std::string &id, plugins::Manifest manifest);
    void serve(Running &plugin, const backends::PluginProcess::ServiceRequest &request,
               backends::PluginProcess::ServiceReply reply);
    void unsubscribe(Running &plugin);
    void relist();

    Wiring m_wiring;
    std::optional<plugins::Installer> m_installer;
    std::map<std::string, std::unique_ptr<Running>> m_plugins;
    QVariantList m_commands;
};

}
