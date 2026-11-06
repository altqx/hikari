#include "hikari/app/ai_settings.h"

#include "hikari/app/ai_features.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_updater.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_signin.h"
#include "hikari/backends/desktop_handoff.h"
#include "fake_auth_server.h"
#include "local_release.h"
#include "minisign_signer.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
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

struct Keychain {
    std::map<std::string, std::string> keys;
    std::optional<ai::KeyError> failWith;
};

class MemoryKeys final : public ai::KeyStore {
public:
    explicit MemoryKeys(Keychain &chain) : m_chain(chain) {}
    void read(const std::string &id, std::function<void(ai::KeyResult)> done) override
    {
        const auto it = m_chain.keys.find(id);
        done(it == m_chain.keys.end() ? ai::KeyResult(ai::KeyError::NotFound) : ai::KeyResult(it->second));
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        if (m_chain.failWith) {
            done(m_chain.failWith);
            return;
        }
        m_chain.keys[id] = key;
        done(std::nullopt);
    }
    void remove(const std::string &id, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        m_chain.keys.erase(id);
        done(std::nullopt);
    }

private:
    Keychain &m_chain;
};

class MemoryConfigs final : public ai::ConfigStore {
public:
    explicit MemoryConfigs(ai::Config &stored) : m_stored(stored) {}
    ai::Config load() override { return m_stored; }
    bool save(const ai::Config &config) override
    {
        m_stored = config;
        return true;
    }

private:
    ai::Config &m_stored;
};

class AiSettingsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_ui_ai_settings_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    app::AiFeatures::Ports ports(bool download = false)
    {
        app::AiFeatures::Ports p;
        p.keyStore = [this] { return std::make_unique<MemoryKeys>(chain); };
        p.configStore = [this] { return std::make_unique<MemoryConfigs>(stored); };
        p.sidecarHost = [this](const backends::ai::SidecarInstall &install) {
            ++hostsBuilt;
            backends::ai::AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {install.bundle};
            o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
            return std::make_unique<backends::ai::AiSidecarHost>(std::move(o), nullptr, api::Caller{"ai", "AI"});
        };
        if (download) {
            p.sidecarUpdater = [this] {
                return std::make_unique<backends::ai::AiSidecarUpdater>(
                    dir.filePath(QStringLiteral("ai/sidecar")), backends::ai::SidecarCompat{1, api::kApiVersion},
                    backends::ai::SidecarSource::local(release.base(), {signer.publicKey()}));
            };
        } else {
            const QString flags = dir.filePath(QStringLiteral("local-bundle"));
            QFile file(flags);
            if (!file.open(QIODevice::WriteOnly) || file.write("--mode ok") != 9)
                ADD_FAILURE() << "can't write " << flags.toStdString();
            p.localSidecar = backends::ai::SidecarInstall{QStringLiteral("0.0.0-dev"), {}, flags, {}};
        }
        p.signIn = [this](ai::KeyStore &keys) {
            return std::make_unique<backends::ai::AiSignIn>(keys, auth.endpoints(browserPort), time.clock());
        };
        p.findCodex = [this](std::function<void(std::optional<backends::ai::CodexCli>)> done) { done(codexCli); };
        p.codexAuthPath = dir.filePath(QStringLiteral("codex/auth.json"));
        return p;
    }

    void serveSidecar()
    {
        channel.addRuntime(QStringLiteral("1.4.4-0123abcd"), QByteArray("#!/bin/sh\necho sidecar\n").repeated(4000));
        channel.addRelease({QStringLiteral("1.1.0")});
        channel.publish(signer);
    }

    static QVariantMap form(const QString &preset, const QString &name, const QString &key, const QString &id = {},
                            const QString &baseUrl = {})
    {
        return {{QStringLiteral("id"), id},         {QStringLiteral("preset"), preset},
                {QStringLiteral("name"), name},     {QStringLiteral("key"), key},
                {QStringLiteral("baseUrl"), baseUrl}, {QStringLiteral("wire"), QStringLiteral("chat-completions")}};
    }
    static QString draftState(const app::AiSettings &s) { return s.draft().value(QStringLiteral("state")).toString(); }
    static QString draftError(const app::AiSettings &s) { return s.draft().value(QStringLiteral("error")).toString(); }
    static QVariantMap row(const app::AiSettings &s, int i) { return s.providers().value(i).toMap(); }
    static QVariant codex(const app::AiSettings &s, const char *key) { return s.codex().value(QLatin1String(key)); }

    void SetUp() override
    {
        auth.script("ok");
        previousRoutes = backends::desktop::replaceRoutes({.openUrl = [this](const QUrl &url) {
            opened.push_back(url);
            return true;
        }});
    }
    void TearDown() override { backends::desktop::replaceRoutes(previousRoutes); }

    /// The OpenRouter page the sign-in opened, approved as the user would in the browser.
    void approveOpenRouter()
    {
        ASSERT_TRUE(waitFor([&] { return !opened.empty() && opened.back().host() == QLatin1String("openrouter.ai"); }));
        EXPECT_EQ(test::browse(test::approval(opened.back())), 200);
    }

    QTemporaryDir dir;
    ui::SettingsStore settings;
    Keychain chain;
    ai::Config stored;
    test::LocalRelease release;
    test::FakeChannel channel{release};
    test::MinisignSigner signer;
    int hostsBuilt = 0;
    std::optional<backends::ai::CodexCli> codexCli;
    std::vector<QUrl> opened;
    backends::desktop::Routes previousRoutes;
    test::FakeAuthServer auth;
    quint16 browserPort = test::freePort();
    test::FakeSignInClock time{.realMs = 20};
};

}

