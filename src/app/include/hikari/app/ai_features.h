#pragma once

#include "hikari/application/ai_providers.h"
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

namespace hikari::api {
class Api;
}
namespace hikari::backends::ai {
class AgentBroker;
class AiRequests;
class AiSignIn;
class AiSidecarHost;
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
    static constexpr const char *kAutoUpdateSetting = "ai.autoUpdate";

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
        std::function<void(backends::ai::AgentBroker &)> agents;
        std::function<std::unique_ptr<backends::ai::AiSignIn>(application::ai::KeyStore &)> signIn;
        /// Finds an agent CLI ("claude" or "codex") and asks it for its version.
        std::function<void(const QString &agent, std::function<void(std::optional<backends::ai::AgentCli>)>)> findAgent;
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
    backends::ai::AiRequests *requests() const;
    /// Null with AI off, or in builds that run their own sidecar.
    backends::ai::AiSidecarUpdater *updater() const;
    backends::ai::AiSignIn *signIn() const;
    const Ports &ports() const { return m_ports; }
    /// The sidecar for an AI action: built on the first one once a provider exists. Null with AI off, without a
    /// provider, or without an installed sidecar.
    backends::ai::AiSidecarHost *sidecarForAction();
    /// The sidecar for the MCP server, which needs no provider. Null with AI off or without an installed sidecar.
    backends::ai::AiSidecarHost *sidecarForMcp();
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
    /// How many sessions of `agent` ("claude" or "codex") the sidecar is running.
    std::size_t agentSessions(const std::string &agent) const;

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

private:
    void apply();
    void startUpdates();
    void stage();
    void poll();
    bool busy() const;
    void replaceHost();
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
