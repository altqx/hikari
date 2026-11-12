#include "hikari/app/ai_features.h"

#include "hikari/api/api.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_antigravity.h"
#include "hikari/backends/ai_config_store.h"
#include "hikari/backends/ai_http_relay.h"
#include "hikari/backends/ai_keychain.h"
#include "hikari/backends/ai_mcp_clients.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_signin.h"
#include "hikari/backends/sandbox.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <tuple>

namespace hikari::app {

namespace {

class NoConfigStore final : public application::ai::ConfigStore {
public:
    application::ai::Config load() override { return {}; }
    bool save(const application::ai::Config &) override { return false; }
};

}

bool aiAutoUpdateOffByPolicy(const QString &environment, const QString &policyFile)
{
    if (environment == QLatin1String("0"))
        return true;
#ifdef Q_OS_WIN
    const QSettings policy(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\HikariSub"), QSettings::NativeFormat);
    const QVariant value = policy.value(QStringLiteral("AiAutoUpdate"));
    if (value.isValid() && value.toInt() == 0)
        return true;
#endif
    QFile file(policyFile);
    if (policyFile.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return false;
    const QJsonValue member = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("ai_auto_update"));
    return member.isBool() && !member.toBool();
}

struct AiFeatures::Live {
    std::unique_ptr<application::ai::KeyStore> keys;
    std::unique_ptr<application::ai::ConfigStore> configs;
    std::unique_ptr<backends::ai::AiSidecarUpdater> updater;
    std::unique_ptr<application::ai::Providers> providers;
    std::unique_ptr<application::ai::SearchKeys> searchKeys;
    std::unique_ptr<backends::ai::AiRequests> requests;
    std::unique_ptr<backends::ai::HttpRelay> relay;
    std::unique_ptr<backends::ai::AiSidecarHost> host;
    std::unique_ptr<backends::ai::AgentBroker> agents;
    std::vector<std::string> acpAdded;
    std::shared_ptr<backends::ai::TokenFileLease> antigravity;
    std::unique_ptr<backends::ai::McpClients> mcp;
    std::unique_ptr<backends::ai::AiSignIn> signIn;
    bool drafting = false;
    std::uint64_t webSearchApplied = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    QString hostVersion;
    bool hostReady = false;
    std::deque<qint64> crashes;
    QElapsedTimer clock;
    qint64 lastActive = 0;
    QTimer checkTimer;
    QTimer pollTimer;
};

namespace {

std::vector<backends::ai::McpServerEntry> mcpServersOf(const ui::SettingsStore &settings)
{
    api::Json rows = api::Json::array();
    for (const QString &row : settings.list(AiFeatures::kMcpServersSetting))
        if (auto parsed = api::Json::parse(row.toStdString()))
            rows.push(std::move(*parsed));
    return backends::ai::mcpServersFromJson(rows);
}

/// The agents Qt can start here. Codex needs a tools channel its AppContainer can reach on Windows (#931).
QString agentCli(const QString &agent)
{
#ifdef Q_OS_WIN
    if (agent == QLatin1String("codex"))
        return {};
#endif
    return backends::ai::findAgentCli(agent);
}

QString acpBroker(const QString &id)
{
    return QStringLiteral("acp:") + id;
}

}

AiFeatures::Ports AiFeatures::defaultPorts(const QString &userFolder, api::Api *api, const QString &)
{
    const QString ai = userFolder + QStringLiteral("/ai");
    Ports ports;
    ports.keyStore = [] { return std::make_unique<backends::ai::KeychainKeyStore>(QStringLiteral("HikariSub")); };
    ports.configStore = [ai, kept = !userFolder.isEmpty()]() -> std::unique_ptr<application::ai::ConfigStore> {
        if (!kept)
            return std::make_unique<NoConfigStore>();
        return std::make_unique<backends::ai::JsonConfigStore>(ai + QStringLiteral("/providers.json"));
    };
    ports.sidecarHost = [ai, api](const backends::ai::SidecarInstall &install)
        -> std::unique_ptr<backends::ai::AiSidecarHost> {
        if (!QFileInfo(install.runtime).isExecutable() || !QFileInfo(install.bundle).isFile())
            return nullptr;
        backends::ai::AiSidecarHost::Options options;
        options.program = install.runtime;
        const QString vendor = ai + QStringLiteral("/vendor");
        QDir().mkpath(vendor);
        options.arguments = {install.bundle, QStringLiteral("--vendor"), QDir::toNativeSeparators(vendor)};
        options.workingDirectory = ai + QStringLiteral("/work");
        options.sandbox = backends::sandbox::Profile{
            .name = QStringLiteral("HikariSub.AI.Sidecar"),
            .readExecute = {QFileInfo(install.runtime).absolutePath(), QFileInfo(install.bundle).absolutePath()},
            .readWrite = {options.workingDirectory, vendor},
        };
        options.buildId = HIKARI_AI_BUILD_ID;
        return std::make_unique<backends::ai::AiSidecarHost>(std::move(options), api,
                                                             api::Caller{.id = "ai", .label = "AI", .reviewed = true, .skills = true});
    };
#ifdef HIKARI_AI_LOCAL_RUNTIME
    ports.localSidecar = backends::ai::SidecarInstall{QStringLiteral("0.0.0-dev"), QStringLiteral(HIKARI_AI_LOCAL_RUNTIME),
                                                      QStringLiteral(HIKARI_AI_LOCAL_BUNDLE), {}};
#else
    ports.sidecarUpdater = [ai] {
        return std::make_unique<backends::ai::AiSidecarUpdater>(
            ai + QStringLiteral("/sidecar"),
            backends::ai::SidecarCompat{backends::ai::kSidecarProtocolVersion, api::kApiVersion});
    };
#endif
    ports.autoUpdateOffByPolicy = [] {
        return aiAutoUpdateOffByPolicy(qEnvironmentVariable("HIKARI_AI_AUTO_UPDATE"),
                                       QStringLiteral("/etc/hikarisub/policy.json"));
    };
    ports.agents = [ai](backends::ai::AgentBroker &broker, application::ai::KeyStore &keys) {
        const QString agents = ai + QStringLiteral("/agents");
        if (const QString claude = agentCli(QStringLiteral("claude")); !claude.isEmpty())
            broker.add("claude", backends::ai::claudeAgent(claude, QDir::homePath(), agents + QStringLiteral("/claude")));
        if (const QString codex = agentCli(QStringLiteral("codex")); !codex.isEmpty())
            if (auto spec = backends::ai::codexAgent(
                    codex, agents + QStringLiteral("/codex/home"), agents + QStringLiteral("/codex/work"),
                    std::make_shared<backends::ai::CodexLease>(keys, agents + QStringLiteral("/codex/home"))))
                broker.add("codex", std::move(*spec));
    };
    ports.agentOnPath = [](const QString &agent) { return !agentCli(agent).isEmpty(); };
    ports.acpCli = [](const QString &command) {
        if (QFileInfo(command).isAbsolute())
            return QFileInfo(command).isExecutable() ? command : QString();
        return backends::ai::findAgentCli(command);
    };
    ports.antigravityTokenFile = ai + QStringLiteral("/agents/antigravity/") + QLatin1String(backends::ai::kAntigravityTokenFile);
    ports.acpSpec = [ai](const AcpAgent &agent, std::shared_ptr<backends::ai::TokenFileLease> antigravity)
        -> std::optional<backends::ai::AgentSpec> {
        const QString agents = ai + QStringLiteral("/agents");
        const QString bridge = QCoreApplication::applicationFilePath();
        if (agent.id == QLatin1String(kOpenCode))
            return backends::ai::openCodeAgent(agent.program, QDir::homePath(), agents + QStringLiteral("/opencode"),
                                               bridge);
        if (agent.id == QLatin1String(kAntigravity))
            return backends::ai::antigravityAgent(agent.program, agents + QStringLiteral("/antigravity"), bridge,
                                                  std::move(antigravity));
        return backends::ai::customAcpAgent(agent.program, QProcess::splitCommand(agent.arguments),
                                            agents + QStringLiteral("/custom/") + agent.id, bridge);
    };
    ports.signIn = [](application::ai::KeyStore &keys) { return std::make_unique<backends::ai::AiSignIn>(keys); };
    ports.findAgent = [](const QString &agent, std::function<void(std::optional<backends::ai::AgentCli>, QString)> done) {
        backends::ai::probeAgentCli(agent, std::move(done));
    };
    ports.agentInstall = [](const backends::ai::AgentCli &cli) {
        return backends::ai::detectAgentInstall(cli.agent, cli.path, cli.realPath, backends::ai::currentAgentHost());
    };
    ports.agentLatest = [ai, latest = std::shared_ptr<backends::ai::AgentLatestVersions>()](
                            const backends::ai::AgentInstall &install, std::function<void(QString)> done) mutable {
        if (!latest)
            latest = std::make_shared<backends::ai::AgentLatestVersions>(ai + QStringLiteral("/agent-versions.json"));
        latest->latest(install, backends::ai::claudeUpdateChannel(QDir::homePath()), std::move(done));
    };
    ports.codexAuthPath = QDir::homePath() + QStringLiteral("/.codex/auth.json");
    return ports;
}

AiFeatures::AiFeatures(ui::SettingsStore &settings, Ports ports, QObject *parent)
    : QObject(parent), m_settings(settings), m_ports(std::move(ports))
{
    connect(&m_settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String(kOffSetting))
            apply();
        else if (id == QLatin1String(kCachingSetting) && m_live)
            m_live->requests->setCaching(m_settings.boolean(kCachingSetting));
        else if (id == QLatin1String(kWebSearchSetting) || id == QLatin1String(kSearchServiceSetting))
            applyWebSearch();
        else if (id == QLatin1String(kCustomAgentsSetting) && m_live)
            addAcpAgents();
        else if (id == QLatin1String(kMcpServersSetting) && m_live)
            m_live->mcp->setServers(mcpServersOf(m_settings));
        else if (id == QLatin1String(kAutoUpdateSetting) && m_live) {
            emit componentsChanged();
            if (autoUpdate())
                stage();
        }
    });
    apply();
}

