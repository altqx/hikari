#include "hikari/app/ai_features.h"
#include "hikari/app/ai_mcp.h"

#include "hikari/application/hotkeys.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_sidecar_updater.h"
#include "hikari/backends/sandbox.h"
#include "local_release.h"
#include "minisign_signer.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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

    app::AiFeatures::Ports ports(bool withProvider, bool updates = false)
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
        p.sidecarHost = [this](const backends::ai::SidecarInstall &install) {
            ++counts.sidecarHosts;
            backends::ai::AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {install.bundle};
            o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
            o.handshakeMs = 3'000;
            return std::make_unique<backends::ai::AiSidecarHost>(std::move(o), nullptr, api::Caller{"ai", "AI"});
        };
        if (updates) {
            p.sidecarUpdater = [this] { return updater(); };
            p.autoUpdateOffByPolicy = [this] { return policyOff; };
            p.timing = {.checkMs = 60'000, .idleMs = 300, .pollMs = 20, .crashWindowMs = 60'000};
        } else {
            const QString flags = dir.filePath(QStringLiteral("local-bundle"));
            QFile file(flags);
            if (!file.open(QIODevice::WriteOnly) || file.write("--mode ok") != 9)
                ADD_FAILURE() << "can't write " << flags.toStdString();
            p.localSidecar = backends::ai::SidecarInstall{QStringLiteral("0.0.0-dev"), {}, flags, {}};
        }
        p.agents = [this](backends::ai::AgentBroker &) { ++counts.agentLists; };
        return p;
    }

    std::unique_ptr<backends::ai::AiSidecarUpdater> updater() const
    {
        return std::make_unique<backends::ai::AiSidecarUpdater>(
            dir.filePath(QStringLiteral("ai/sidecar")), backends::ai::SidecarCompat{1, api::kApiVersion},
            backends::ai::SidecarSource::local(release.base(), {signer.publicKey()}));
    }

    void publish(const QString &version, const QByteArray &bundle = "--mode ok")
    {
        channel.addRelease({.version = version, .bundle = bundle});
        channel.publish(signer);
    }

    void installNewest()
    {
        auto store = updater();
        bool done = false;
        QObject::connect(store.get(), &backends::ai::AiSidecarUpdater::downloaded, [&] { done = true; });
        QObject::connect(store.get(), &backends::ai::AiSidecarUpdater::failed, [&] { done = true; });
        store->check(true);
        ASSERT_TRUE(waitFor([&] { return done; }));
        ASSERT_TRUE(store->switchToStaged());
    }

    void SetUp() override { channel.addRuntime(QStringLiteral("1.4.4-0123abcd"), "runtime"); }

    QTemporaryDir dir;
    ui::SettingsStore settings;
    Counts counts;
    test::LocalRelease release;
    test::FakeChannel channel{release};
    test::MinisignSigner signer;
    bool policyOff = false;
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

