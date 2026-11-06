#include "hikari/backends/ai_signin.h"

#include "fake_auth_server.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTcpServer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <map>

using namespace hikari::backends::ai;
namespace app = hikari::application::ai;
namespace test = hikari::test;
using test::FakeAuthServer;
using test::jsonAnswer;

namespace {

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

QByteArray challengeOf(const QString &verifier)
{
    return QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
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

    void begin(SignInFlow flow)
    {
        outcome.reset();
        signIn.start(flow, [this](const SignInProgress &p) { progress.push_back(p); },
                     [this](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    }

    Outcome run(SignInFlow flow)
    {
        begin(flow);
        EXPECT_TRUE(waitFor([&] { return outcome.has_value(); }));
        return outcome;
    }

    SignInFailure failure(SignInFlow flow)
    {
        const Outcome o = run(flow);
        EXPECT_TRUE(o && !*o);
        return o && !*o ? o->error() : SignInFailure{};
    }

    QUrl browserPage()
    {
        EXPECT_TRUE(waitFor([&] { return !signIn.page().isEmpty(); }));
        return signIn.page();
    }

    std::optional<CodexSession> session() const
    {
        const auto it = keys.keys.find(kCodexSecret);
        return it == keys.keys.end() ? std::nullopt : sessionFromJson(it->second);
    }

    QTemporaryDir dir;
    Keys keys;
    FakeAuthServer issuer;
    quint16 browserPort = test::freePort();
    test::FakeSignInClock time;
    AiSignIn signIn{keys, issuer.endpoints(browserPort), time.clock()};
    std::vector<SignInProgress> progress;
    Outcome outcome;
};

}

TEST_F(AiSignInTest, ADeviceCodeSignInShowsTheCodePollsUntilItIsEnteredAndKeepsTheSession)
{
    issuer.on(FakeAuthServer::kUsercode, {jsonAnswer({{"device_auth_id", "dev-1"}, {"user_code", "ABCD-1234"}, {"interval", "5"}})})
        .on(FakeAuthServer::kPoll, {jsonAnswer({}, 403), jsonAnswer({}, 404),
                                    jsonAnswer({{"authorization_code", "auth-1"}, {"code_verifier", "verifier-1"}})})
        .on(FakeAuthServer::kToken, {FakeAuthServer::tokens()});
    const std::int64_t started = time.now;
    const Outcome o = run(SignInFlow::CodexDevice);
    ASSERT_TRUE(o && *o) << (o ? o->error().kind : "");
    EXPECT_EQ((*o)->email, "owner@example.com");
    EXPECT_TRUE((*o)->key.empty());

    EXPECT_EQ(issuer.at(FakeAuthServer::kUsercode).at(0).json(), QJsonObject({{"client_id", "app_test"}}));
    ASSERT_EQ(progress.size(), 1u);
    EXPECT_EQ(progress[0].step, "code");
    EXPECT_EQ(progress[0].userCode, "ABCD-1234");
    EXPECT_EQ(progress[0].openUrl, QUrl(QStringLiteral("https://auth.openai.com/codex/device")));
    EXPECT_EQ(progress[0].expiresAt, started + AiSignIn::kCodeLifetimeMs);
    EXPECT_TRUE(progress[0].browserAvailable);
    const auto polls = issuer.at(FakeAuthServer::kPoll);
    ASSERT_EQ(polls.size(), 3u);
    for (const auto &poll : polls)
        EXPECT_EQ(poll.json(), QJsonObject({{"device_auth_id", "dev-1"}, {"user_code", "ABCD-1234"}}));
    EXPECT_EQ(time.waits, (std::vector<int>{5000, 5000, 5000}));
    const QUrlQuery exchanged = issuer.at(FakeAuthServer::kToken).at(0).form();
    EXPECT_EQ(exchanged.queryItemValue("grant_type"), "authorization_code");
    EXPECT_EQ(exchanged.queryItemValue("code"), "auth-1");
    EXPECT_EQ(exchanged.queryItemValue("redirect_uri", QUrl::FullyDecoded), "https://auth.openai.com/deviceauth/callback");
    EXPECT_EQ(exchanged.queryItemValue("client_id"), "app_test");
    EXPECT_EQ(exchanged.queryItemValue("code_verifier"), "verifier-1");

    ASSERT_TRUE(session());
    EXPECT_EQ(session()->refresh, "fake-refresh");
    EXPECT_EQ(session()->accountId, "acct-fake");
    EXPECT_EQ(session()->email, "owner@example.com");
    EXPECT_EQ(session()->expiresAt, 4102444800000);
    EXPECT_TRUE(signIn.page().isEmpty());
}

TEST_F(AiSignInTest, PollsNeverComeFasterThanEveryThreeSeconds)
{
    issuer.script("ok");
    ASSERT_TRUE(*run(SignInFlow::CodexDevice));
    EXPECT_EQ(time.waits, (std::vector<int>{3000}));
}

TEST_F(AiSignInTest, A404OnTheCodeRequestMeansTheWorkspaceTurnsCodesOff)
{
    issuer.script("codesOff");
    const SignInFailure f = failure(SignInFlow::CodexDevice);
    EXPECT_EQ(f.kind, "codesOff");
    EXPECT_EQ(f.host, "auth.openai.com");
    EXPECT_TRUE(progress.empty());
}

TEST_F(AiSignInTest, ACodeNobodyEntersExpiresAfterFifteenMinutes)
{
    issuer.script("hang");
    issuer.on(FakeAuthServer::kUsercode, {jsonAnswer({{"device_auth_id", "d"}, {"user_code", "C"}, {"interval", 60}})});
    EXPECT_EQ(failure(SignInFlow::CodexDevice).kind, "codeExpired");
    EXPECT_EQ(issuer.at(FakeAuthServer::kPoll).size(), AiSignIn::kCodeLifetimeMs / 60'000 - 1);
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(AiSignInTest, ARefusedCodeAndARefusedExchangeAreDenials)
{
    issuer.script("denied");
    const SignInFailure refused = failure(SignInFlow::CodexDevice);
    EXPECT_EQ(refused.kind, "denied");
    EXPECT_EQ(refused.detail, "access_denied");

    issuer.script("ok");
    issuer.on(FakeAuthServer::kToken, {jsonAnswer({{"error", "invalid_grant"}}, 400)});
    EXPECT_EQ(failure(SignInFlow::CodexDevice).kind, "denied");
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(AiSignInTest, A429WaitsOutRetryAfterAndAFewServerErrorsWhilePollingAreTolerated)
{
    issuer.script("ok");
    issuer.on(FakeAuthServer::kUsercode, {jsonAnswer({}, 429, {{"Retry-After", "7"}}),
                                          jsonAnswer({{"device_auth_id", "d"}, {"user_code", "C"}})})
        .on(FakeAuthServer::kPoll, {jsonAnswer({}, 502), jsonAnswer({}, 503),
                                    jsonAnswer({{"authorization_code", "a"}, {"code_verifier", "v"}})});
    ASSERT_TRUE(*run(SignInFlow::CodexDevice));
    EXPECT_EQ(time.waits, (std::vector<int>{7000, 3000, 3000, 3000}));
    EXPECT_TRUE(session());
}

TEST_F(AiSignInTest, AnIssuerThatCantBeReachedNamesItsHost)
{
    issuer.stop();
    const SignInFailure f = failure(SignInFlow::CodexDevice);
    EXPECT_EQ(f.kind, "network");
    EXPECT_EQ(f.host, "auth.openai.com");
}

TEST_F(AiSignInTest, CancellingStopsThePollingEndsTheSignInOnceAndStoresNothing)
{
    issuer.script("hang");
    int calls = 0;
    std::string kind;
    signIn.start(SignInFlow::CodexDevice, [this](const SignInProgress &p) { progress.push_back(p); },
                 [&](std::expected<SignedIn, SignInFailure> r) {
                     ++calls;
                     kind = r ? "" : r.error().kind;
                 });
    ASSERT_TRUE(waitFor([&] { return !issuer.at(FakeAuthServer::kPoll).empty(); }));
    signIn.cancel();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(kind, "cancelled");
    EXPECT_FALSE(signIn.running());
    const std::size_t polled = issuer.at(FakeAuthServer::kPoll).size();
    waitFor([] { return false; }, 200);
    EXPECT_EQ(issuer.at(FakeAuthServer::kPoll).size(), polled);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(AiSignInTest, ABrowserSignInChecksTheStateThenExchangesTheCodeWithItsOwnVerifier)
{
    issuer.script("ok");
    begin(SignInFlow::CodexBrowser);
    const QUrl page = browserPage();
    const QUrlQuery query(page);
    EXPECT_EQ(page.host(), QStringLiteral("auth.openai.com"));
    EXPECT_EQ(page.path(), QStringLiteral("/oauth/authorize"));
    EXPECT_TRUE(AiSignIn::openable(page));
    EXPECT_EQ(query.queryItemValue("redirect_uri", QUrl::FullyDecoded),
              QStringLiteral("http://localhost:%1/auth/callback").arg(browserPort));
    EXPECT_EQ(query.queryItemValue("scope", QUrl::FullyDecoded), "openid profile email offline_access");
    EXPECT_EQ(query.queryItemValue("code_challenge_method"), "S256");
    ASSERT_EQ(progress.size(), 1u);
    EXPECT_EQ(progress[0].step, "browser");

    const QString callback = QStringLiteral("http://127.0.0.1:%1/auth/callback").arg(browserPort);
    EXPECT_EQ(test::browse(QUrl(callback + "x")), 404);
    QByteArray body;
    EXPECT_EQ(test::browse(test::approval(page, "auth-2"), &body), 200);
    EXPECT_TRUE(body.contains("You can close this tab"));
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    ASSERT_TRUE(*outcome) << outcome->error().kind;

    const QUrlQuery exchanged = issuer.at(FakeAuthServer::kToken).at(0).form();
    EXPECT_EQ(exchanged.queryItemValue("code"), "auth-2");
    EXPECT_EQ(challengeOf(exchanged.queryItemValue("code_verifier")), query.queryItemValue("code_challenge").toLatin1());
    EXPECT_EQ(session()->email, "owner@example.com");
    EXPECT_EQ(test::browse(QUrl(callback)), -1);
}

TEST_F(AiSignInTest, ACallbackWithTheWrongStateIsRefusedAndNothingIsStored)
{
    issuer.script("ok");
    begin(SignInFlow::CodexBrowser);
    test::browse(QUrl(QStringLiteral("http://127.0.0.1:%1/auth/callback?code=c&state=forged").arg(browserPort)));
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    EXPECT_EQ(outcome->error().kind, "invalid");
    EXPECT_TRUE(issuer.at(FakeAuthServer::kToken).empty());
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(AiSignInTest, ADeniedConsentPageComesBackAsADenial)
{
    begin(SignInFlow::CodexBrowser);
    browserPage();
    test::browse(QUrl(QStringLiteral("http://127.0.0.1:%1/auth/callback?error=access_denied").arg(browserPort)));
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    EXPECT_EQ(outcome->error().kind, "denied");
}

TEST_F(AiSignInTest, AnotherAppOnTheBrowserPortMakesTheBrowserSignInUnavailable)
{
    QTcpServer held;
    ASSERT_TRUE(held.listen(QHostAddress::LocalHost, browserPort));
    EXPECT_EQ(failure(SignInFlow::CodexBrowser).kind, "portBusy");
    issuer.script("ok");
    ASSERT_TRUE(*run(SignInFlow::CodexDevice));
    EXPECT_FALSE(progress.at(0).browserAvailable);
}

TEST_F(AiSignInTest, CancellingABrowserSignInClosesTheListener)
{
    begin(SignInFlow::CodexBrowser);
    browserPage();
    signIn.cancel();
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->error().kind, "cancelled");
    EXPECT_EQ(test::browse(QUrl(QStringLiteral("http://127.0.0.1:%1/auth/callback").arg(browserPort))), -1);
}

TEST_F(AiSignInTest, AnOpenRouterSignInListensOnARandomPathAndTradesTheCodeForAKey)
{
    issuer.script("ok");
    begin(SignInFlow::OpenRouter);
    const QUrl page = browserPage();
    EXPECT_EQ(page.host(), QStringLiteral("openrouter.ai"));
    EXPECT_EQ(page.path(), QStringLiteral("/auth"));
    EXPECT_TRUE(AiSignIn::openable(page));
    const QUrl callback(QUrlQuery(page).queryItemValue("callback_url", QUrl::FullyDecoded));
    EXPECT_EQ(callback.host(), QStringLiteral("127.0.0.1"));
    EXPECT_GT(callback.path().size(), 16);
    EXPECT_EQ(test::browse(QUrl(QStringLiteral("http://127.0.0.1:%1/?code=guess").arg(callback.port()))), 404);
    EXPECT_EQ(test::browse(test::approval(page, "or-code")), 200);
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    ASSERT_TRUE(*outcome) << outcome->error().kind;
    EXPECT_EQ((*outcome)->key, "sk-or-v1-fake");
    EXPECT_TRUE(keys.keys.empty());

    const QJsonObject body = issuer.at(FakeAuthServer::kKeys).at(0).json();
    EXPECT_EQ(body.value("code").toString(), "or-code");
    EXPECT_EQ(body.value("code_challenge_method").toString(), "S256");
    EXPECT_EQ(challengeOf(body.value("code_verifier").toString()), QUrlQuery(page).queryItemValue("code_challenge").toLatin1());
}

TEST_F(AiSignInTest, OpenRouterRefusingTheCodeIsADenial)
{
    issuer.script("denied");
    begin(SignInFlow::OpenRouter);
    test::browse(test::approval(browserPage()));
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    EXPECT_EQ(outcome->error().kind, "denied");
}

TEST_F(AiSignInTest, ARefreshTradesTheStoredRefreshTokenAndKeepsWhatTheIssuerDidntRotate)
{
    const CodexSession old{.access = "old", .refresh = "refresh-0", .idToken = "id-0", .accountId = "acct-0",
                           .email = "old@example.com", .expiresAt = 1};
    keys.keys[kCodexSecret] = sessionJson(old);
    const QByteArray access = test::jwt({{"exp", 3'000'000}});
    issuer.on(FakeAuthServer::kToken, {jsonAnswer({{"access_token", QString::fromLatin1(access)}})});
    signIn.refreshCodex([this](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    ASSERT_TRUE(*outcome) << outcome->error().kind;

    const QUrlQuery sent = issuer.at(FakeAuthServer::kToken).at(0).form();
    EXPECT_EQ(sent.queryItemValue("grant_type"), "refresh_token");
    EXPECT_EQ(sent.queryItemValue("refresh_token"), "refresh-0");
    EXPECT_EQ(sent.queryItemValue("client_id"), "app_test");
    CodexSession renewed = old;
    renewed.access = access.toStdString();
    renewed.expiresAt = 3'000'000'000;
    EXPECT_EQ(session(), renewed);
}

TEST_F(AiSignInTest, ARefusedRefreshMeansSignInAgain)
{
    keys.keys[kCodexSecret] = sessionJson({.access = "a", .refresh = "r"});
    issuer.script("expired");
    signIn.refreshCodex([this](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    EXPECT_EQ(outcome->error().kind, "expired");
    EXPECT_EQ(session()->access, "a");
}

TEST_F(AiSignInTest, ARefreshWithoutASessionMeansSignInAgain)
{
    signIn.refreshCodex([this](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_EQ(outcome->error().kind, "expired");
    EXPECT_TRUE(issuer.seen.empty());
}

TEST_F(AiSignInTest, AKeychainThatWontWriteFailsTheSignIn)
{
    keys.writeError = app::KeyError::Denied;
    issuer.script("ok");
    const SignInFailure f = failure(SignInFlow::CodexDevice);
    EXPECT_EQ(f.kind, "keychain");
    EXPECT_EQ(f.detail, "denied");
}

TEST(AiSignIn, OnlyHttpsPagesOnTheTwoSignInHostsOpen)
{
    for (const char *url : {"https://auth.openai.com/codex/device", "https://openrouter.ai/auth?callback_url=x"})
        EXPECT_TRUE(AiSignIn::openable(QUrl(QString::fromLatin1(url)))) << url;
    for (const char *url : {"http://auth.openai.com/codex/device", "https://auth.openai.com.example.com/",
                            "https://example.com/?https://auth.openai.com", "https://me@auth.openai.com/",
                            "https://auth.openai.com:8443/", "file:///etc/passwd", "https://chat.openai.com/",
                            "http://127.0.0.1:1455/auth/callback"})
        EXPECT_FALSE(AiSignIn::openable(QUrl(QString::fromLatin1(url)))) << url;
}

TEST_F(AiSignInTest, ImportCopiesTheCodexSignInAndNeverWritesItsFile)
{
    const QString path = dir.filePath(QStringLiteral("codex/auth.json"));
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QByteArray contents =
        R"({"OPENAI_API_KEY":null,"tokens":{"id_token":")" +
        test::jwt({{"email", "owner@example.com"}, {"https://api.openai.com/auth", QJsonObject{{"chatgpt_account_id", "acct-cli"}}}}) +
        R"(","access_token":")" + test::jwt({{"exp", 2000000}}) + R"(","refresh_token":"cli-refresh"},"last_refresh":"2026-11-01T00:00:00Z"})";
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
    QFile::setPermissions(path, QFile::ReadOwner);
    const QDateTime modified = QFileInfo(path).lastModified();

    signIn.importCodex(path, [this](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(outcome && *outcome);
    EXPECT_EQ((*outcome)->email, "owner@example.com");
    ASSERT_TRUE(session());
    EXPECT_EQ(session()->refresh, "cli-refresh");
    EXPECT_EQ(session()->accountId, "acct-cli");
    EXPECT_EQ(session()->expiresAt, 2'000'000'000);

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), contents);
    EXPECT_EQ(QFileInfo(path).lastModified(), modified);
    EXPECT_TRUE(issuer.seen.empty());
}

TEST_F(AiSignInTest, ImportWithoutACodexSignInStoresNothing)
{
    signIn.importCodex(dir.filePath(QStringLiteral("missing/auth.json")),
                       [this](std::expected<SignedIn, SignInFailure> r) { outcome = std::move(r); });
    ASSERT_TRUE(outcome && !*outcome);
    EXPECT_EQ(outcome->error().kind, "import");
    EXPECT_TRUE(keys.keys.empty());
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
