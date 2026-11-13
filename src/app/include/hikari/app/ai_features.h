#pragma once

#include "hikari/application/ai_providers.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_agent_cli.h"
#include "hikari/backends/ai_sidecar_updater.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::api {
class Api;
}
namespace hikari::backends::ai {
class AgentBroker;
class McpClients;
class AiRequests;
class AiSignIn;
class AiSidecarHost;
class AntigravitySignIn;
class TokenFileLease;
}
namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

/// Whether a machine's policy turns automatic AI updates off for every user: HIKARI_AI_AUTO_UPDATE=0, or the
/// `ai_auto_update: false` member of `policyFile`, or on Windows HKLM\SOFTWARE\Policies\HikariSub\AiAutoUpdate = 0.
bool aiAutoUpdateOffByPolicy(const QString &environment, const QString &policyFile);

/// Every AI object the app builds, behind "Turn off all AI features". With the switch on, none exists: no key
/// store, config store or sidecar host, so nothing reads the keychain, downloads, starts or listens.
class AiFeatures final : public QObject {
    Q_OBJECT
public:
    static constexpr const char *kOffSetting = "ai.off";
    static constexpr const char *kCachingSetting = "ai.promptCaching";
    static constexpr const char *kWebSearchSetting = "ai.webSearch";
    static constexpr const char *kMcpServersSetting = "ai.mcp.servers";
    static constexpr const char *kSearchServiceSetting = "ai.searchService";
    static constexpr const char *kAutoUpdateSetting = "ai.autoUpdate";
    static constexpr const char *kCustomAgentsSetting = "ai.agents.custom";
    static constexpr const char *kOpenCode = "opencode";
    static constexpr const char *kAntigravity = "antigravity";

    /// An agent Qt talks to over ACP: OpenCode, Antigravity, or one of the user's own.
    struct AcpAgent {
        QString id;
        QString name;
        /// What starts it: a path, or a name looked up on PATH.
        QString command;
        QString arguments;
        /// The CLI Qt starts; empty when it isn't found.
        QString program;
        bool operator==(const AcpAgent &) const = default;
    };

    struct Timing {
        int checkMs = 60 * 60 * 1000;
        int idleMs = 2 * 60 * 1000;
        int pollMs = 5 * 1000;
        int crashWindowMs = 10 * 60 * 1000;
    };

    struct Ports {
        std::function<std::unique_ptr<application::ai::KeyStore>()> keyStore;
        std::function<std::unique_ptr<application::ai::ConfigStore>()> configStore;
        std::function<std::unique_ptr<backends::ai::AiSidecarHost>(const backends::ai::SidecarInstall &)> sidecarHost;
        /// The sidecar's store and its updates; null in builds that run their own sidecar.
        std::function<std::unique_ptr<backends::ai::AiSidecarUpdater>()> sidecarUpdater;
        /// The sidecar a developer or test build runs instead.
        std::optional<backends::ai::SidecarInstall> localSidecar;
        std::function<bool()> autoUpdateOffByPolicy;
        /// Adds the agent CLIs the sidecar may ask Qt to start.
        std::function<void(backends::ai::AgentBroker &, application::ai::KeyStore &)> agents;
        /// Where an agent's CLI is, found as findAgentCli finds it; empty when it isn't there.
        std::function<QString(const QString &command)> acpCli;
        /// How Qt starts an ACP agent; Antigravity borrows its sign-in from `antigravity`.
        std::function<std::optional<backends::ai::AgentSpec>(const AcpAgent &,
                                                             std::shared_ptr<backends::ai::TokenFileLease> antigravity)>
            acpSpec;
        /// Where agy reads its sign-in in Hikari's Antigravity profile.
        QString antigravityTokenFile;
        std::function<std::unique_ptr<backends::ai::AiSignIn>(application::ai::KeyStore &)> signIn;
        /// Whether Qt can start `agent` ("claude" or "codex"), found as Settings › AI › External agents finds it; the
        /// panel offers only these.
        std::function<bool(const QString &agent)> agentOnPath;
        /// Whether this system's sandbox keeps an agent to its own files; Codex and ACP agents need it (Q413). Unset
        /// counts as kept.
        std::function<bool()> filesConfined;
        /// Why agents' private tools channel can't open here, as on a Windows that can't make a pipe for their
        /// AppContainer; empty when it can. Codex and ACP agents need it. Unset counts as open.
        std::function<QString()> channelProblem;
        /// Finds an agent CLI ("claude" or "codex") and asks it for its version.
        std::function<void(const QString &agent, std::function<void(std::optional<backends::ai::AgentCli>, QString unresolvedShim)>)> findAgent;
        std::function<backends::ai::AgentInstall(const backends::ai::AgentCli &)> agentInstall;
        /// The latest version an install's own source offers; empty when it couldn't be read.
        std::function<void(const backends::ai::AgentInstall &, std::function<void(QString)>)> agentLatest;
        /// The Codex CLI's sign-in, which Import reads and never writes.
        QString codexAuthPath;
        /// Sends a crash report for a rolled-back sidecar; true when one went.
        std::function<bool(const QString &version, const QString &reason)> reportRollback;
        Timing timing;
    };
    /// The keychain, providers.json and the sidecar under <userFolder>/ai.
    static Ports defaultPorts(const QString &userFolder, api::Api *api, const QString &appVersion);

    AiFeatures(ui::SettingsStore &settings, Ports ports, QObject *parent = nullptr);
    ~AiFeatures() override;

