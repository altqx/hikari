#include "hikari/backends/ai_signin.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <map>

using namespace hikari::backends::ai;
namespace app = hikari::application::ai;
using hikari::api::Json;

namespace {

constexpr const char *kBuildId = "0.1.0+test";

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        if (done())
            return true;
        if (t.elapsed() >= ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

struct Keys final : app::KeyStore {
    std::map<std::string, std::string> keys;
    std::optional<app::KeyError> writeError;

    void read(const std::string &id, std::function<void(app::KeyResult)> done) override
    {
        if (const auto it = keys.find(id); it != keys.end())
            done(it->second);
        else
            done(app::KeyError::NotFound);
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<app::KeyError>)> done) override
    {
        if (!writeError)
            keys[id] = key;
        done(writeError);
    }
    void remove(const std::string &id, std::function<void(std::optional<app::KeyError>)> done) override
    {
        done(keys.erase(id) ? std::nullopt : std::optional(app::KeyError::NotFound));
    }
};

QByteArray jwt(const QByteArray &claims)
{
    return "e30." + claims.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) + ".sig";
}

using Outcome = std::optional<std::expected<SignedIn, SignInFailure>>;

class AiSignInTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_signin_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    AiSidecarHost *sidecar()
    {
        if (!host) {
            AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {QStringLiteral("--build-id"), QString::fromLatin1(kBuildId), QStringLiteral("--api-version"),
                           QString::number(hikari::api::kApiVersion)};
            o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
            o.buildId = kBuildId;
            o.shutdownMs = 600;
            host = std::make_unique<AiSidecarHost>(std::move(o), nullptr, hikari::api::Caller{"ai", "AI"});
        }
        return host.get();
    }

    Json ask(const std::string &method, Json params = {})
    {
        std::optional<SidecarResult> result;
        sidecar()->request(method, std::move(params), [&](SidecarResult r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result && *result ? **result : Json();
    }

    Outcome run(SignInFlow flow)
    {
        Outcome outcome;
        signIn.start(flow, [this](const SignInProgress &p) { progress.push_back(p); },
                     [&](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return outcome.has_value(); }));
        return outcome;
    }

    QTemporaryDir dir;
    Keys keys;
    std::unique_ptr<AiSidecarHost> host;
    AiSignIn signIn{keys, [this] { return sidecar(); }};
    std::vector<SignInProgress> progress;
};

}

TEST_F(AiSignInTest, ACodexDeviceSignInShowsTheCodeAndKeepsTheSessionInTheKeychain)
{
    const Outcome outcome = run(SignInFlow::CodexDevice);
    ASSERT_TRUE(outcome && *outcome) << (outcome ? outcome->error().kind : "");
    EXPECT_EQ((*outcome)->email, "owner@example.com");
    EXPECT_TRUE((*outcome)->key.empty());

    ASSERT_EQ(progress.size(), 1u);
    EXPECT_EQ(progress[0].step, "code");
    EXPECT_EQ(progress[0].userCode, "FAKE-1234");
    EXPECT_EQ(progress[0].openUrl, QUrl(QStringLiteral("https://auth.openai.com/codex/device")));
    EXPECT_GT(progress[0].expiresAt, 0);

    const auto session = sessionFromJson(keys.keys.at(kCodexSecret));
    ASSERT_TRUE(session);
    EXPECT_EQ(session->access, "fake-access");
    EXPECT_EQ(session->accountId, "acct-fake");
    const Json sent = ask("received");
    ASSERT_GE(sent.asArray().size(), 1u);
    EXPECT_EQ(*sent.asArray()[0].find("params"), Json::object({{"flow", "codexDevice"}, {"secret", kCodexSecret}}));
}

TEST_F(AiSignInTest, AnOpenRouterSignInHandsTheKeyBackForTheNewProvider)
{
    const Outcome outcome = run(SignInFlow::OpenRouter);
    ASSERT_TRUE(outcome && *outcome);
    EXPECT_EQ((*outcome)->key, "sk-or-v1-fake");
    EXPECT_TRUE(keys.keys.empty());
    ASSERT_EQ(progress.size(), 1u);
    EXPECT_EQ(progress[0].openUrl.host(), QStringLiteral("openrouter.ai"));
}

