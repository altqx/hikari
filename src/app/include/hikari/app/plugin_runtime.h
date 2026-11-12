#pragma once

#include "hikari/api/api.h"
#include "hikari/app/plugin_files.h"
#include "hikari/app/plugin_manager.h"
#include "hikari/app/plugin_network.h"
#include "hikari/app/plugin_ui.h"
#include "hikari/backends/plugin_process.h"
#include "hikari/plugins/installer.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <deque>
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
    Q_PROPERTY(hikari::app::PluginUi *ui READ ui CONSTANT)
    // Restricted plugins run in an operating-system sandbox here.
    Q_PROPERTY(bool sandboxed READ sandboxed CONSTANT)

public:
    struct Wiring {
        QString helperPath;
        PluginManager::Folders folders;
        api::Api *api = nullptr;
        ui::LogController *log = nullptr;
        std::function<QString()> language; // "" for English
        backends::PluginProcess::Limits limits;
        std::size_t memoryLimit = 256u << 20;
        // How long a plugin waits for another plugin's edit before it is told HikariSub is busy.
        int transactionWaitMs = 5000;
        // The global plugin network switch; unset: off.
        std::function<bool()> networkAllowed;
        // Null: the runtime makes its own.
        QNetworkAccessManager *network = nullptr;
        // Restricted plugins start in pluginProfile's sandbox; unrestricted ones never do.
        bool sandbox = false;
        // A QML panel's api.setSetting: the refusal, or "" once kept. Unset: the runtime keeps it itself.
        std::function<QString(const QString &plugin, const QString &setting, const QString &json)> setSetting;
        // The Project the user works in, for the `project` place.
        std::function<std::optional<api::ProjectState>()> project;
    };

    explicit PluginRuntime(Wiring wiring, QObject *parent = nullptr);
    ~PluginRuntime() override;

    // Starts a helper for each enabled plugin that has none or changed version, and stops the rest.
    Q_INVOKABLE void sync();
    QVariantList commands() const { return m_commands; }
    // False when no running plugin has the command.
    Q_INVOKABLE bool run(const QString &commandId);
    Q_INVOKABLE void forceStop(const QString &pluginId);
    // Starts a stopped plugin, or one that didn't start, again.
    Q_INVOKABLE void restart(const QString &pluginId);
    // A setting the plugin manager changed, so that hikari.settings.get sees it at once.
    void settingChanged(const QString &pluginId, const QString &setting, const QString &json);
    PluginUi *ui() const { return m_ui; }
    bool sandboxed() const;
    // The plugin's file handles and the folders granted for this run end, as when it stops.
    Q_INVOKABLE void endFileAccess(const QString &pluginId);
    backends::PluginProcess *process(const QString &pluginId) const;
    // Running a command or an event right now.
    bool busy(const QString &pluginId) const;

signals:
    void commandsChanged();
    // The plugin finished a command or an event, or stopped.
    void idle(const QString &pluginId);

private:
    struct Running;
    std::unique_ptr<Running> launch(const std::string &id, plugins::Manifest manifest);
    void serve(Running &plugin, const backends::PluginProcess::ServiceRequest &request,
               backends::PluginProcess::ServiceReply reply);
    void unsubscribe(Running &plugin);
    void relist();
    void lock(Running &plugin, backends::PluginProcess::ServiceReply reply);
    void unlock(Running &plugin);
    void releaseLock(Running &plugin);
    void endAccess(Running &plugin);
    void wireQmlPanels();
    std::optional<std::string> setSetting(const std::string &plugin, const std::string &setting, const api::Json &value);

    struct LockWaiter {
        Running *plugin = nullptr;
        backends::PluginProcess::ServiceReply reply;
        std::unique_ptr<QTimer> timeout;
    };

    Wiring m_wiring;
    std::optional<plugins::Installer> m_installer;
    std::map<std::string, std::unique_ptr<Running>> m_plugins;
    Running *m_lockHolder = nullptr;
    std::deque<LockWaiter> m_lockWaiters;
    QVariantList m_commands;
    PluginUi *m_ui = nullptr;
    std::unique_ptr<QNetworkAccessManager> m_ownNetwork;
    PluginNetwork *m_network = nullptr;
    std::unique_ptr<PluginFiles> m_files;
};

}