AiFeatures::~AiFeatures() = default;

void AiFeatures::apply()
{
    const bool wanted = !m_settings.boolean(kOffSetting);
    if (wanted == on())
        return;
    if (wanted) {
        m_live = std::make_unique<Live>();
        m_live->clock.start();
        m_live->keys = m_ports.keyStore();
        if (!m_ports.antigravityTokenFile.isEmpty())
            m_live->antigravity = std::make_shared<backends::ai::TokenFileLease>(
                *m_live->keys, backends::ai::kAntigravitySecret, m_ports.antigravityTokenFile,
                QStringLiteral("Sign in with Google for Antigravity in Settings first"));
        m_live->configs = m_ports.configStore();
        m_live->providers = std::make_unique<application::ai::Providers>(*m_live->configs, *m_live->keys);
        m_live->requests =
            std::make_unique<backends::ai::AiRequests>(*m_live->providers, [this] { return sidecarForAction(); });
        m_live->requests->setCaching(m_settings.boolean(kCachingSetting));
        m_live->requests->setProjectKeys(m_projectKeys);
        m_live->searchKeys = std::make_unique<application::ai::SearchKeys>(*m_live->keys);
        applyWebSearch();
        m_live->mcp = std::make_unique<backends::ai::McpClients>(*m_live->keys, QDir::homePath());
        m_live->mcp->setServers(mcpServersOf(m_settings));
        m_live->relay = std::make_unique<backends::ai::HttpRelay>(backends::ai::withSearchTargets(
            m_live->mcp->relayTargets(backends::ai::withRegistryTargets(m_live->requests->relayTargets())),
            *m_live->searchKeys,
            [requests = m_live->requests.get()](application::ai::SearchService service) {
                return requests->searchAccess(service);
            }));
        if (m_ports.signIn)
            m_live->signIn = m_ports.signIn(*m_live->keys);
        if (m_ports.sidecarUpdater)
            m_live->updater = m_ports.sidecarUpdater();
        startUpdates();
    } else {
        m_live.reset();
    }
    emit switched(wanted);
}

