#include "hikari/app/ai_features.h"
#include "hikari/app/ai_mcp.h"

#include "hikari/application/hotkeys.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/sandbox.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace hikari;
namespace ai = hikari::application::ai;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

struct Counts {
    int keyStores = 0;
    int keyReads = 0;
    int configStores = 0;
    int sidecarHosts = 0;
    int agentLists = 0;
};

class CountingKeyStore final : public ai::KeyStore {
public:
    explicit CountingKeyStore(Counts &counts) : m_counts(counts) {}
    void read(const std::string &, std::function<void(ai::KeyResult)> done) override
    {
        ++m_counts.keyReads;
        done(ai::KeyError::NotFound);
    }
    void write(const std::string &, const std::string &, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
    void remove(const std::string &, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }

private:
    Counts &m_counts;
};

class FixedConfigStore final : public ai::ConfigStore {
public:
    explicit FixedConfigStore(ai::Config config) : m_config(std::move(config)) {}
    ai::Config load() override { return m_config; }
    bool save(const ai::Config &config) override
    {
        m_config = config;
        return true;
    }

private:
    ai::Config m_config;
};

class AiFeaturesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_ui_ai_features_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    app::AiFeatures::Ports ports(bool withProvider)
    {
        ai::Config config;
        if (withProvider)
            config.providers.push_back(ai::presetProvider(ai::Preset::OpenAI, "openai", "OpenAI"));
        app::AiFeatures::Ports p;
        p.keyStore = [this] {
            ++counts.keyStores;
            return std::make_unique<CountingKeyStore>(counts);
        };
        p.configStore = [this, config] {
            ++counts.configStores;
            return std::make_unique<FixedConfigStore>(config);
        };
        p.sidecarHost = [this] {
            ++counts.sidecarHosts;
            backends::ai::AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {QStringLiteral("--mode"), QStringLiteral("ok"), QStringLiteral("--build-id"),
                           QStringLiteral("test"), QStringLiteral("--api-version"),
                           QString::number(api::kApiVersion)};
            o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
            o.buildId = "test";
            return std::make_unique<backends::ai::AiSidecarHost>(std::move(o), nullptr, api::Caller{"ai", "AI"});
        };
        p.agents = [this](backends::ai::AgentBroker &) { ++counts.agentLists; };
        return p;
    }

    QTemporaryDir dir;
    ui::SettingsStore settings;
    Counts counts;
};

TEST_F(AiFeaturesTest, WithAiOffNothingIsBuiltAcrossStartupAndASession)
{
    settings.set(app::AiFeatures::kOffSetting, true);
    app::AiFeatures features(settings, ports(true));
    EXPECT_FALSE(features.on());
    EXPECT_EQ(features.keys(), nullptr);
    EXPECT_EQ(features.configs(), nullptr);
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(features.sidecarForAction(), nullptr);
    settings.set("grammar.language", 1);
    EXPECT_EQ(counts.keyStores, 0);
    EXPECT_EQ(counts.keyReads, 0);
    EXPECT_EQ(counts.configStores, 0);
    EXPECT_EQ(counts.sidecarHosts, 0);
    EXPECT_EQ(counts.agentLists, 0);
}

TEST_F(AiFeaturesTest, WithoutAProviderNoSidecarIsBuiltOrStarted)
{
    app::AiFeatures features(settings, ports(false));
    EXPECT_TRUE(features.on());
    EXPECT_EQ(features.sidecarForAction(), nullptr);
    EXPECT_EQ(counts.sidecarHosts, 0);
    EXPECT_EQ(counts.keyReads, 0);
    EXPECT_EQ(counts.agentLists, 0);
}

TEST_F(AiFeaturesTest, StartupAndOpeningSettingsDontBuildTheSidecar)
{
    app::AiFeatures features(settings, ports(true));
    ASSERT_NE(features.configs(), nullptr);
    EXPECT_TRUE(features.configs()->load().enabled());
    EXPECT_EQ(features.sidecar(), nullptr);
    EXPECT_EQ(counts.sidecarHosts, 0);
}