TEST_F(AiSettingsTest, AddingGeminiTestsTheTypedKeyThenStoresItAndItsModels)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    EXPECT_TRUE(page.providers().isEmpty());
    page.openDraft();
    EXPECT_FALSE(page.download().value(QStringLiteral("needed")).toBool());

    page.testDraft(form(QStringLiteral("gemini"), QStringLiteral("Google Gemini"), QStringLiteral("AIza-typed")));
    ASSERT_TRUE(waitFor([&] { return draftState(page) == QLatin1String("ok"); }));
    EXPECT_EQ(page.draft().value(QStringLiteral("models")).toInt(), 1);
    EXPECT_TRUE(chain.keys.empty());
    EXPECT_TRUE(stored.providers.empty());

    QSignalSpy saved(&page, &app::AiSettings::draftSaved);
    page.saveDraft(form(QStringLiteral("gemini"), QStringLiteral("Google Gemini"), QStringLiteral("AIza-typed")));
    ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
    EXPECT_EQ(saved.first().first().toString(), QStringLiteral("gemini"));
    EXPECT_EQ(chain.keys.at("gemini"), "AIza-typed");
    ASSERT_EQ(page.providers().size(), 1);
    EXPECT_EQ(row(page, 0).value(QStringLiteral("name")).toString(), QStringLiteral("Google Gemini"));
    EXPECT_EQ(row(page, 0).value(QStringLiteral("wire")).toString(), QStringLiteral("gemini"));
    EXPECT_EQ(row(page, 0).value(QStringLiteral("models")).toInt(), 1);
    EXPECT_EQ(row(page, 0).value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
    EXPECT_EQ(page.roles().value(QStringLiteral("chat")).toString(), QStringLiteral("gemini\tb/model"));
    EXPECT_EQ(stored.providers.at(0).baseUrl, "https://generativelanguage.googleapis.com/v1beta");
}

TEST_F(AiSettingsTest, ANewProviderNeedsAKeyAndACustomOneAValidAddress)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.openDraft();
    page.testDraft(form(QStringLiteral("anthropic"), QStringLiteral("Anthropic"), {}));
    EXPECT_EQ(draftError(page), QStringLiteral("key"));
    page.saveDraft(form(QStringLiteral("custom"), QStringLiteral("Local"), {}, {}, QStringLiteral("localhost:11434")));
    EXPECT_EQ(draftError(page), QStringLiteral("form"));
    EXPECT_EQ(hostsBuilt, 0);

    QSignalSpy saved(&page, &app::AiSettings::draftSaved);
    page.saveDraft(form(QStringLiteral("custom"), QStringLiteral("Ollama on this computer"), {}, {},
                        QStringLiteral("http://localhost:11434/v1")));
    ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
    EXPECT_FALSE(row(page, 0).value(QStringLiteral("needsKey")).toBool());
    EXPECT_EQ(row(page, 0).value(QStringLiteral("id")).toString(), QStringLiteral("custom"));
    EXPECT_TRUE(chain.keys.empty());
    ASSERT_TRUE(waitFor([&] { return row(page, 0).value(QStringLiteral("state")).toString() == QLatin1String("ok"); }));
}