backends::ai::McpClients *AiFeatures::mcpClients()
{
    return m_live ? m_live->mcp.get() : nullptr;
}

void AiFeatures::applyWebSearch()
{
    if (!m_live)
        return;
    const bool on = m_settings.boolean(kWebSearchSetting);
    const auto service = application::ai::searchServiceFromId(m_settings.text(kSearchServiceSetting).toStdString());
    const std::uint64_t applied = ++m_live->webSearchApplied;
    m_live->requests->setWebSearch(on, std::nullopt);
    if (!on || !service)
        return;
    m_live->searchKeys->read(*service, [this, alive = std::weak_ptr(m_live->alive), applied,
                                        service](std::optional<std::string> key) {
        if (alive.lock() && key && m_live->webSearchApplied == applied)
            m_live->requests->setWebSearch(true, service);
    });
}

void AiFeatures::startUpdates()
{
    backends::ai::AiSidecarUpdater *updater = m_live->updater.get();
    if (!updater)
        return;
    using backends::ai::AiSidecarUpdater;
    connect(updater, &AiSidecarUpdater::checked, this, [this] {
        const auto next = runnable();
        if (m_live->host && (!next || next->version != m_live->hostVersion))
            m_live->pollTimer.start();
        emit componentsChanged();
    });
    connect(updater, &AiSidecarUpdater::downloaded, this, [this] {
        if (!m_live->host && !runnable())
            m_live->updater->switchToStaged();
        else
            m_live->pollTimer.start();
        emit componentsChanged();
    });
    connect(updater, &AiSidecarUpdater::installed, this, [this] {
        if (!m_live->checkTimer.isActive())
            m_live->checkTimer.start();
        emit componentsChanged();
    });
    m_live->checkTimer.setInterval(m_ports.timing.checkMs);
    connect(&m_live->checkTimer, &QTimer::timeout, this, &AiFeatures::stage);
    m_live->pollTimer.setInterval(m_ports.timing.pollMs);
    connect(&m_live->pollTimer, &QTimer::timeout, this, &AiFeatures::poll);
    if (!updater->current())
        return;
    if (autoUpdate())
        updater->switchToStaged();
    m_live->checkTimer.start();
    stage();
}

