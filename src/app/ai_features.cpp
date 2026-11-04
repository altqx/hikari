#include "hikari/app/ai_features.h"

#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_keychain.h"
#include "hikari/backends/ai_config_store.h"
#include "hikari/backends/ai_http_relay.h"
#include "hikari/backends/ai_sidecar_download.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "settings_store.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

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
    std::unique_ptr<backends::ai::HttpRelay> relay;
    std::unique_ptr<backends::ai::AiSidecarHost> host;
    std::unique_ptr<backends::ai::AgentBroker> agents;
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
    ports.agents = [ai](backends::ai::AgentBroker &broker) {
        const QString agents = ai + QStringLiteral("/agents");
        if (const QString claude = QStandardPaths::findExecutable(QStringLiteral("claude")); !claude.isEmpty())
            broker.add("claude", backends::ai::claudeAgent(claude, QDir::homePath(), agents + QStringLiteral("/claude")));
        if (const QString codex = QStandardPaths::findExecutable(QStringLiteral("codex")); !codex.isEmpty())
            if (auto spec = backends::ai::codexAgent(codex, agents + QStringLiteral("/codex/home"),
                                                     agents + QStringLiteral("/codex/work")))
                broker.add("codex", std::move(*spec));
    };
    return ports;
}

AiFeatures::AiFeatures(ui::SettingsStore &settings, Ports ports, QObject *parent)
    : QObject(parent), m_settings(settings), m_ports(std::move(ports))
{
    connect(&m_settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String(kOffSetting))
            apply();
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
        m_live->relay = std::make_unique<backends::ai::HttpRelay>(backends::ai::providerTargets(*m_live->providers));
    } else {
        m_live.reset();
    }
    emit switched(wanted);
}

application::ai::KeyStore *AiFeatures::keys() const
{
    return m_live ? m_live->keys.get() : nullptr;
}

application::ai::ConfigStore *AiFeatures::configs() const
{
    return m_live ? m_live->configs.get() : nullptr;
}

backends::ai::AiSidecarHost *AiFeatures::sidecar() const
{
    return m_live ? m_live->host.get() : nullptr;
}

backends::ai::AiSidecarHost *AiFeatures::sidecarForAction()
{
    if (!m_live)
        return nullptr;
    if (!m_live->host && m_live->configs && m_live->configs->load().enabled()) {
        m_live->host = m_ports.sidecarHost();
        if (m_live->host) {
            m_live->relay->attach(*m_live->host);
            m_live->agents = std::make_unique<backends::ai::AgentBroker>();
            if (m_ports.agents)
                m_ports.agents(*m_live->agents);
            m_live->agents->attach(*m_live->host);
        }
    }
    return m_live->host.get();
}

}