TEST_F(AiSettingsTest, AFailedKeychainWriteAddsNothingAndSaysWhy)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    chain.failWith = ai::KeyError::Denied;
    page.saveDraft(form(QStringLiteral("openai"), QStringLiteral("OpenAI"), QStringLiteral("sk-typed")));
    EXPECT_EQ(draftError(page), QStringLiteral("keychainDenied"));
    EXPECT_TRUE(page.providers().isEmpty());
}

TEST_F(AiSettingsTest, EditingKeepsTheStoredKeyUnlessANewOneIsTypedAndDeletingRemovesIt)
{
    stored.providers.push_back(ai::presetProvider(ai::Preset::OpenRouter, "openrouter", "OpenRouter"));
    stored.providers.push_back(ai::presetProvider(ai::Preset::Anthropic, "anthropic", "Anthropic"));
    chain.keys = {{"openrouter", "sk-or"}, {"anthropic", "sk-ant"}};
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    EXPECT_EQ(page.form(QStringLiteral("openrouter")).value(QStringLiteral("name")).toString(), QStringLiteral("OpenRouter"));

    QSignalSpy saved(&page, &app::AiSettings::draftSaved);
    page.saveDraft(form(QStringLiteral("openrouter"), QStringLiteral("My OpenRouter"), {}, QStringLiteral("openrouter")));
    ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
    EXPECT_EQ(row(page, 0).value(QStringLiteral("name")).toString(), QStringLiteral("My OpenRouter"));
    EXPECT_EQ(chain.keys.at("openrouter"), "sk-or");

    page.testDraft(form(QStringLiteral("openrouter"), QStringLiteral("My OpenRouter"), {}, QStringLiteral("openrouter")));
    ASSERT_TRUE(waitFor([&] { return draftState(page) == QLatin1String("ok"); }));

    page.saveDraft(form(QStringLiteral("openrouter"), QStringLiteral("My OpenRouter"), QStringLiteral("sk-new"),
                        QStringLiteral("openrouter")));
    ASSERT_TRUE(waitFor([&] { return saved.count() == 2; }));
    EXPECT_EQ(chain.keys.at("openrouter"), "sk-new");
    EXPECT_EQ(row(page, 0).value(QStringLiteral("id")).toString(), QStringLiteral("openrouter"));

    page.remove(QStringLiteral("openrouter"));
    ASSERT_EQ(page.providers().size(), 1);
    EXPECT_EQ(row(page, 0).value(QStringLiteral("id")).toString(), QStringLiteral("anthropic"));
    EXPECT_FALSE(chain.keys.contains("openrouter"));
}

TEST_F(AiSettingsTest, TestingAProviderShowsTheProvidersAnswer)
{
    stored.providers.push_back(ai::presetProvider(ai::Preset::OpenAI, "openai", "OpenAI"));
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.test(QStringLiteral("openai"));
    ASSERT_TRUE(waitFor([&] { return row(page, 0).value(QStringLiteral("state")).toString() == QLatin1String("failed"); }));
    EXPECT_EQ(row(page, 0).value(QStringLiteral("error")).toString(), QStringLiteral("key"));

    chain.keys["openai"] = "sk";
    page.refreshModels();
    ASSERT_TRUE(waitFor([&] { return row(page, 0).value(QStringLiteral("state")).toString() == QLatin1String("ok"); }));
    EXPECT_EQ(row(page, 0).value(QStringLiteral("models")).toInt(), 1);
}