void AiFeatures::stage()
{
    if (m_live && m_live->updater && !m_live->updater->busy())
        m_live->updater->check(autoUpdate());
}

void AiFeatures::checkNow()
{
    stage();
}

void AiFeatures::updateNow()
{
    if (m_live && m_live->updater && !m_live->updater->busy())
        m_live->updater->check(true);
}

bool AiFeatures::autoUpdateOffByPolicy() const
{
    return m_ports.autoUpdateOffByPolicy && m_ports.autoUpdateOffByPolicy();
}

bool AiFeatures::autoUpdate() const
{
    return m_settings.boolean(kAutoUpdateSetting) && !autoUpdateOffByPolicy();
}

bool AiFeatures::busy() const
{
    if (m_live->host && m_live->host->outstanding() > 0)
        return true;
    for (const auto &[id, source] : m_busy)
        if (source())
            return true;
    return false;
}

void AiFeatures::poll()
{
    if (!m_live)
        return;
    if (busy()) {
        m_live->lastActive = m_live->clock.elapsed();
        return;
    }
    if (m_live->clock.elapsed() - m_live->lastActive < m_ports.timing.idleMs)
        return;
    m_live->pollTimer.stop();
    const bool switched = m_live->updater && m_live->updater->switchToStaged();
    const auto next = runnable();
    if (m_live->host && (switched || !next || next->version != m_live->hostVersion))
        replaceHost();
    emit componentsChanged();
}

void AiFeatures::replaceHost()
{
    backends::ai::AiSidecarHost *old = m_live->host.release();
    old->disconnect(this);
    old->setParent(this);
    QTimer::singleShot(0, old, [old] {
        if (old->processId() != 0)
            connect(old, &backends::ai::AiSidecarHost::finished, old, &QObject::deleteLater);
        else
            old->deleteLater();
        old->shutdown();
    });
    m_live->agents.reset();
    m_live->hostVersion.clear();
    m_live->hostReady = false;
    m_live->crashes.clear();
}

void AiFeatures::noteActivity()
{
    if (m_live)
        m_live->lastActive = m_live->clock.elapsed();
}

int AiFeatures::addBusySource(std::function<bool()> busy)
{
    const int id = m_nextBusy++;
    m_busy.emplace(id, std::move(busy));
    return id;
}

void AiFeatures::removeBusySource(int id)
{
    m_busy.erase(id);
}

std::optional<backends::ai::SidecarInstall> AiFeatures::runnable() const
{
    if (m_ports.localSidecar)
        return m_ports.localSidecar;
    return m_live && m_live->updater ? m_live->updater->current() : std::nullopt;
}

backends::ai::AiSidecarHost *AiFeatures::buildHost()
{
    if (!m_live->host && m_ports.sidecarHost) {
        if (const auto install = runnable()) {
            m_live->host = m_ports.sidecarHost(*install);
            if (m_live->host) {
                m_live->relay->attach(*m_live->host);
                m_live->agents = std::make_unique<backends::ai::AgentBroker>();
                if (m_ports.agents)
                    m_ports.agents(*m_live->agents, *m_live->keys);
                addAcpAgents();
                m_live->agents->attach(*m_live->host);
                m_live->mcp->attach(*m_live->host, *m_live->agents);
                watchHost(m_live->host.get(), install->version);
            }
        }
    }
    return m_live->host.get();
}

