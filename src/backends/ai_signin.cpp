#include "hikari/backends/ai_signin.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;
using api::Json;

namespace {

constexpr int kMinPollMs = 3'000;
constexpr int kTransientLimit = 6;
constexpr qsizetype kMaxCallbackBytes = 16 * 1024;

constexpr const char kDonePage[] =
    "<!doctype html><meta charset=\"utf-8\"><title>HikariSub</title>\n"
    "<p style=\"font:16px system-ui;margin:3em\">You're signed in. You can close this tab and go back to "
    "HikariSub.</p>";

class SystemProxies final : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override { return systemProxyForQuery(query); }
};

std::string stringAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

std::int64_t intAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isNumber() ? static_cast<std::int64_t>(value->asNumber()) : 0;
}

QJsonObject claimsOf(const QString &jwt)
{
    const QStringList parts = jwt.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return {};
    return QJsonDocument::fromJson(QByteArray::fromBase64(parts[1].toLatin1(), QByteArray::Base64UrlEncoding |
                                                                                    QByteArray::OmitTrailingEquals))
        .object();
}

QByteArray base64url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray randomToken(int bytes = 32)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), bytes / 4);
    return base64url(raw);
}

QByteArray challengeOf(const QByteArray &verifier)
{
    return base64url(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256));
}

std::string hostOf(const QUrl &url)
{
    const QString host = url.port() == -1 ? url.host() : url.host() + QLatin1Char(':') + QString::number(url.port());
    return host.toStdString();
}

QUrl under(const QUrl &base, const QString &path)
{
    QUrl url = base;
    url.setPath(base.path() + path);
    return url;
}

QByteArray form(std::initializer_list<std::pair<QString, QString>> fields)
{
    QUrlQuery query;
    for (const auto &[name, value] : fields)
        query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(name)),
                           QString::fromLatin1(QUrl::toPercentEncoding(value)));
    return query.query(QUrl::FullyEncoded).toLatin1();
}

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::string keyErrorDetail(app::KeyError error)
{
    switch (error) {
    case app::KeyError::Unavailable:
        return "unavailable";
    case app::KeyError::Denied:
        return "denied";
    case app::KeyError::NotFound:
        return "notFound";
    case app::KeyError::Other:
        return "other";
    }
    return "other";
}

}

Json sessionObject(const CodexSession &s)
{
    return Json::object({{"access", s.access}, {"refresh", s.refresh}, {"idToken", s.idToken},
                         {"accountId", s.accountId}, {"email", s.email}, {"expiresAt", s.expiresAt}});
}

std::string sessionJson(const CodexSession &s)
{
    return sessionObject(s).dump();
}

std::optional<CodexSession> sessionFromJson(std::string_view json)
{
    const auto parsed = Json::parse(json);
    if (!parsed || !parsed->isObject())
        return std::nullopt;
    CodexSession s{.access = stringAt(*parsed, "access"), .refresh = stringAt(*parsed, "refresh"),
                   .idToken = stringAt(*parsed, "idToken"), .accountId = stringAt(*parsed, "accountId"),
                   .email = stringAt(*parsed, "email"), .expiresAt = intAt(*parsed, "expiresAt")};
    if (s.access.empty() || s.refresh.empty())
        return std::nullopt;
    return s;
}

namespace {

CodexSession sessionOf(const QString &access, const QString &refresh, const QString &idToken, QString accountId)
{
    const QJsonObject identity = claimsOf(idToken);
    if (accountId.isEmpty())
        accountId = identity.value(QStringLiteral("https://api.openai.com/auth"))
                        .toObject()
                        .value(QStringLiteral("chatgpt_account_id"))
                        .toString();
    return {.access = access.toStdString(),
            .refresh = refresh.toStdString(),
            .idToken = idToken.toStdString(),
            .accountId = accountId.toStdString(),
            .email = identity.value(QStringLiteral("email")).toString().toStdString(),
            .expiresAt = static_cast<std::int64_t>(claimsOf(access).value(QStringLiteral("exp")).toDouble()) * 1000};
}

}