TEST_F(AiSettingsTest, RoleModelsChangeOnlyWhenApplied)
{
    ai::Provider google = ai::presetProvider(ai::Preset::Gemini, "gemini", "Google Gemini");
    google.models = {{.id = "flash", .name = "Flash"}, {.id = "pro", .name = "Pro"}};
    ai::Provider openRouter = ai::presetProvider(ai::Preset::OpenRouter, "openrouter", "OpenRouter");
    openRouter.models = {{.id = "text/only", .name = "Text only"}};
    stored.providers = {google, openRouter};
    stored.roles[ai::Role::Chat] = {"gemini", "flash"};
    stored.roles[ai::Role::Reviewer] = {"gemini", "pro"};
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);

    QStringList vision;
    for (const QVariant &choice : page.modelChoices())
        if (choice.toMap().value(QStringLiteral("vision")).toBool())
            vision << choice.toMap().value(QStringLiteral("value")).toString();
    EXPECT_EQ(vision, (QStringList{QStringLiteral("gemini\tflash"), QStringLiteral("gemini\tpro")}));
    EXPECT_EQ(page.roles().value(QStringLiteral("translator")).toString(), QString());

    page.applyRoles({{QStringLiteral("chat"), QStringLiteral("gemini\tpro")},
                     {QStringLiteral("translator"), QStringLiteral("openrouter\ttext/only")},
                     {QStringLiteral("reviewer"), QString()}});
    EXPECT_EQ(stored.roles.at(ai::Role::Chat), (ai::ModelRef{"gemini", "pro"}));
    EXPECT_EQ(stored.roles.at(ai::Role::Translator), (ai::ModelRef{"openrouter", "text/only"}));
    EXPECT_FALSE(stored.roles.contains(ai::Role::Reviewer));
    page.applyRoles({{QStringLiteral("chat"), QString()}});
    EXPECT_EQ(stored.roles.at(ai::Role::Chat), (ai::ModelRef{"gemini", "pro"}));
}

TEST_F(AiSettingsTest, TheFirstProviderDownloadsTheComponentOnceItsSizeIsShown)
{
    serveSidecar();
    app::AiFeatures features(settings, ports(true));
    app::AiSettings page(features);
    EXPECT_TRUE(release.requested.empty());
    page.openDraft();
    ASSERT_TRUE(waitFor([&] { return page.download().value(QStringLiteral("size")).toLongLong() > 0; }));
    EXPECT_TRUE(page.download().value(QStringLiteral("needed")).toBool());
    EXPECT_FALSE(page.download().value(QStringLiteral("active")).toBool());
    ASSERT_EQ(release.requested.size(), 2u);
    EXPECT_EQ(hostsBuilt, 0);

    page.testDraft(form(QStringLiteral("anthropic"), QStringLiteral("Anthropic"), QStringLiteral("sk-ant")));
    ASSERT_TRUE(waitFor([&] { return draftState(page) == QLatin1String("ok"); }));
    EXPECT_FALSE(page.download().value(QStringLiteral("needed")).toBool());
    EXPECT_EQ(release.requested.size(), 6u);
    EXPECT_EQ(hostsBuilt, 1);

    page.closeDraft();
    page.openDraft();
    EXPECT_FALSE(page.download().value(QStringLiteral("needed")).toBool());
    EXPECT_EQ(release.requested.size(), 6u);
}

TEST_F(AiSettingsTest, AFailedDownloadFailsTheTestAndSaysSo)
{
    app::AiFeatures features(settings, ports(true));
    app::AiSettings page(features);
    page.openDraft();
    page.testDraft(form(QStringLiteral("anthropic"), QStringLiteral("Anthropic"), QStringLiteral("sk-ant")));
    ASSERT_TRUE(waitFor([&] { return draftState(page) == QLatin1String("failed"); }));
    EXPECT_EQ(draftError(page), QStringLiteral("downloadManifest"));
    EXPECT_EQ(hostsBuilt, 0);
}

TEST_F(AiSettingsTest, AManifestSignedByAnotherKeyFailsTheTestAndInstallsNothing)
{
    serveSidecar();
    channel.publish(test::MinisignSigner());
    app::AiFeatures features(settings, ports(true));
    app::AiSettings page(features);
    page.openDraft();
    page.testDraft(form(QStringLiteral("anthropic"), QStringLiteral("Anthropic"), QStringLiteral("sk-ant")));
    ASSERT_TRUE(waitFor([&] { return draftState(page) == QLatin1String("failed"); }));
    EXPECT_EQ(draftError(page), QStringLiteral("downloadManifest"));
    EXPECT_TRUE(page.download().value(QStringLiteral("needed")).toBool());
    EXPECT_EQ(hostsBuilt, 0);
    for (const QByteArray &path : release.requested)
        EXPECT_FALSE(path.endsWith(".gz")) << path.constData();
}