    bool on() const { return m_live != nullptr; }
    /// Null with AI off.
    application::ai::KeyStore *keys() const;
    application::ai::ConfigStore *configs() const;
    application::ai::Providers *providers() const;
    application::ai::SearchKeys *searchKeys() const;
    backends::ai::AiRequests *requests() const;
    /// Kept across AI being switched off and on.
    void setProjectKeys(std::function<std::optional<std::string>(const std::string &project, const std::string &providerId)> keys);
    /// Every open Project's folder; kept across AI being switched off and on.
    void setProjectRoots(std::function<QStringList()> roots);
    /// A Project opened or closed: custom ACP agents inside one stop being offered, and `agentsChanged` says when that
    /// changed which agents are offered.
    void projectsChanged();
    bool confinesFiles() const;
    /// Whether Codex and ACP agents can have a private tools channel; asked once.
    bool channelOpens() const;
    /// Why `agent` ("codex", "opencode", "agy") isn't offered: "sandbox", "channel", or empty when it is. Claude never
    /// is refused.
    QString refusal(const QString &agent) const;
    /// Why the ACP agent at `program` isn't offered: "sandbox", "channel", "project" when it lies inside an open
    /// Project, or empty.
    QString acpRefusal(const QString &program) const;
    /// Why a thread's external agent (`claude`, `codex` or `acp:<id>`, as in its `agent:` provider) can't start here.
    QString agentRefusal(const QString &agent) const;
    /// Found and not refused.
    bool offersAgent(const QString &agent) const { return agentFound(agent) && refusal(agent).isEmpty(); }
    /// Null with AI off, or in builds that run their own sidecar.
    backends::ai::AiSidecarUpdater *updater() const;
    backends::ai::AiSignIn *signIn() const;
    const Ports &ports() const { return m_ports; }
    /// The sidecar for an AI action: built on the first one once a provider exists. Null with AI off, without a
    /// provider, or without an installed sidecar.
    backends::ai::AiSidecarHost *sidecarForAction();
    /// The sidecar for the MCP server, which needs no provider. Null with AI off or without an installed sidecar.
    backends::ai::AiSidecarHost *sidecarForMcp();
    /// Applies the web search settings. The sidecar gets the search service only once its key is in the keychain, so
    /// call this after a search key is stored or removed.
    void applyWebSearch();
    /// The MCP servers the user added; null with AI off.
    backends::ai::McpClients *mcpClients();
    /// While the user adds their first provider, its Test connection may start the sidecar too.
    void setDrafting(bool drafting);
    /// The host if one was built; never builds it.
    backends::ai::AiSidecarHost *sidecar() const;
    /// The version the running sidecar reported, or the one that would run.
    QString sidecarVersion() const;
    /// Whether the sidecar runs fully sandboxed: the host's own coverage once it is built, else what this system can
    /// enforce.
    bool sandboxed() const;
    /// The running sidecar's version and its runtime's and provider libraries', as ai_* event properties.
    QJsonObject versionProperties() const;
    bool agentFound(const QString &agent) const { return m_ports.agentOnPath && m_ports.agentOnPath(agent); }
    /// How many sessions of `agent` ("claude" or "codex") the sidecar is running.
    std::size_t agentSessions(const std::string &agent) const;
    /// The user's own ACP agents, found or not.
    std::vector<AcpAgent> customAgents() const;
    void setCustomAgents(const std::vector<AcpAgent> &agents);
    /// OpenCode and Antigravity when found, then the user's own agents that are.
    std::vector<AcpAgent> acpAgents() const;
    /// `agy acp --login` in Hikari's Antigravity profile. Null with AI off or without agy.
    std::unique_ptr<backends::ai::AntigravitySignIn> antigravitySignIn();

    bool autoUpdate() const;
    bool autoUpdateOffByPolicy() const;
    /// Reads the channel now; with automatic updates on, a newer sidecar downloads too.
    void checkNow();
    /// Downloads the newest sidecar and switches to it at the next idle moment.
    void updateNow();

    /// Something that holds a sidecar switch off while it returns true: a turn, a sub-agent, an MCP client.
    int addBusySource(std::function<bool()> busy);
    void removeBusySource(int id);
    /// The panel was used; a switch waits until it has been idle for Timing::idleMs.
    void noteActivity();

signals:
    void switched(bool on);
    /// The sidecar's version, the check or the staged update changed.
    void componentsChanged();
    /// `version` failed `initialize` or crashed twice (`reason` "initialize" or "crashed"); the previous sidecar runs
    /// again and `version` is skipped.
    void rolledBack(QString version, QString reason);
    void agentsChanged();

private:
    std::function<std::optional<std::string>(const std::string &, const std::string &)> m_projectKeys;
    std::function<QStringList()> m_projectRoots;
    QStringList m_refusedInProjects;
    mutable std::optional<bool> m_channelOpens;
    QStringList refusedInProjects() const;
    void apply();
    void startUpdates();
    void stage();
    void poll();
    bool busy() const;
    void replaceHost();
    void addAcpAgents();
    std::optional<backends::ai::SidecarInstall> runnable() const;
    backends::ai::AiSidecarHost *buildHost();
    void watchHost(backends::ai::AiSidecarHost *host, const QString &version);
    void failed(const QString &version, const QString &reason);

    struct Live;
    ui::SettingsStore &m_settings;
    Ports m_ports;
    std::unique_ptr<Live> m_live;
    std::map<int, std::function<bool()>> m_busy;
    int m_nextBusy = 1;
};

}