TEST_F(AiSignInTest, ASecretUnderAnotherNameIsRefusedAndNothingIsStored)
{
    ask("fake.signin", "store-other");
    const Outcome outcome = run(SignInFlow::CodexDevice);
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(AiSignInTest, ASidecarFailureKeepsItsKindAndHost)
{
    ask("fake.signin", "codesOff");
    const Outcome outcome = run(SignInFlow::CodexDevice);
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_EQ(outcome->error().kind, "codesOff");
    EXPECT_EQ(outcome->error().host, "auth.openai.com");
}

TEST_F(AiSignInTest, AKeychainThatWontWriteFailsTheSignIn)
{
    keys.writeError = app::KeyError::Denied;
    const Outcome outcome = run(SignInFlow::CodexBrowser);
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_EQ(outcome->error().kind, "keychain");
}

TEST_F(AiSignInTest, CancellingEndsTheSignInOnce)
{
    ask("fake.signin", "hang");
    int calls = 0;
    std::string kind;
    signIn.start(SignInFlow::CodexDevice, [this](const SignInProgress &p) { progress.push_back(p); },
                 [&](std::expected<SignedIn, SignInFailure> r) {
                     ++calls;
                     kind = r ? "" : r.error().kind;
                 });
    ASSERT_TRUE(waitFor([&] { return !progress.empty(); }));
    signIn.cancel();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(kind, "cancelled");
    EXPECT_FALSE(signIn.running());
    ask("echo");
    EXPECT_EQ(calls, 1);
}

TEST_F(AiSignInTest, WithoutASidecarTheSignInFailsAtOnce)
{
    AiSignIn none(keys, [] { return nullptr; });
    Outcome outcome;
    none.start(SignInFlow::CodexDevice, {}, [&](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_EQ(outcome->error().kind, "sidecar");
}

TEST(AiSignIn, OnlyHttpsPagesOnTheTwoSignInHostsOpen)
{
    for (const char *url : {"https://auth.openai.com/codex/device", "https://openrouter.ai/auth?callback_url=x"})
        EXPECT_TRUE(AiSignIn::openable(QUrl(QString::fromLatin1(url)))) << url;
    for (const char *url : {"http://auth.openai.com/codex/device", "https://auth.openai.com.example.com/",
                            "https://example.com/?https://auth.openai.com", "https://me@auth.openai.com/",
                            "https://auth.openai.com:8443/", "file:///etc/passwd", "https://chat.openai.com/"})
        EXPECT_FALSE(AiSignIn::openable(QUrl(QString::fromLatin1(url)))) << url;
}

TEST_F(AiSignInTest, ImportCopiesTheCodexSignInAndNeverWritesItsFile)
{
    const QString path = dir.filePath(QStringLiteral("codex/auth.json"));
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QByteArray contents =
        R"({"OPENAI_API_KEY":null,"tokens":{"id_token":")" +
        jwt(R"({"email":"owner@example.com","https://api.openai.com/auth":{"chatgpt_account_id":"acct-cli"}})") +
        R"(","access_token":")" + jwt(R"({"exp":2000000})") + R"(","refresh_token":"cli-refresh"},"last_refresh":"2026-11-01T00:00:00Z"})";
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
    QFile::setPermissions(path, QFile::ReadOwner);
    const QDateTime modified = QFileInfo(path).lastModified();

    Outcome outcome;
    signIn.importCodex(path, [&](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(outcome && *outcome);
    EXPECT_EQ((*outcome)->email, "owner@example.com");
    const auto session = sessionFromJson(keys.keys.at(kCodexSecret));
    ASSERT_TRUE(session);
    EXPECT_EQ(session->refresh, "cli-refresh");
    EXPECT_EQ(session->accountId, "acct-cli");
    EXPECT_EQ(session->expiresAt, 2'000'000'000);

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), contents);
    EXPECT_EQ(QFileInfo(path).lastModified(), modified);
    EXPECT_FALSE(host);
}

TEST_F(AiSignInTest, ImportWithoutACodexSignInStoresNothing)
{
    Outcome outcome;
    signIn.importCodex(dir.filePath(QStringLiteral("missing/auth.json")),
                       [&](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_EQ(outcome->error().kind, "import");
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(AiSignInTest, ARefreshSendsTheStoredSessionAndKeepsTheNewOne)
{
    const CodexSession old{.access = "old", .refresh = "old-refresh", .idToken = "id", .accountId = "acct",
                           .email = "owner@example.com", .expiresAt = 1};
    keys.keys[kCodexSecret] = sessionJson(old);
    Outcome outcome;
    signIn.refreshCodex([&](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    ASSERT_TRUE(*outcome);
    EXPECT_EQ(sessionFromJson(keys.keys.at(kCodexSecret))->access, "fake-access-2");
    const Json sent = ask("received");
    EXPECT_EQ(*sent.asArray()[0].find("params"), Json::object({{"secret", kCodexSecret}, {"session", sessionObject(old)}}));
}

TEST_F(AiSignInTest, ARefreshWithoutASessionMeansSignInAgain)
{
    Outcome outcome;
    signIn.refreshCodex([&](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_EQ(outcome->error().kind, "expired");
}

TEST_F(AiSignInTest, SigningOutDeletesOnlyHikariSubsSession)
{
    keys.keys[kCodexSecret] = "{}";
    keys.keys["openrouter"] = "sk-or-v1-kept";
    std::optional<std::optional<app::KeyError>> result;
    signIn.signOutCodex([&](std::optional<app::KeyError> e) { result = e; });
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, std::nullopt);
    EXPECT_FALSE(keys.keys.contains(kCodexSecret));
    EXPECT_EQ(keys.keys.at("openrouter"), "sk-or-v1-kept");
    signIn.signOutCodex([&](std::optional<app::KeyError> e) { result = e; });
    EXPECT_EQ(*result, std::nullopt);
}
