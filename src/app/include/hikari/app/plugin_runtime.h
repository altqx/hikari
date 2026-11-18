#pragma once

#include "hikari/application/row_marks.h"

#include "hikari/api/api.h"
#include "hikari/app/plugin_files.h"
#include "hikari/app/plugin_manager.h"
#include "hikari/app/plugin_network.h"
#include "hikari/app/plugin_programs.h"
#include "hikari/app/plugin_ui.h"
#include "hikari/backends/plugin_process.h"
#include "hikari/plugins/conditions.h"
#include "hikari/plugins/installer.h"
#include "hikari/plugins/project_plugins.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace hikari::ui {
class LogController;
}

namespace hikari::app {

// One hikari-plugin-helper per enabled plugin, and the commands they contribute.
class PluginRuntime : public QObject {
    Q_OBJECT
    // [{id: "plugin.<plugin id>.<command>", plugin, pluginName, title, titles: [pluginName, title], icon}],
    // by plugin name, then in manifest order.
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)
    // contributes.menus in command order: [{id, plugin, pluginName, title, icon, location, group, name}], where
    // location is grid.context, lineEditor.context or menuBar, and name the item's objectName.
    Q_PROPERTY(QVariantList menuItems READ menuItems NOTIFY commandsChanged)
    // The running plugins' shown status items, in command order:
    // [{plugin, pluginName, item, key: "<plugin>/<item>", title, text, icon, tooltip, tone, command}].
    Q_PROPERTY(QVariantList statusItems READ statusItems NOTIFY statusItemsChanged)
    // Every running plugin's status items by title, for the status bar menu: [{key, title, pluginName}].
    Q_PROPERTY(QVariantList statusItemTitles READ statusItemTitles NOTIFY statusItemsChanged)
    // Changes whenever a window's condition facts change.
    Q_PROPERTY(int conditionsRevision READ conditionsRevision NOTIFY conditionsChanged)
    // Commands running as jobs, newest first: {plugin, name, title, fraction (< 0: unknown), text, cancelling, stuck}.
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    // Plugins at work worth showing, by plugin name: {plugin, name, what, background, stillWorking}. stillWorking: a
    // load, command, event or timer callback past 10 seconds that isn't a job. A background plugin is listed while a
    // timer callback or a job runs too.
    Q_PROPERTY(QVariantList working READ working NOTIFY workingChanged)
    Q_PROPERTY(int marksRevision READ marksRevision NOTIFY marksChanged)
    Q_PROPERTY(hikari::app::PluginUi *ui READ ui CONSTANT)
    // Restricted plugins run in an operating-system sandbox here.
    Q_PROPERTY(bool sandboxed READ sandboxed CONSTANT)
    // Changes whenever a window's Project changes which plugins are on in it.
    Q_PROPERTY(int windowsRevision READ windowsRevision NOTIFY windowsChanged)

public:
    struct Wiring {
        QString helperPath;
        PluginManager::Folders folders;
        api::Api *api = nullptr;
        ui::LogController *log = nullptr;
        std::function<QString()> language; // "" for English
        backends::PluginProcess::Limits limits;
        std::size_t memoryLimit = std::size_t(2) << 30;
        // How long a plugin waits for another plugin's edit before it is told HikariSub is busy.
        int transactionWaitMs = 5000;
        // How long a cancelled job may run on before Stop is offered.
        int jobGraceMs = 5000;
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
        // Each shell window, with its trusted Project's [plugins]; nullopt: no Project, or an untrusted one.
        std::function<std::map<int, std::optional<plugins::ProjectPlugins>>()> windowPlugins;
        // The window the user works in.
        std::function<int()> activeWindow;
        // Analytics: a plugin used one of the features plugin_feature_used counts.
        std::function<void(const QString &feature)> featureUsed;
        // Grid row marks; a plugin's go when it stops.
        application::RowMarks *marks = nullptr;
        // A Document's tab title, for the plugin manager's Marks row.
        std::function<QString(application::DocumentId)> documentTitle;
        PluginPrograms *programs = nullptr;
        QString lowRightsHelper;
    };

    explicit PluginRuntime(Wiring wiring, QObject *parent = nullptr);
    ~PluginRuntime() override;