std::optional<CodexSession> sessionFromCodexAuth(const QString &authJsonPath)
{
    QFile file(authJsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject tokens = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("tokens")).toObject();
    CodexSession s = sessionOf(tokens.value(QStringLiteral("access_token")).toString(),
                               tokens.value(QStringLiteral("refresh_token")).toString(),
                               tokens.value(QStringLiteral("id_token")).toString(),
                               tokens.value(QStringLiteral("account_id")).toString());
    if (s.access.empty() || s.refresh.empty())
        return std::nullopt;
    return s;
}

SignInClock SignInClock::system()
{
    return {.now = [] { return QDateTime::currentMSecsSinceEpoch(); },
            .wait = [](int ms, std::function<void()> then) { QTimer::singleShot(ms, std::move(then)); }};
}

/// The browser's redirect back to HikariSub: listens on 127.0.0.1 only, and only until the first callback.
class AiSignIn::Loopback {
public:
    Loopback(QString path, std::function<void(QUrlQuery)> callback)
        : m_path(std::move(path)), m_callback(std::move(callback))
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] { accept(); });
    }

    bool listen(quint16 port) { return m_server.listen(QHostAddress::LocalHost, port); }
    quint16 port() const { return m_server.serverPort(); }
    QString path() const { return m_path; }

    static bool isFree(quint16 port)
    {
        QTcpServer probe;
        return probe.listen(QHostAddress::LocalHost, port);
    }

private:
    void accept()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            socket->setParent(nullptr);
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, alive = std::weak_ptr(m_alive), socket] {
                if (!alive.lock()) {
                    socket->abort();
                    return;
                }
                if (!socket->canReadLine() && socket->bytesAvailable() < kMaxCallbackBytes)
                    return;
                QObject::disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
                const QList<QByteArray> line = socket->readLine(kMaxCallbackBytes).trimmed().split(' ');
                const QUrl target(QString::fromLatin1(line.value(1)));
                const bool ours = line.value(0) == "GET" && target.path() == m_path;
                const QByteArray page = ours ? QByteArray(kDonePage) : QByteArray("Not found");
                socket->write(QByteArray(ours ? "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                                              : "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n") +
                              "Content-Length: " + QByteArray::number(page.size()) + "\r\nConnection: close\r\n\r\n" +
                              page);
                socket->disconnectFromHost();
                if (ours && m_server.isListening()) {
                    m_server.close();
                    const auto callback = m_callback;
                    callback(QUrlQuery(target));
                }
            });
        }
    }

    QTcpServer m_server;
    QString m_path;
    std::function<void(QUrlQuery)> m_callback;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

struct AiSignIn::Reply {
    bool reached = false;
    int status = 0;
    QByteArray body;
    int retryAfterSeconds = 0;
    QString error;

    bool ok() const { return status >= 200 && status < 300; }
    QJsonObject object() const { return QJsonDocument::fromJson(body).object(); }
};

struct AiSignIn::Running {
    std::uint64_t serial = 0;
    Progress progress;
    Done done;
    QUrl page;
    QPointer<QNetworkReply> reply;
    std::unique_ptr<Loopback> loopback;
    QTimer timeout;
    QByteArray deviceAuthId;
    QByteArray userCode;
    std::int64_t expiresAt = 0;
    int intervalMs = kMinPollMs;
    int transients = 0;

    ~Running()
    {
        if (reply)
            reply->abort();
    }
};

AiSignIn::AiSignIn(app::KeyStore &keys, SignInEndpoints endpoints, SignInClock clock)
    : m_keys(keys), m_endpoints(std::move(endpoints)), m_clock(std::move(clock))
{
    m_nam.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    m_nam.setProxyFactory(new SystemProxies);
}

AiSignIn::~AiSignIn()
{
    *m_alive = false;
}

bool AiSignIn::openable(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") && url.userInfo().isEmpty() && url.port() == -1 &&
           (url.host() == QLatin1String("auth.openai.com") || url.host() == QLatin1String("openrouter.ai"));
}

QUrl AiSignIn::page() const
{
    return m_running ? m_running->page : QUrl();
}

std::uint64_t AiSignIn::begin(Progress progress, Done done)
{
    if (m_running)
        cancel();
    m_running = std::make_unique<Running>();
    m_running->serial = ++m_serial;
    m_running->progress = std::move(progress);
    m_running->done = std::move(done);
    m_running->timeout.setSingleShot(true);
    return m_running->serial;
}

bool AiSignIn::live(std::uint64_t serial) const
{
    return m_running && m_running->serial == serial;
}