TEST_F(AiSettingsTest, TurningAiOffEmptiesThePageAndBuildsNothing)
{
    stored.providers.push_back(ai::presetProvider(ai::Preset::OpenAI, "openai", "OpenAI"));
    settings.set(app::AiFeatures::kOffSetting, true);
    app::AiFeatures features(settings, ports(true));
    app::AiSettings page(features);
    EXPECT_FALSE(page.on());
    EXPECT_TRUE(page.providers().isEmpty());
    page.openDraft();
    page.test(QStringLiteral("openai"));
    page.saveDraft(form(QStringLiteral("openai"), QStringLiteral("OpenAI"), QStringLiteral("sk")));
    EXPECT_TRUE(release.requested.empty());
    EXPECT_TRUE(chain.keys.empty());

    QSignalSpy switched(&page, &app::AiSettings::switched);
    settings.set(app::AiFeatures::kOffSetting, false);
    EXPECT_EQ(switched.count(), 1);
    EXPECT_TRUE(page.on());
    EXPECT_EQ(page.providers().size(), 1);
    EXPECT_EQ(hostsBuilt, 0);
}

TEST_F(AiSettingsTest, TheCachingSettingReachesEveryRequest)
{
    ai::Provider openAi = ai::presetProvider(ai::Preset::OpenAI, "openai", "OpenAI");
    openAi.models = {{.id = "m", .name = "M"}};
    stored.providers.push_back(openAi);
    stored.roles[ai::Role::Chat] = {"openai", "m"};
    chain.keys["openai"] = "sk";
    settings.set(app::AiFeatures::kCachingSetting, false);
    app::AiFeatures features(settings, ports());
    const auto run = [&] {
        bool done = false;
        features.requests()->complete(ai::Role::Chat, api::Json::object({{"messages", api::Json::array()}}),
                                      [&](const ai::Event &e) {
                                          done = std::holds_alternative<ai::Finished>(e) ||
                                                 std::holds_alternative<ai::Failed>(e);
                                      });
        EXPECT_TRUE(waitFor([&] { return done; }));
    };
    run();
    settings.set(app::AiFeatures::kCachingSetting, true);
    run();
    ASSERT_NE(features.sidecar(), nullptr);
    std::optional<backends::ai::SidecarResult> received;
    features.sidecar()->request("received", {}, [&](backends::ai::SidecarResult r) { received = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return received.has_value(); }));
    ASSERT_TRUE(*received);
    const auto &sent = (**received).asArray();
    ASSERT_EQ(sent.size(), 4u);
    EXPECT_EQ(*sent[1].find("params")->find("request")->find("caching"), api::Json(false));
    EXPECT_EQ(sent[3].find("params")->find("request")->find("caching"), nullptr);
}

TEST_F(AiSettingsTest, CodexIsFoundOnPathWithItsVersion)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.checkCodex();
    EXPECT_FALSE(codex(page, "found").toBool());
    EXPECT_FALSE(codex(page, "signedIn").toBool());
    EXPECT_FALSE(codex(page, "canImport").toBool());

    codexCli = backends::ai::CodexCli{QStringLiteral("/usr/local/bin/codex"), QStringLiteral("0.58.0")};
    page.checkCodex();
    EXPECT_TRUE(codex(page, "found").toBool());
    EXPECT_EQ(codex(page, "path").toString(), QStringLiteral("/usr/local/bin/codex"));
    EXPECT_EQ(codex(page, "version").toString(), QStringLiteral("0.58.0"));
    EXPECT_EQ(hostsBuilt, 0);
}