    // Starts a helper for each plugin on in some window that has none or changed version, and stops the rest.
    Q_INVOKABLE void sync();
    // A window's Project changed or became trusted.
    void projectsChanged();
    Q_INVOKABLE bool onIn(const QString &pluginId, int shellWindow) const;
    // The ids the window's trusted Project enables that aren't installed.
    Q_INVOKABLE QStringList missingIn(int shellWindow) const;
    int windowsRevision() const { return m_windowsRevision; }
    QVariantList commands() const { return m_commands; }
    QVariantList menuItems() const { return m_menuItems; }
    // contributes.keybindings of the running plugins, earliest installed first: [{id, keys, pluginName}].
    QVariantList keybindings() const;
    QVariantList statusItems() const;
    QVariantList statusItemTitles() const;
    int conditionsRevision() const { return m_conditionsRevision; }
    // What a window's `when` conditions are checked against: {document, selectedLines, video, audio,
    // karaoke, translation}.
    Q_INVOKABLE void setConditions(int shellWindow, const QVariantMap &facts);
    Q_INVOKABLE void forgetWindow(int shellWindow);
    // Why the command is disabled in the window (the window the user works in when < 0), or "" when it isn't.
    Q_INVOKABLE QString disabledReason(const QString &commandId, int shellWindow = -1) const;
    // False when no running plugin has the command, or its conditions fail in the window the user works in.
    // `via` is the handler's context.via: menu, key, status, api or ai.
    Q_INVOKABLE bool run(const QString &commandId, const QString &via = QStringLiteral("api"));
    // status_items.set for the plugin.
    api::StatusItemSet setStatusItem(const std::string &pluginId, const api::StatusItemChange &change);
    Q_INVOKABLE void forceStop(const QString &pluginId);
    // As forceStop, but a background plugin then stays stopped until restart or the next HikariSub start.
    Q_INVOKABLE void stopWorking(const QString &pluginId);
    QVariantList jobs() const;
    QVariantList working() const { return m_working; }
    // Asks the plugin's job to stop; when it hasn't 5 seconds later, the job is stuck and Stop is offered.
    Q_INVOKABLE void cancelJob(const QString &pluginId);
    // Per Document the plugin marks Lines in: {document, file, count}.
    Q_INVOKABLE QVariantList marks(const QString &pluginId) const;
    Q_INVOKABLE void clearMarks(const QString &pluginId, double document);
    int marksRevision() const { return m_marksRevision; }
    void rowMarksChanged()
    {
        ++m_marksRevision;
        emit marksChanged();
    }
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
    QSet<QString> runningIds() const;
    // {id, name} of each running plugin whose manifest asks for presence.
    QVariantList presencePlugins() const;

    struct Format {
        std::string id; // plugin:<plugin id>/<format id>
        std::string plugin;
        std::string pluginName;
        plugins::FormatContribution format;
    };
    // The formats of the plugins that run, in the order the plugins were installed.
    std::vector<Format> formats() const;
    std::optional<Format> format(const std::string &id) const;
    // False when the format's plugin doesn't run or can't take more work; else `done` is called once.
    bool exportDocument(const Format &format, const api::Json &snapshot, const api::Json &options,
                        backends::PluginProcess::FormatDone done);
    bool importFile(const Format &format, std::string bytes, const std::string &fileName,
                    backends::PluginProcess::FormatDone done);

signals:
    void commandsChanged();
    void statusItemsChanged();
    void conditionsChanged();
    void windowsChanged();
    void jobsChanged();
    void workingChanged();
    void marksChanged();
    // The plugin finished a command or an event, or stopped.
    void idle(const QString &pluginId);

private:
    struct Running;
    std::unique_ptr<Running> launch(const std::string &id, plugins::Manifest manifest);
    void serve(Running &plugin, const backends::PluginProcess::ServiceRequest &request,
               backends::PluginProcess::ServiceReply reply);
    void unsubscribe(Running &plugin);
    void relist();
    std::vector<const Running *> byName() const;
    void lock(Running &plugin, backends::PluginProcess::ServiceReply reply);
    void unlock(Running &plugin);
    void releaseLock(Running &plugin);
    void endAccess(Running &plugin);
    void wireQmlPanels();
    void wirePrograms();
    std::optional<std::string> setSetting(const std::string &plugin, const std::string &setting, const api::Json &value);
    const plugins::ProjectPlugins *windowProject(int shellWindow) const;
    api::Json storedSettings(const std::string &plugin) const;

    struct LockWaiter {
        Running *plugin = nullptr;
        backends::PluginProcess::ServiceReply reply;
        std::unique_ptr<QTimer> timeout;
    };

    Wiring m_wiring;
    std::optional<plugins::Installer> m_installer;
    std::map<std::string, std::unique_ptr<Running>> m_plugins;
    std::vector<std::string> m_installOrder;
    Running *m_lockHolder = nullptr;
    std::deque<LockWaiter> m_lockWaiters;
    QVariantList m_commands;
    QVariantList m_menuItems;
    std::map<int, plugins::ConditionFacts> m_conditions;
    int m_conditionsRevision = 0;
    const plugins::Command *command(const QString &commandId, Running **plugin = nullptr) const;
    std::map<std::string, std::int64_t> m_installOrder;
    PluginUi *m_ui = nullptr;
    std::unique_ptr<QNetworkAccessManager> m_ownNetwork;
    PluginNetwork *m_network = nullptr;
    std::unique_ptr<PluginFiles> m_files;
    std::map<int, std::optional<plugins::ProjectPlugins>> m_windows;
    std::set<std::string> m_userOn;
    std::map<int, std::set<std::string>> m_onIn; // by window, as of the last sync
    struct Job {
        std::string plugin;
        QString name, title, text;
        double fraction = -1;
        bool cancelling = false, stuck = false;
        std::unique_ptr<QTimer> grace;
    };
    std::vector<Job> m_jobs;
    int m_marksRevision = 0;
    void endJob(const std::string &plugin);
    void rework();
    QVariantList m_working;
    int m_windowsRevision = 0;
};

}