void AiSignIn::report(SignInProgress progress)
{
    m_running->page = progress.openUrl;
    if (m_running->progress)
        m_running->progress(progress);
}

void AiSignIn::start(SignInFlow flow, Progress progress, Done done)
{
    const std::uint64_t serial = begin(std::move(progress), std::move(done));
    switch (flow) {
    case SignInFlow::CodexDevice:
        return requestCode(serial, m_clock.now());
    case SignInFlow::CodexBrowser:
        return codexBrowser(serial);
    case SignInFlow::OpenRouter:
        return openRouter(serial);
    }
}

void AiSignIn::cancel()
{
    if (m_running)
        finish(std::unexpected(SignInFailure{.kind = "cancelled"}));
}

void AiSignIn::post(std::uint64_t serial, const QUrl &url, const QByteArray &contentType, QByteArray body, Then then)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    QNetworkReply *reply = m_nam.post(request, body);
    m_running->reply = reply;
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, alive = std::weak_ptr(m_alive), serial, reply, then = std::move(then)] {
                         reply->deleteLater();
                         if (!alive.lock() || !live(serial))
                             return;
                         const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                         Reply answer{.reached = status.isValid(),
                                      .status = status.toInt(),
                                      .body = reply->readAll(),
                                      .retryAfterSeconds = reply->rawHeader("Retry-After").toInt(),
                                      .error = reply->errorString()};
                         then(std::move(answer));
                     });
}

void AiSignIn::wait(std::uint64_t serial, int ms, std::function<void()> then)
{
    m_clock.wait(ms, [this, alive = std::weak_ptr(m_alive), serial, then = std::move(then)] {
        if (alive.lock() && live(serial))
            then();
    });
}

void AiSignIn::fail(std::string kind, std::string detail, std::string host)
{
    finish(std::unexpected(SignInFailure{.kind = std::move(kind), .host = std::move(host), .detail = std::move(detail)}));
}

void AiSignIn::finish(std::expected<SignedIn, SignInFailure> result)
{
    const std::unique_ptr<Running> running = std::move(m_running);
    if (running && running->done)
        running->done(std::move(result));
}

namespace {

/// What a refused request means: a server error is the network's, an expired code is codeExpired, the rest denied.
SignInFailure refusal(int status, const QByteArray &body, const std::string &host)
{
    const QJsonValue error = QJsonDocument::fromJson(body).object().value(QStringLiteral("error"));
    const QString code = error.isString() ? error.toString() : error.toObject().value(QStringLiteral("code")).toString();
    if (status >= 500)
        return {.kind = "network", .host = host, .detail = "HTTP " + std::to_string(status)};
    if (code == QLatin1String("expired_token"))
        return {.kind = "codeExpired", .host = host, .detail = code.toStdString()};
    return {.kind = "denied", .host = host,
            .detail = code.isEmpty() ? "HTTP " + std::to_string(status) : code.toStdString()};
}

}


bool AiSignIn::listen(std::uint64_t serial, quint16 port, QString path, std::function<void(QUrlQuery)> callback)
{
    auto loopback = std::make_unique<Loopback>(std::move(path), [this, alive = std::weak_ptr(m_alive), serial,
                                                                 callback = std::move(callback)](QUrlQuery query) {
        if (!alive.lock() || !live(serial))
            return;
        m_running->timeout.stop();
        callback(std::move(query));
    });
    if (!loopback->listen(port))
        return false;
    m_running->loopback = std::move(loopback);
    QObject::connect(&m_running->timeout, &QTimer::timeout, &m_running->timeout,
                     [this, alive = std::weak_ptr(m_alive), serial] {
                         if (alive.lock() && live(serial))
                             fail("timedOut");
                     });
    m_running->timeout.start(kBrowserWaitMs);
    return true;
}