void AiFeatures::watchHost(backends::ai::AiSidecarHost *host, const QString &version)
{
    using backends::ai::AiSidecarHost;
    m_live->hostVersion = version;
    m_live->hostReady = false;
    m_live->crashes.clear();
    connect(host, &AiSidecarHost::requested, this, [this] { noteActivity(); });
    connect(host, &AiSidecarHost::ready, this, [this] {
        m_live->hostReady = true;
        emit componentsChanged();
    });
    if (m_ports.localSidecar)
        return;
    connect(host, &AiSidecarHost::refused, this, [this, version] { failed(version, QStringLiteral("initialize")); });
    connect(host, &AiSidecarHost::lost, this, [this, version] {
        if (!m_live->hostReady)
            return failed(version, QStringLiteral("initialize"));
        m_live->hostReady = false;
        const qint64 now = m_live->clock.elapsed();
        m_live->crashes.push_back(now);
        while (now - m_live->crashes.front() > m_ports.timing.crashWindowMs)
            m_live->crashes.pop_front();
        if (m_live->crashes.size() >= 2)
            failed(version, QStringLiteral("crashed"));
    });
}

void AiFeatures::failed(const QString &version, const QString &reason)
{
    backends::ai::AiSidecarUpdater *updater = m_live->updater.get();
    const auto current = updater ? updater->current() : std::nullopt;
    if (!current || current->version != version || updater->previous().isEmpty())
        return;
    updater->reject(version);
    const auto now = updater->current();
    const QString to = now ? now->version : QString();
    replaceHost();
    const bool reported = m_ports.reportRollback && m_ports.reportRollback(version, reason);
    updater->setRollback(backends::ai::SidecarRollback{.version = version, .to = to, .reason = reason, .reportSent = reported});
    emit rolledBack(version, reason);
    emit componentsChanged();
}

backends::ai::AiSidecarUpdater *AiFeatures::updater() const
{
    return m_live ? m_live->updater.get() : nullptr;
}

application::ai::KeyStore *AiFeatures::keys() const
{
    return m_live ? m_live->keys.get() : nullptr;
}

application::ai::ConfigStore *AiFeatures::configs() const
{
    return m_live ? m_live->configs.get() : nullptr;
}

application::ai::Providers *AiFeatures::providers() const
{
    return m_live ? m_live->providers.get() : nullptr;
}

application::ai::SearchKeys *AiFeatures::searchKeys() const
{
    return m_live ? m_live->searchKeys.get() : nullptr;
}

void AiFeatures::setProjectKeys(
    std::function<std::optional<std::string>(const std::string &project, const std::string &providerId)> keys)
{
    m_projectKeys = std::move(keys);
    if (m_live)
        m_live->requests->setProjectKeys(m_projectKeys);
}

backends::ai::AiRequests *AiFeatures::requests() const
{
    return m_live ? m_live->requests.get() : nullptr;
}

void AiFeatures::setDrafting(bool drafting)
{
    if (m_live)
        m_live->drafting = drafting;
}

backends::ai::AiSidecarHost *AiFeatures::sidecar() const
{
    return m_live ? m_live->host.get() : nullptr;
}

QString AiFeatures::sidecarVersion() const
{
    if (!m_live)
        return {};
    if (m_live->host && m_live->hostReady && !m_live->host->info().version.empty())
        return QString::fromStdString(m_live->host->info().version);
    const auto install = runnable();
    return install ? install->version : QString();
}

QJsonObject AiFeatures::versionProperties() const
{
    const auto install = runnable();
    if (!m_live || !install)
        return {};
    QJsonObject properties{{QStringLiteral("ai_component_version"), sidecarVersion()}};
    for (const auto &[package, property] : {std::pair{"bun", "ai_runtime_version"},
                                            std::pair{"@anthropic-ai/sdk", "ai_anthropic_version"},
                                            std::pair{"openai", "ai_openai_version"},
                                            std::pair{"@google/genai", "ai_google_version"}})
        if (const auto it = install->packages.find(QLatin1String(package)); it != install->packages.end())
            properties.insert(QLatin1String(property), it->second);
    return properties;
}

