#include "hikari/app/ai_features.h"

#include "hikari/api/api.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_config_store.h"
#include "hikari/backends/ai_http_relay.h"
#include "hikari/backends/ai_keychain.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_signin.h"
#include "hikari/backends/sandbox.h"
#include "settings_store.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <cstdint>

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
                                                             api::Caller{.id = "ai", .label = "AI", .reviewed = true});
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
        if (const QString claude = backends::ai::findAgentCli(QStringLiteral("claude")); !claude.isEmpty())
            broker.add("claude", backends::ai::claudeAgent(claude, QDir::homePath(), agents + QStringLiteral("/claude")));
        if (const QString codex = backends::ai::findAgentCli(QStringLiteral("codex")); !codex.isEmpty())
            if (auto spec = backends::ai::codexAgent(
                    codex, agents + QStringLiteral("/codex/home"), agents + QStringLiteral("/codex/work"),
                    std::make_shared<backends::ai::CodexLease>(keys, agents + QStringLiteral("/codex/home"))))
                broker.add("codex", std::move(*spec));
    };
    ports.agentOnPath = [](const QString &agent) { return !backends::ai::findAgentCli(agent).isEmpty(); };
    ports.signIn = [](application::ai::KeyStore &keys) { return std::make_unique<backends::ai::AiSignIn>(keys); };
    ports.findAgent = [](const QString &agent, std::function<void(std::optional<backends::ai::AgentCli>)> done) {
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
        m_live->configs = m_ports.configStore();
        m_live->providers = std::make_unique<application::ai::Providers>(*m_live->configs, *m_live->keys);
        m_live->requests =
            std::make_unique<backends::ai::AiRequests>(*m_live->providers, [this] { return sidecarForAction(); });
        m_live->requests->setCaching(m_settings.boolean(kCachingSetting));
        m_live->searchKeys = std::make_unique<application::ai::SearchKeys>(*m_live->keys);
        applyWebSearch();
        m_live->relay = std::make_unique<backends::ai::HttpRelay>(backends::ai::withSearchTargets(
            backends::ai::withRegistryTargets(m_live->requests->relayTargets()), *m_live->searchKeys,
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
                m_live->agents->attach(*m_live->host);
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