void AiSignIn::requestCode(std::uint64_t serial, std::int64_t started)
{
    const std::string host = hostOf(m_endpoints.codexIssuer);
    post(serial, under(m_endpoints.codexApi, QStringLiteral("/api/accounts/deviceauth/usercode")),
         "application/json", json({{QStringLiteral("client_id"), m_endpoints.codexClientId}}),
         [this, serial, started, host](Reply reply) {
             if (!reply.reached)
                 return fail("network", reply.error.toStdString(), host);
             if (reply.status == 429)
                 return wait(serial, (reply.retryAfterSeconds > 0 ? std::min(reply.retryAfterSeconds, 60) : 5) * 1000,
                             [this, serial, started] { requestCode(serial, started); });
             if (reply.status == 404)
                 return fail("codesOff", {}, host);
             if (!reply.ok())
                 return finish(std::unexpected(refusal(reply.status, reply.body, host)));
             const QJsonObject issued = reply.object();
             Running &r = *m_running;
             r.deviceAuthId = issued.value(QStringLiteral("device_auth_id")).toString().toUtf8();
             r.userCode = issued.value(QStringLiteral("user_code")).toString().toUtf8();
             if (r.deviceAuthId.isEmpty() || r.userCode.isEmpty())
                 return fail("invalid", "usercode reply without a code", host);
             r.expiresAt = started + kCodeLifetimeMs;
             const QJsonValue interval = issued.value(QStringLiteral("interval"));
             const double seconds = interval.isString() ? interval.toString().toDouble() : interval.toDouble();
             r.intervalMs = std::max(kMinPollMs, static_cast<int>(seconds * 1000));
             report({.step = "code",
                     .userCode = r.userCode.toStdString(),
                     .openUrl = under(m_endpoints.codexIssuer, QStringLiteral("/codex/device")),
                     .expiresAt = r.expiresAt,
                     .browserAvailable = Loopback::isFree(m_endpoints.codexBrowserPort)});
             if (live(serial))
                 poll(serial);
         });
}

void AiSignIn::poll(std::uint64_t serial)
{
    wait(serial, m_running->intervalMs, [this, serial] {
        if (m_clock.now() >= m_running->expiresAt)
            return fail("codeExpired", {}, hostOf(m_endpoints.codexIssuer));
        askToken(serial);
    });
}