TEST_F(AiFeaturesTest, ASidecarStartedForMcpServesAiActionsWithItsRelayAndAgents)
{
    app::AiFeatures features(settings, ports(false));
    auto *host = features.sidecarForMcp();
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(counts.agentLists, 1);
    EXPECT_EQ(features.sidecarForAction(), host);
    EXPECT_EQ(counts.sidecarHosts, 1);
    EXPECT_EQ(counts.agentLists, 1);
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

TEST_F(AiFeaturesTest, CopySetupStartsThisProgramAndGivesJsonOnlyWhileHttpListens)
{
    qunsetenv("APPIMAGE");
    settings.set(app::AiMcp::kSetting, true);
    app::AiFeatures features(settings, ports(false));
    app::AiMcp mcp(settings, features, backends::ai::mcpEndpointFile(dir.path()));
    const QString program = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    EXPECT_EQ(app::AiMcp::program(), program);
    EXPECT_EQ(mcp.claudeCodeSetup(), QStringLiteral("claude mcp add hikari -- \"%1\" --mcp").arg(program));
    QString escaped = program;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    EXPECT_EQ(mcp.codexSetup(), QStringLiteral("[mcp_servers.hikari]\ncommand = \"%1\"\nargs = [\"--mcp\"]\n").arg(escaped));
    EXPECT_TRUE(mcp.httpSetup().isEmpty());
    EXPECT_EQ(mcp.setup().value(QStringLiteral("port")).toInt(), 0);

    int changed = 0;
    QObject::connect(&mcp, &app::AiMcp::changed, [&] { ++changed; });
    settings.set(app::AiMcp::kHttpSetting, true);
    EXPECT_EQ(changed, 1);
    const quint16 port = mcp.server()->httpPort();
    const QJsonObject hikari = QJsonDocument::fromJson(mcp.httpSetup().toUtf8())
                                   .object()
                                   .value(QStringLiteral("mcpServers"))
                                   .toObject()
                                   .value(QStringLiteral("hikari"))
                                   .toObject();
    EXPECT_EQ(hikari.value(QStringLiteral("type")).toString(), QStringLiteral("http"));
    EXPECT_EQ(hikari.value(QStringLiteral("url")).toString(), QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    EXPECT_EQ(hikari.value(QStringLiteral("headers")).toObject().value(QStringLiteral("Authorization")).toString(),
              QStringLiteral("Bearer ") + QString::fromLatin1(mcp.endpoint().token));
    EXPECT_EQ(mcp.setup().value(QStringLiteral("port")).toInt(), port);

    qputenv("APPIMAGE", "/home/me/Apps/HikariSub.AppImage");
    EXPECT_EQ(app::AiMcp::program(), QDir::toNativeSeparators(QStringLiteral("/home/me/Apps/HikariSub.AppImage")));
    qunsetenv("APPIMAGE");
}

TEST_F(AiFeaturesTest, ResetTokenSavesANewOneThatTheServerAndTheJsonSetupUse)
{
    settings.set(app::AiMcp::kSetting, true);
    settings.set(app::AiMcp::kHttpSetting, true);
    app::AiFeatures features(settings, ports(false));
    const QString file = backends::ai::mcpEndpointFile(dir.path());
    app::AiMcp mcp(settings, features, file);
    const QByteArray old = mcp.endpoint().token;
    int changed = 0;
    QObject::connect(&mcp, &app::AiMcp::changed, [&] { ++changed; });

    mcp.resetToken();
    EXPECT_EQ(changed, 1);
    const QByteArray token = mcp.endpoint().token;
    EXPECT_EQ(token.size(), 64);
    EXPECT_NE(token, old);
    EXPECT_EQ(backends::ai::readMcpEndpoint(file)->token, token);
    EXPECT_TRUE(mcp.httpSetup().contains(QString::fromLatin1(token)));
    EXPECT_FALSE(mcp.httpSetup().contains(QString::fromLatin1(old)));
#ifndef _WIN32
    EXPECT_EQ(QFile::permissions(file) & ~(QFile::ReadUser | QFile::WriteUser | QFile::ReadOwner | QFile::WriteOwner),
              QFile::Permissions());
#endif
}

TEST_F(AiFeaturesTest, ASidecarThatFailsItsHandshakeRollsBackToThePreviousOne)
{
    publish(QStringLiteral("1.1.0"));
    installNewest();
    publish(QStringLiteral("1.2.0"), "--required-api 99");
    installNewest();
    policyOff = true;
    app::AiFeatures::Ports p = ports(false, true);
    std::vector<std::pair<QString, QString>> reported;
    p.reportRollback = [&](const QString &version, const QString &reason) {
        reported.emplace_back(version, reason);
        return true;
    };
    app::AiFeatures features(settings, std::move(p));
    ASSERT_EQ(features.updater()->current()->version, QStringLiteral("1.2.0"));
    std::optional<QString> rolledBack;
    QObject::connect(&features, &app::AiFeatures::rolledBack, [&](const QString &v) { rolledBack = v; });

    features.sidecarForMcp()->request("echo", api::Json(1), [](backends::ai::SidecarResult) {});
    ASSERT_TRUE(waitFor([&] { return rolledBack.has_value(); }));
    EXPECT_EQ(*rolledBack, QStringLiteral("1.2.0"));
    EXPECT_EQ(features.updater()->current()->version, QStringLiteral("1.1.0"));
    EXPECT_EQ(features.sidecar(), nullptr);
    EXPECT_EQ(reported, (std::vector<std::pair<QString, QString>>{{QStringLiteral("1.2.0"), QStringLiteral("initialize")}}));
    const auto notice = features.updater()->rollback();
    ASSERT_TRUE(notice);
    EXPECT_EQ(notice->version, QStringLiteral("1.2.0"));
    EXPECT_EQ(notice->to, QStringLiteral("1.1.0"));
    EXPECT_EQ(notice->reason, QStringLiteral("initialize"));
    EXPECT_TRUE(notice->reportSent);
    EXPECT_EQ(features.versionProperties().value(QStringLiteral("ai_component_version")).toString(), QStringLiteral("1.1.0"));

    bool answered = false;
    features.sidecarForMcp()->request("echo", api::Json(1), [&](backends::ai::SidecarResult r) { answered = r.has_value(); });
    ASSERT_TRUE(waitFor([&] { return answered; }));
    EXPECT_EQ(counts.sidecarHosts, 2);
}

TEST_F(AiFeaturesTest, WithoutAPreviousSidecarAFailingOneKeepsRunning)
{
    publish(QStringLiteral("1.1.0"), "--required-api 99");
    installNewest();
    policyOff = true;
    app::AiFeatures features(settings, ports(false, true));
    bool rolledBack = false;
    QObject::connect(&features, &app::AiFeatures::rolledBack, [&] { rolledBack = true; });
    std::optional<backends::ai::SidecarResult> result;
    features.sidecarForMcp()->request("echo", api::Json(1), [&](backends::ai::SidecarResult r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_FALSE(*result);
    EXPECT_FALSE(rolledBack);
    EXPECT_EQ(features.updater()->current()->version, QStringLiteral("1.1.0"));
}

TEST_F(AiFeaturesTest, ANewSidecarSwitchesInOnlyOnceNothingHasBeenBusyForTheIdleTime)
{
    settings.set(app::AiFeatures::kAutoUpdateSetting, true);
    publish(QStringLiteral("1.1.0"));
    installNewest();
    app::AiFeatures features(settings, ports(false, true));
    ASSERT_TRUE(waitFor([&] { return !features.updater()->busy(); }));
    bool answered = false;
    features.sidecarForMcp()->request("echo", api::Json(1), [&](backends::ai::SidecarResult r) { answered = r.has_value(); });
    ASSERT_TRUE(waitFor([&] { return answered; }));

    bool turn = true;
    const int source = features.addBusySource([&] { return turn; });
    publish(QStringLiteral("1.2.0"));
    features.checkNow();
    ASSERT_TRUE(waitFor([&] { return features.updater()->staged().has_value(); }));
    waitFor([] { return false; }, 800);
    EXPECT_EQ(features.updater()->current()->version, QStringLiteral("1.1.0"));
    EXPECT_NE(features.sidecar(), nullptr);

    turn = false;
    QElapsedTimer idle;
    idle.start();
    ASSERT_TRUE(waitFor([&] { return features.updater()->current()->version == QLatin1String("1.2.0"); }));
    EXPECT_GE(idle.elapsed(), 250);
    EXPECT_EQ(features.sidecar(), nullptr);
    EXPECT_EQ(features.updater()->previous(), QStringLiteral("1.1.0"));
    features.removeBusySource(source);
}

TEST_F(AiFeaturesTest, ThePolicyKeepsChecksButNeverDownloads)
{
    settings.set(app::AiFeatures::kAutoUpdateSetting, true);
    publish(QStringLiteral("1.1.0"));
    installNewest();
    publish(QStringLiteral("1.2.0"));
    policyOff = true;
    app::AiFeatures features(settings, ports(false, true));
    EXPECT_FALSE(features.autoUpdate());
    ASSERT_TRUE(waitFor([&] { return features.updater()->available() == QLatin1String("1.2.0"); }));
    ASSERT_TRUE(waitFor([&] { return !features.updater()->busy(); }));
    EXPECT_FALSE(features.updater()->staged());
    EXPECT_EQ(release.fetches(".zip"), 1);
}

TEST(AiAutoUpdatePolicy, ReadsTheEnvironmentAndThePolicyFile)
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("policy.json"));
    const auto write = [&](const QByteArray &json) {
        QFile out(file);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(json);
    };
    EXPECT_FALSE(app::aiAutoUpdateOffByPolicy({}, file));
    EXPECT_TRUE(app::aiAutoUpdateOffByPolicy(QStringLiteral("0"), file));
    write(R"({"ai_auto_update": false})");
    EXPECT_TRUE(app::aiAutoUpdateOffByPolicy({}, file));
    write(R"({"ai_auto_update": true})");
    EXPECT_FALSE(app::aiAutoUpdateOffByPolicy({}, file));
    write(R"({"ai_auto_update": 0})");
    EXPECT_FALSE(app::aiAutoUpdateOffByPolicy({}, file));
}

TEST(AiCommands, TheSwitchTakesEveryCommandWithAnAiToken)
{
    const QRegularExpression aiToken(QStringLiteral("(^|_)AI(_|$)"));
    for (const auto &action : application::hotkeyActions())
        EXPECT_EQ(aiToken.match(QString::fromLatin1(action.symbol.data(), action.symbol.size())).hasMatch(),
                  application::aiCommand(action.id))
            << action.symbol;
    QFile ledger(QStringLiteral(HIKARI_MENU_LEDGER));
    ASSERT_TRUE(ledger.open(QIODevice::ReadOnly | QIODevice::Text));
    const QRegularExpression command(QStringLiteral(R"(\[([A-Z0-9_]+)\]$)"));
    while (!ledger.atEnd()) {
        const QString line = QString::fromUtf8(ledger.readLine()).trimmed();
        const auto found = command.match(line);
        if (found.hasMatch() && aiToken.match(found.captured(1)).hasMatch())
            EXPECT_TRUE(application::aiCommand(application::hotkeyIdOf(found.captured(1).toStdString())))
                << line.toStdString();
    }
}

}