std::size_t AiFeatures::agentSessions(const std::string &agent) const
{
    return m_live && m_live->agents ? m_live->agents->running(agent) : 0;
}

std::vector<AiFeatures::AcpAgent> AiFeatures::customAgents() const
{
    std::vector<AcpAgent> out;
    for (const QString &entry : m_settings.list(kCustomAgentsSetting)) {
        const QJsonObject o = QJsonDocument::fromJson(entry.toUtf8()).object();
        AcpAgent agent{.id = o.value(QLatin1String("id")).toString(),
                       .name = o.value(QLatin1String("name")).toString(),
                       .command = o.value(QLatin1String("command")).toString(),
                       .arguments = o.value(QLatin1String("arguments")).toString()};
        static const QRegularExpression safeId(QStringLiteral("^custom-[0-9a-z]{1,32}$"));
        if (!safeId.match(agent.id).hasMatch() || agent.command.isEmpty())
            continue;
        if (m_ports.acpCli)
            agent.program = m_ports.acpCli(agent.command);
        out.push_back(std::move(agent));
    }
    return out;
}

void AiFeatures::setCustomAgents(const std::vector<AcpAgent> &agents)
{
    QStringList entries;
    for (const AcpAgent &agent : agents)
        entries << QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("id"), agent.id},
                                                               {QStringLiteral("name"), agent.name},
                                                               {QStringLiteral("command"), agent.command},
                                                               {QStringLiteral("arguments"), agent.arguments}})
                                         .toJson(QJsonDocument::Compact));
    m_settings.set(kCustomAgentsSetting, entries);
}

std::vector<AiFeatures::AcpAgent> AiFeatures::acpAgents() const
{
#ifdef Q_OS_WIN
    return {};
#else
    std::vector<AcpAgent> out;
    if (!m_ports.acpCli)
        return out;
    for (const auto &[id, name, cli] : {std::tuple{kOpenCode, "OpenCode", "opencode"}, std::tuple{kAntigravity, "Antigravity", "agy"}})
        if (const QString program = m_ports.acpCli(QLatin1String(cli)); !program.isEmpty())
            out.push_back({.id = QLatin1String(id), .name = QLatin1String(name), .command = QLatin1String(cli),
                           .program = program});
    for (AcpAgent &agent : customAgents())
        if (!agent.program.isEmpty())
            out.push_back(std::move(agent));
    return out;
#endif
}

void AiFeatures::addAcpAgents()
{
    if (!m_live->agents)
        return;
    for (const std::string &added : m_live->acpAdded)
        m_live->agents->remove(added);
    m_live->acpAdded.clear();
    if (!m_ports.acpSpec)
        return;
    for (const AcpAgent &agent : acpAgents())
        if (auto spec = m_ports.acpSpec(agent, m_live->antigravity)) {
            m_live->acpAdded.push_back(acpBroker(agent.id).toStdString());
            m_live->agents->add(m_live->acpAdded.back(), std::move(*spec));
        }
}

std::unique_ptr<backends::ai::AntigravitySignIn> AiFeatures::antigravitySignIn()
{
    if (!m_live || !m_live->antigravity || !m_ports.acpSpec)
        return nullptr;
    const std::vector<AcpAgent> agents = acpAgents();
    const auto agy = std::ranges::find(agents, QLatin1String(kAntigravity), &AcpAgent::id);
    if (agy == agents.end())
        return nullptr;
    std::optional<backends::ai::AgentSpec> spec = m_ports.acpSpec(*agy, nullptr);
    if (!spec)
        return nullptr;
    return std::make_unique<backends::ai::AntigravitySignIn>(std::move(*spec), m_live->antigravity);
}

backends::ai::AiSignIn *AiFeatures::signIn() const
{
    return m_live ? m_live->signIn.get() : nullptr;
}

bool AiFeatures::sandboxed() const
{
    if (const backends::ai::AiSidecarHost *host = sidecar())
        return host->sandboxCoverage().complete();
    return backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete();
}

backends::ai::AiSidecarHost *AiFeatures::sidecarForMcp()
{
    return m_live ? buildHost() : nullptr;
}

backends::ai::AiSidecarHost *AiFeatures::sidecarForAction()
{
    if (!m_live)
        return nullptr;
    if (!m_live->host && !(m_live->providers->enabled() || m_live->drafting))
        return nullptr;
    return buildHost();
}

}