void AiSignIn::askToken(std::uint64_t serial)
{
    const std::string host = hostOf(m_endpoints.codexIssuer);
    const QJsonObject body{{QStringLiteral("device_auth_id"), QString::fromUtf8(m_running->deviceAuthId)},
                           {QStringLiteral("user_code"), QString::fromUtf8(m_running->userCode)}};
    post(serial, under(m_endpoints.codexApi, QStringLiteral("/api/accounts/deviceauth/token")), "application/json",
         json(body), [this, serial, host](Reply reply) {
             Running &r = *m_running;
             if (!reply.reached)
                 return ++r.transients < kTransientLimit ? poll(serial) : fail("network", reply.error.toStdString(), host);
             if (reply.status == 429)
                 return wait(serial, (reply.retryAfterSeconds > 0 ? std::min(reply.retryAfterSeconds, 60) : 5) * 1000,
                             [this, serial] { askToken(serial); });
             if (reply.status == 403 || reply.status == 404)
                 return poll(serial);
             if (reply.status >= 500 && ++r.transients < kTransientLimit)
                 return poll(serial);
             if (!reply.ok())
                 return finish(std::unexpected(refusal(reply.status, reply.body, host)));
             const QJsonObject grant = reply.object();
             const QString code = grant.value(QStringLiteral("authorization_code")).toString();
             const QString verifier = grant.value(QStringLiteral("code_verifier")).toString();
             if (code.isEmpty() || verifier.isEmpty())
                 return fail("invalid", "device grant without a code", host);
             exchange(serial, form({{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
                                    {QStringLiteral("code"), code},
                                    {QStringLiteral("redirect_uri"),
                                     under(m_endpoints.codexIssuer, QStringLiteral("/deviceauth/callback")).toString()},
                                    {QStringLiteral("client_id"), m_endpoints.codexClientId},
                                    {QStringLiteral("code_verifier"), verifier}}));
         });
}

void AiSignIn::codexBrowser(std::uint64_t serial)
{
    const std::string host = hostOf(m_endpoints.codexIssuer);
    const QByteArray verifier = randomToken();
    const QByteArray state = randomToken(16);
    const quint16 port = m_endpoints.codexBrowserPort;
    const QString redirect = QStringLiteral("http://localhost:%1/auth/callback").arg(port);
    const bool listening = listen(serial, port, QStringLiteral("/auth/callback"),
                                  [this, serial, host, verifier, state, redirect](const QUrlQuery &callback) {
                                      if (callback.hasQueryItem(QStringLiteral("error")))
                                          return fail("denied", callback.queryItemValue(QStringLiteral("error")).toStdString(), host);
                                      if (callback.queryItemValue(QStringLiteral("state")).toLatin1() != state)
                                          return fail("invalid", "state mismatch", host);
                                      const QString code = callback.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
                                      if (code.isEmpty())
                                          return fail("invalid", "callback without a code", host);
                                      exchange(serial, form({{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
                                                             {QStringLiteral("code"), code},
                                                             {QStringLiteral("redirect_uri"), redirect},
                                                             {QStringLiteral("client_id"), m_endpoints.codexClientId},
                                                             {QStringLiteral("code_verifier"), QString::fromLatin1(verifier)}}));
                                  });
    if (!listening)
        return fail("portBusy", "port " + std::to_string(port) + " is in use");
    QUrl authorize = under(m_endpoints.codexIssuer, QStringLiteral("/oauth/authorize"));
    QUrlQuery query;
    query.setQueryItems({{QStringLiteral("response_type"), QStringLiteral("code")},
                         {QStringLiteral("client_id"), m_endpoints.codexClientId},
                         {QStringLiteral("redirect_uri"), QString::fromLatin1(QUrl::toPercentEncoding(redirect))},
                         {QStringLiteral("scope"), QStringLiteral("openid%20profile%20email%20offline_access")},
                         {QStringLiteral("code_challenge"), QString::fromLatin1(challengeOf(verifier))},
                         {QStringLiteral("code_challenge_method"), QStringLiteral("S256")},
                         {QStringLiteral("id_token_add_organizations"), QStringLiteral("true")},
                         {QStringLiteral("state"), QString::fromLatin1(state)}});
    authorize.setQuery(query);
    report({.step = "browser", .openUrl = authorize});
}

void AiSignIn::openRouter(std::uint64_t serial)
{
    const std::string host = hostOf(m_endpoints.openRouter);
    const QByteArray verifier = randomToken();
    const bool listening = listen(serial, 0, QLatin1Char('/') + QString::fromLatin1(randomToken(16)),
                                  [this, serial, host, verifier](const QUrlQuery &callback) {
                                      const QString code = callback.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
                                      if (code.isEmpty())
                                          return fail("denied", "callback without a code", host);
                                      const QJsonObject body{{QStringLiteral("code"), code},
                                                             {QStringLiteral("code_verifier"), QString::fromLatin1(verifier)},
                                                             {QStringLiteral("code_challenge_method"), QStringLiteral("S256")}};
                                      post(serial, under(m_endpoints.openRouterApi, QStringLiteral("/api/v1/auth/keys")),
                                           "application/json", json(body), [this, host](Reply reply) {
                                               if (!reply.reached)
                                                   return fail("network", reply.error.toStdString(), host);
                                               if (reply.status >= 500)
                                                   return fail("network", "HTTP " + std::to_string(reply.status), host);
                                               if (!reply.ok())
                                                   return fail("denied", "HTTP " + std::to_string(reply.status), host);
                                               const QString key = reply.object().value(QStringLiteral("key")).toString();
                                               if (key.isEmpty())
                                                   return fail("invalid", "keys reply without a key", host);
                                               finish(SignedIn{.email = {}, .key = key.toStdString()});
                                           });
                                  });
    if (!listening)
        return fail("portBusy", "no free port on 127.0.0.1");
    const Loopback &loopback = *m_running->loopback;
    QUrl auth = under(m_endpoints.openRouter, QStringLiteral("/auth"));
    QUrlQuery query;
    const QString callbackUrl = QStringLiteral("http://127.0.0.1:%1%2").arg(loopback.port()).arg(loopback.path());
    query.setQueryItems({{QStringLiteral("callback_url"), QString::fromLatin1(QUrl::toPercentEncoding(callbackUrl))},
                         {QStringLiteral("code_challenge"), QString::fromLatin1(challengeOf(verifier))},
                         {QStringLiteral("code_challenge_method"), QStringLiteral("S256")}});
    auth.setQuery(query);
    report({.step = "browser", .openUrl = auth});
}

void AiSignIn::exchange(std::uint64_t serial, QByteArray body, std::optional<CodexSession> refreshed)
{
    const std::string host = hostOf(m_endpoints.codexIssuer);
    post(serial, under(m_endpoints.codexApi, QStringLiteral("/oauth/token")), "application/x-www-form-urlencoded",
         std::move(body), [this, serial, host, refreshed = std::move(refreshed)](Reply reply) {
             if (!reply.reached)
                 return fail("network", reply.error.toStdString(), host);
             if (!reply.ok()) {
                 SignInFailure refused = refusal(reply.status, reply.body, host);
                 if (refreshed && reply.status >= 400 && reply.status < 500)
                     refused.kind = "expired";
                 return finish(std::unexpected(std::move(refused)));
             }
             const QJsonObject tokens = reply.object();
             const QString access = tokens.value(QStringLiteral("access_token")).toString();
             if (access.isEmpty())
                 return fail("invalid", "token reply without an access token", host);
             CodexSession session = sessionOf(access, tokens.value(QStringLiteral("refresh_token")).toString(),
                                              tokens.value(QStringLiteral("id_token")).toString(), {});
             if (session.expiresAt <= 0)
                 session.expiresAt = m_clock.now() + tokens.value(QStringLiteral("expires_in")).toInt(3600) * 1000LL;
             if (refreshed) {
                 const auto keep = [](std::string &now, const std::string &before) {
                     if (now.empty())
                         now = before;
                 };
                 keep(session.refresh, refreshed->refresh);
                 keep(session.idToken, refreshed->idToken);
                 keep(session.accountId, refreshed->accountId);
                 keep(session.email, refreshed->email);
             }
             store(serial, std::move(session));
         });
}

void AiSignIn::store(std::uint64_t serial, CodexSession session)
{
    m_keys.write(kCodexSecret, sessionJson(session),
                 [this, alive = std::weak_ptr(m_alive), serial, email = session.email](std::optional<app::KeyError> error) {
                     if (!alive.lock() || !live(serial))
                         return;
                     if (error)
                         return fail("keychain", keyErrorDetail(*error));
                     finish(SignedIn{.email = email, .key = {}});
                 });
}

void AiSignIn::codexSession(std::function<void(std::optional<CodexSession>)> done)
{
    m_keys.read(kCodexSecret, [alive = std::weak_ptr(m_alive), done = std::move(done)](app::KeyResult result) {
        if (!alive.lock())
            return;
        const auto *json = std::get_if<std::string>(&result);
        done(json ? sessionFromJson(*json) : std::nullopt);
    });
}

void AiSignIn::refreshCodex(Done done)
{
    if (m_running) {
        done(std::unexpected(SignInFailure{.kind = "invalid", .detail = "a sign-in is running"}));
        return;
    }
    codexSession([this, alive = std::weak_ptr(m_alive), done = std::move(done)](std::optional<CodexSession> session) mutable {
        if (!alive.lock())
            return;
        if (!session) {
            done(std::unexpected(SignInFailure{.kind = "expired"}));
            return;
        }
        const std::uint64_t serial = begin({}, std::move(done));
        const QByteArray body = form({{QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
                                      {QStringLiteral("refresh_token"), QString::fromStdString(session->refresh)},
                                      {QStringLiteral("client_id"), m_endpoints.codexClientId}});
        exchange(serial, body, std::move(session));
    });
}

void AiSignIn::importCodex(const QString &authJsonPath, Done done)
{
    const std::optional<CodexSession> session = sessionFromCodexAuth(authJsonPath);
    if (!session) {
        done(std::unexpected(SignInFailure{.kind = "import"}));
        return;
    }
    m_keys.write(kCodexSecret, sessionJson(*session),
                 [alive = std::weak_ptr(m_alive), email = session->email, done = std::move(done)](std::optional<app::KeyError> error) {
                     if (!alive.lock())
                         return;
                     if (error)
                         done(std::unexpected(SignInFailure{.kind = "keychain", .detail = keyErrorDetail(*error)}));
                     else
                         done(SignedIn{.email = email, .key = {}});
                 });
}

void AiSignIn::signOutCodex(std::function<void(std::optional<app::KeyError>)> done)
{
    m_keys.remove(kCodexSecret, [alive = std::weak_ptr(m_alive), done = std::move(done)](std::optional<app::KeyError> error) {
        if (alive.lock())
            done(error == app::KeyError::NotFound ? std::nullopt : error);
    });
}

}
