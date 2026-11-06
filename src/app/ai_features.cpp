#include "hikari/app/ai_features.h"

#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_keychain.h"
#include "hikari/backends/ai_config_store.h"
#include "hikari/backends/ai_http_relay.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_download.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_signin.h"
#include "hikari/backends/sandbox.h"
#include "settings_store.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdint>

namespace hikari::app {

namespace {

class NoConfigStore final : public application::ai::ConfigStore {
public:
    application::ai::Config load() override { return {}; }
    bool save(const application::ai::Config &) override { return false; }
};

}

struct AiFeatures::Live {
    std::unique_ptr<application::ai::KeyStore> keys;
    std::unique_ptr<application::ai::ConfigStore> configs;
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
};

AiFeatures::Ports AiFeatures::defaultPorts(const QString &userFolder, api::Api *api, const QString &appVersion)
{
    const QString ai = userFolder + QStringLiteral("/ai");
    Ports ports;
    ports.keyStore = [] { return std::make_unique<backends::ai::KeychainKeyStore>(QStringLiteral("HikariSub")); };
    ports.configStore = [ai, kept = !userFolder.isEmpty()]() -> std::unique_ptr<application::ai::ConfigStore> {
        if (!kept)
            return std::make_unique<NoConfigStore>();
        return std::make_unique<backends::ai::JsonConfigStore>(ai + QStringLiteral("/providers.json"));
    };
    ports.sidecarHost = [ai, api, appVersion]() -> std::unique_ptr<backends::ai::AiSidecarHost> {
#ifdef HIKARI_AI_LOCAL_SIDECAR
        const QString program = QStringLiteral(HIKARI_AI_LOCAL_SIDECAR);
#else
        const QString program = backends::ai::AiSidecarDownload(ai + QStringLiteral("/sidecar"), appVersion)
                                    .installedProgram()
                                    .value_or(QString());
#endif
        if (program.isEmpty() || !QFileInfo(program).isExecutable())
            return nullptr;
        backends::ai::AiSidecarHost::Options options;
        options.program = program;
        options.workingDirectory = ai + QStringLiteral("/work");
        options.sandbox = backends::sandbox::Profile{
            .name = QStringLiteral("HikariSub.AI.Sidecar"),
            .readExecute = {QFileInfo(program).absolutePath()},
            .readWrite = {options.workingDirectory},
        };
        options.buildId = HIKARI_AI_BUILD_ID;
        return std::make_unique<backends::ai::AiSidecarHost>(std::move(options), api,
                                                             api::Caller{"ai", "Hikari AI", false});
    };
    ports.sidecarDownload = [ai, appVersion]() -> std::unique_ptr<backends::ai::AiSidecarDownload> {
#ifdef HIKARI_AI_LOCAL_SIDECAR
        return nullptr;
#else
        auto download = std::make_unique<backends::ai::AiSidecarDownload>(ai + QStringLiteral("/sidecar"), appVersion);
        return download->installedProgram() ? nullptr : std::move(download);
#endif
    };
    ports.agents = [ai](backends::ai::AgentBroker &broker) {
        const QString agents = ai + QStringLiteral("/agents");
        if (const QString claude = QStandardPaths::findExecutable(QStringLiteral("claude")); !claude.isEmpty())
            broker.add("claude", backends::ai::claudeAgent(claude, QDir::homePath(), agents + QStringLiteral("/claude")));
        if (const QString codex = QStandardPaths::findExecutable(QStringLiteral("codex")); !codex.isEmpty())
            if (auto spec = backends::ai::codexAgent(codex, agents + QStringLiteral("/codex/home"),
                                                     agents + QStringLiteral("/codex/work")))
                broker.add("codex", std::move(*spec));
    };
    ports.signIn = [](application::ai::KeyStore &keys) { return std::make_unique<backends::ai::AiSignIn>(keys); };
    ports.findCodex = backends::ai::findCodexCli;
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
        m_live->keys = m_ports.keyStore();
        m_live->configs = m_ports.configStore();
        m_live->providers = std::make_unique<application::ai::Providers>(*m_live->configs, *m_live->keys);
        m_live->requests =
            std::make_unique<backends::ai::AiRequests>(*m_live->providers, [this] { return sidecarForAction(); });
        m_live->requests->setCaching(m_settings.boolean(kCachingSetting));
        m_live->searchKeys = std::make_unique<application::ai::SearchKeys>(*m_live->keys);
        applyWebSearch();
        m_live->relay = std::make_unique<backends::ai::HttpRelay>(backends::ai::withSearchTargets(
            m_live->requests->relayTargets(), *m_live->searchKeys,
            [requests = m_live->requests.get()](application::ai::SearchService service) {
                return requests->searchAccess(service);
            }));
        if (m_ports.signIn)
            m_live->signIn = m_ports.signIn(*m_live->keys);
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

std::unique_ptr<backends::ai::AiSidecarDownload> AiFeatures::sidecarDownload() const
{
    return m_live && m_ports.sidecarDownload ? m_ports.sidecarDownload() : nullptr;
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
    if (!m_live)
        return nullptr;
    if (!m_live->host)
        startSidecar();
    return m_live->host.get();
}

backends::ai::AiSidecarHost *AiFeatures::sidecarForAction()
{
    if (!m_live)
        return nullptr;
    if (!m_live->host && (m_live->providers->enabled() || m_live->drafting))
        startSidecar();
    return m_live->host.get();
}

void AiFeatures::startSidecar()
{
    if (!m_ports.sidecarHost)
        return;
    m_live->host = m_ports.sidecarHost();
    if (!m_live->host)
        return;
    m_live->relay->attach(*m_live->host);
    m_live->agents = std::make_unique<backends::ai::AgentBroker>();
    if (m_ports.agents)
        m_ports.agents(*m_live->agents);
    m_live->agents->attach(*m_live->host);
}

}