TEST_F(AiSettingsTest, ACodexSignInShowsTheCodeOpensItsPageOnlyWhenAskedAndCancels)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    auth.script("hang");
    page.signInCodex(false);
    ASSERT_TRUE(waitFor([&] { return codex(page, "state").toString() == QLatin1String("code"); }));
    EXPECT_EQ(codex(page, "userCode").toString(), QStringLiteral("FAKE-1234"));
    EXPECT_GT(codex(page, "expiresAt").toLongLong(), QDateTime::currentMSecsSinceEpoch());
    EXPECT_TRUE(codex(page, "browserAvailable").toBool());
    EXPECT_TRUE(opened.empty());
    page.openSignInPage();
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], QUrl(QStringLiteral("https://auth.openai.com/codex/device")));

    page.cancelSignIn();
    EXPECT_EQ(codex(page, "state").toString(), QString());
    EXPECT_EQ(codex(page, "error").toString(), QString());
    EXPECT_FALSE(codex(page, "signedIn").toBool());
    EXPECT_FALSE(chain.keys.contains(backends::ai::kCodexSecret));
}

TEST_F(AiSettingsTest, TheBrowserSignInOpensAuthOpenaiComAtOnce)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    auth.script("hang");
    page.signInCodex(true);
    ASSERT_TRUE(waitFor([&] { return codex(page, "state").toString() == QLatin1String("browser"); }));
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0].host(), QStringLiteral("auth.openai.com"));
    EXPECT_EQ(opened[0].path(), QStringLiteral("/oauth/authorize"));
    page.cancelSignIn();
}

TEST_F(AiSettingsTest, ASignedInCodexShowsItsEmailUntilSignedOut)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.signInCodex(false);
    ASSERT_TRUE(waitFor([&] { return codex(page, "signedIn").toBool(); }));
    EXPECT_EQ(codex(page, "email").toString(), QStringLiteral("owner@example.com"));
    EXPECT_EQ(codex(page, "state").toString(), QString());
    ASSERT_TRUE(chain.keys.contains(backends::ai::kCodexSecret));
    EXPECT_EQ(backends::ai::sessionFromJson(chain.keys.at(backends::ai::kCodexSecret))->accountId, "acct-fake");

    page.signOutCodex();
    EXPECT_FALSE(codex(page, "signedIn").toBool());
    EXPECT_FALSE(chain.keys.contains(backends::ai::kCodexSecret));
}

TEST_F(AiSettingsTest, AWorkspaceWithoutCodesSaysSoAndNothingIsStored)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    auth.script("codesOff");
    page.signInCodex(false);
    ASSERT_TRUE(waitFor([&] { return codex(page, "error").toString() == QLatin1String("codesOff"); }));
    EXPECT_EQ(codex(page, "host").toString(), QStringLiteral("auth.openai.com"));
    EXPECT_FALSE(codex(page, "signedIn").toBool());
    EXPECT_TRUE(chain.keys.empty());
    page.cancelSignIn();
    EXPECT_EQ(codex(page, "error").toString(), QString());
}

TEST_F(AiSettingsTest, ImportCopiesTheCodexSignInAndLeavesAuthJsonAlone)
{
    const QString path = dir.filePath(QStringLiteral("codex/auth.json"));
    QDir().mkpath(QFileInfo(path).absolutePath());
    const auto jwt = [](const QByteArray &claims) {
        return "e30." + claims.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) + ".sig";
    };
    const QByteArray contents = R"({"tokens":{"id_token":")" + jwt(R"({"email":"cli@example.com"})") +
                                R"(","access_token":"a.b.c","refresh_token":"r","account_id":"acct"}})";
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.checkCodex();
    EXPECT_TRUE(codex(page, "canImport").toBool());
    page.importCodex();
    EXPECT_TRUE(codex(page, "signedIn").toBool());
    EXPECT_EQ(codex(page, "email").toString(), QStringLiteral("cli@example.com"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), contents);
    EXPECT_EQ(hostsBuilt, 0);
}

TEST_F(AiSettingsTest, AnExpiredSessionIsRefreshedAndARefusedRefreshAsksToSignInAgain)
{
    const backends::ai::CodexSession old{.access = "old", .refresh = "r", .idToken = "i", .accountId = "a",
                                         .email = "owner@example.com", .expiresAt = 1};
    chain.keys[backends::ai::kCodexSecret] = backends::ai::sessionJson(old);
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    auth.script("ok");
    page.checkCodex();
    ASSERT_TRUE(waitFor([&] {
        return backends::ai::sessionFromJson(chain.keys.at(backends::ai::kCodexSecret))->refresh == "fake-refresh";
    }));
    EXPECT_TRUE(codex(page, "signedIn").toBool());

    chain.keys[backends::ai::kCodexSecret] = backends::ai::sessionJson(old);
    auth.script("expired");
    page.checkCodex();
    ASSERT_TRUE(waitFor([&] { return codex(page, "error").toString() == QLatin1String("expired"); }));
    EXPECT_FALSE(codex(page, "signedIn").toBool());
}