TEST_F(AiFeaturesTest, TurningAiOffAtRuntimeStopsTheSidecarAndTurningItOnBuildsOnDemand)
{
    app::AiFeatures features(settings, ports(true));
    EXPECT_EQ(features.sandboxed(), backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete());
    auto *host = features.sidecarForAction();
    ASSERT_NE(host, nullptr);
    EXPECT_FALSE(features.sandboxed()) << "the test host starts its sidecar without a profile";
    bool answered = false;
    host->request("echo", api::Json(1), [&](backends::ai::SidecarResult r) { answered = r.has_value(); });
    ASSERT_TRUE(waitFor([&] { return answered; }));
    const qint64 pid = host->processId();
    ASSERT_GT(pid, 0);

    settings.set(app::AiFeatures::kOffSetting, true);
    EXPECT_FALSE(features.on());
    EXPECT_EQ(features.sidecar(), nullptr);
    EXPECT_EQ(features.keys(), nullptr);
#ifndef _WIN32
    EXPECT_TRUE(waitFor([&] { return !QFile::exists(QStringLiteral("/proc/%1").arg(pid)); }, 5'000));
#endif

    settings.set(app::AiFeatures::kOffSetting, false);
    EXPECT_TRUE(features.on());
    EXPECT_EQ(features.sidecar(), nullptr);
    EXPECT_NE(features.sidecarForAction(), nullptr);
    EXPECT_EQ(counts.keyStores, 2);
    EXPECT_EQ(counts.sidecarHosts, 2);
    EXPECT_EQ(counts.agentLists, 2);
}

TEST_F(AiFeaturesTest, NoMcpListenerUnlessTheUserTurnsItOnAndNeverWithAiOff)
{
    const QString file = backends::ai::mcpEndpointFile(dir.path());
    settings.set(app::AiFeatures::kOffSetting, true);
    settings.set(app::AiMcp::kSetting, true);
    settings.set(app::AiMcp::kHttpSetting, true);
    app::AiFeatures features(settings, ports(false));
    app::AiMcp mcp(settings, features, file);
    EXPECT_EQ(mcp.server(), nullptr);
    EXPECT_FALSE(QFile::exists(file));

    settings.set(app::AiFeatures::kOffSetting, false);
    ASSERT_NE(mcp.server(), nullptr);
    EXPECT_FALSE(mcp.server()->socketName().isEmpty());
    EXPECT_NE(mcp.server()->httpPort(), 0);
    EXPECT_EQ(counts.sidecarHosts, 0);
    const auto listening = backends::ai::readMcpEndpoint(file);
    ASSERT_TRUE(listening);
    EXPECT_EQ(listening->socket, mcp.server()->socketName());
    EXPECT_EQ(listening->port, mcp.server()->httpPort());

    settings.set(app::AiMcp::kSetting, false);
    EXPECT_EQ(mcp.server(), nullptr);
    EXPECT_TRUE(backends::ai::readMcpEndpoint(file)->socket.isEmpty());
    settings.set(app::AiMcp::kSetting, true);
    ASSERT_NE(mcp.server(), nullptr);
    settings.set(app::AiFeatures::kOffSetting, true);
    EXPECT_EQ(mcp.server(), nullptr);
    EXPECT_TRUE(backends::ai::readMcpEndpoint(file)->socket.isEmpty());
}

TEST_F(AiFeaturesTest, TheMcpTokenAndPortSurviveARestartAndHttpTurnsOffAlone)
{
    const QString file = backends::ai::mcpEndpointFile(dir.path());
    settings.set(app::AiMcp::kSetting, true);
    settings.set(app::AiMcp::kHttpSetting, true);
    app::AiFeatures features(settings, ports(false));
    QByteArray token;
    quint16 port = 0;
    {
        app::AiMcp mcp(settings, features, file);
        token = mcp.endpoint().token;
        port = mcp.server()->httpPort();
        settings.set(app::AiMcp::kHttpSetting, false);
        ASSERT_NE(mcp.server(), nullptr);
        EXPECT_EQ(mcp.server()->httpPort(), 0);
        EXPECT_EQ(backends::ai::readMcpEndpoint(file)->port, port);
        settings.set(app::AiMcp::kHttpSetting, true);
    }
    EXPECT_TRUE(backends::ai::readMcpEndpoint(file)->socket.isEmpty());
    app::AiMcp again(settings, features, file);
    EXPECT_EQ(again.endpoint().token, token);
    EXPECT_EQ(again.server()->httpPort(), port);
}

TEST(AiCommands, NoCommandOrMenuEntryIsAnAiOneWhileAiHasNoCommands)
{
    const QRegularExpression aiToken(QStringLiteral("(^|_)AI(_|$)"));
    for (const auto &action : application::hotkeyActions())
        EXPECT_FALSE(aiToken.match(QString::fromLatin1(action.symbol.data(), action.symbol.size())).hasMatch())
            << action.symbol;
    QFile ledger(QStringLiteral(HIKARI_MENU_LEDGER));
    ASSERT_TRUE(ledger.open(QIODevice::ReadOnly | QIODevice::Text));
    const QRegularExpression aiEntry(QStringLiteral("^AI\\b|\\[[A-Z_]*\\bAI_[A-Z_]*\\]|\\[[A-Z_]*_AI\\]"));
    while (!ledger.atEnd()) {
        const QString line = QString::fromUtf8(ledger.readLine()).trimmed();
        EXPECT_FALSE(aiEntry.match(line).hasMatch()) << line.toStdString();
    }
}

}