TEST_F(AiSettingsTest, SigningInToOpenRouterAddsTheProviderWithItsNewKey)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.openDraft();
    auth.script("hang");
    page.signInDraft(form(QStringLiteral("openrouter"), QStringLiteral("OpenRouter"), {}));
    EXPECT_EQ(draftState(page), QStringLiteral("signingIn"));
    ASSERT_TRUE(waitFor([&] { return !opened.empty(); }));
    EXPECT_EQ(opened[0].host(), QStringLiteral("openrouter.ai"));
    page.cancelSignIn();
    EXPECT_EQ(draftState(page), QString());
    EXPECT_TRUE(page.providers().isEmpty());

    auth.script("ok");
    QSignalSpy saved(&page, &app::AiSettings::draftSaved);
    opened.clear();
    page.signInDraft(form(QStringLiteral("openrouter"), QStringLiteral("OpenRouter"), {}));
    approveOpenRouter();
    ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
    EXPECT_EQ(chain.keys.at("openrouter"), "sk-or-v1-fake");
    EXPECT_FALSE(chain.keys.contains("openrouter-key"));
    EXPECT_TRUE(row(page, 0).value(QStringLiteral("signedIn")).toBool());
    EXPECT_TRUE(stored.providers.at(0).signedIn);
    EXPECT_TRUE(page.form(QStringLiteral("openrouter")).value(QStringLiteral("signedIn")).toBool());
    ASSERT_TRUE(waitFor([&] { return row(page, 0).value(QStringLiteral("state")).toString() == QLatin1String("ok"); }));
}

TEST_F(AiSettingsTest, ARefusedOpenRouterSignInSaysSoAndAddsNothing)
{
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.openDraft();
    auth.script("denied");
    page.signInDraft(form(QStringLiteral("openrouter"), QStringLiteral("OpenRouter"), {}));
    approveOpenRouter();
    ASSERT_TRUE(waitFor([&] { return draftState(page) == QLatin1String("failed"); }));
    EXPECT_EQ(draftError(page), QStringLiteral("signIn"));
    EXPECT_EQ(page.draft().value(QStringLiteral("detail")).toString(), QStringLiteral("denied"));
    EXPECT_TRUE(page.providers().isEmpty());
    EXPECT_TRUE(chain.keys.empty());

    page.signInDraft(form(QStringLiteral("anthropic"), QStringLiteral("Anthropic"), {}));
    EXPECT_EQ(draftError(page), QStringLiteral("form"));
}

TEST_F(AiSettingsTest, SigningOutOfOpenRouterDeletesTheKeyAndAPastedKeyIsNoLongerSignedIn)
{
    ai::Provider p = ai::presetProvider(ai::Preset::OpenRouter, "openrouter", "OpenRouter");
    p.signedIn = true;
    stored.providers.push_back(p);
    chain.keys["openrouter"] = "sk-or-v1-signed";
    app::AiFeatures features(settings, ports());
    app::AiSettings page(features);
    page.signOutProvider(QStringLiteral("openrouter"));
    EXPECT_FALSE(chain.keys.contains("openrouter"));
    EXPECT_FALSE(row(page, 0).value(QStringLiteral("signedIn")).toBool());
    EXPECT_EQ(row(page, 0).value(QStringLiteral("error")).toString(), QStringLiteral("key"));

    stored.providers[0].signedIn = true;
    app::AiFeatures again(settings, ports());
    app::AiSettings edited(again);
    QSignalSpy saved(&edited, &app::AiSettings::draftSaved);
    edited.saveDraft(form(QStringLiteral("openrouter"), QStringLiteral("OpenRouter"), QStringLiteral("sk-pasted"),
                          QStringLiteral("openrouter")));
    ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
    EXPECT_FALSE(stored.providers.at(0).signedIn);
}
