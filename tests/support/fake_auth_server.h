#pragma once

#include "hikari/backends/ai_signin.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace hikari::test {

struct AuthAnswer {
    int status = 200;
    QByteArray body;
    std::vector<std::pair<QByteArray, QByteArray>> headers;
};

struct AuthSeen {
    QByteArray path;
    QByteArray body;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
    QUrlQuery form() const { return QUrlQuery(QString::fromLatin1(body)); }
};

inline AuthAnswer jsonAnswer(const QJsonObject &body, int status = 200,
                             std::vector<std::pair<QByteArray, QByteArray>> headers = {})
{
    headers.emplace_back("Content-Type", "application/json");
    return {status, QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(headers)};
}

inline QByteArray jwt(const QJsonObject &claims)
{
    return "e30." +
           QJsonDocument(claims).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding |
                                                                         QByteArray::OmitTrailingEquals) +
           ".sig";
}

/// auth.openai.com and openrouter.ai on 127.0.0.1: each path answers from its queue, the last answer repeating.
class FakeAuthServer {
public:
    static constexpr const char *kUsercode = "/api/accounts/deviceauth/usercode";
    static constexpr const char *kPoll = "/api/accounts/deviceauth/token";
    static constexpr const char *kToken = "/oauth/token";
    static constexpr const char *kKeys = "/api/v1/auth/keys";

    FakeAuthServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] { accept(); });
    }

    QUrl base() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }

    /// The real sites for what the user sees, this server for Qt's requests.
    backends::ai::SignInEndpoints endpoints(quint16 browserPort) const
    {
        backends::ai::SignInEndpoints e;
        e.codexClientId = QStringLiteral("app_test");
        e.codexBrowserPort = browserPort;
        e.codexApi = base();
        e.openRouterApi = base();
        return e;
    }

    FakeAuthServer &on(const QByteArray &path, std::vector<AuthAnswer> answers)
    {
        m_answers[path] = std::move(answers);
        return *this;
    }

    std::vector<AuthSeen> at(const QByteArray &path) const
    {
        std::vector<AuthSeen> found;
        for (const AuthSeen &s : seen)
            if (s.path == path)
                found.push_back(s);
        return found;
    }

    void stop() { m_server.close(); }

    static AuthAnswer tokens(const QString &email = QStringLiteral("owner@example.com"))
    {
        return jsonAnswer({{QStringLiteral("id_token"),
                            QString::fromLatin1(jwt({{QStringLiteral("email"), email},
                                                     {QStringLiteral("https://api.openai.com/auth"),
                                                      QJsonObject{{QStringLiteral("chatgpt_account_id"),
                                                                   QStringLiteral("acct-fake")}}}}))},
                           {QStringLiteral("access_token"), QString::fromLatin1(jwt({{QStringLiteral("exp"), 4102444800LL}}))},
                           {QStringLiteral("refresh_token"), QStringLiteral("fake-refresh")}});
    }

    /// Every flow ends with `outcome`: ok, hang (the code is never entered), codesOff, denied or expired (a refused
    /// refresh). The browser flows wait for approve() whatever the outcome.
    void script(std::string_view outcome)
    {
        m_answers.clear();
        on(kUsercode, {outcome == "codesOff"
                           ? jsonAnswer({}, 404)
                           : jsonAnswer({{QStringLiteral("device_auth_id"), QStringLiteral("dev-1")},
                                         {QStringLiteral("user_code"), QStringLiteral("FAKE-1234")},
                                         {QStringLiteral("interval"), 0}})});
        if (outcome == "hang")
            on(kPoll, {jsonAnswer({}, 403)});
        else if (outcome == "denied")
            on(kPoll, {jsonAnswer({{QStringLiteral("error"), QStringLiteral("access_denied")}}, 400)});
        else
            on(kPoll, {jsonAnswer({{QStringLiteral("authorization_code"), QStringLiteral("auth-1")},
                                   {QStringLiteral("code_verifier"), QStringLiteral("verifier-1")}})});
        on(kToken, {outcome == "expired"
                        ? jsonAnswer({{QStringLiteral("error"),
                                       QJsonObject{{QStringLiteral("code"), QStringLiteral("refresh_token_reused")}}}},
                                     401)
                        : tokens()});
        on(kKeys, {outcome == "denied" ? jsonAnswer({{QStringLiteral("error"), QStringLiteral("bad code")}}, 403)
                                       : jsonAnswer({{QStringLiteral("key"), QStringLiteral("sk-or-v1-fake")}})});
    }

    std::vector<AuthSeen> seen;

private:
    void accept()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                buffer->append(socket->readAll());
                const qsizetype end = buffer->indexOf("\r\n\r\n");
                if (end < 0)
                    return;
                const QByteArray head = buffer->left(end);
                qsizetype length = 0;
                for (const QByteArray &line : head.split('\n'))
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toLongLong();
                if (buffer->size() < end + 4 + length)
                    return;
                const QByteArray path = QUrl(QString::fromLatin1(head.split(' ').value(1))).path().toLatin1();
                seen.push_back({path, buffer->mid(end + 4, length)});
                answer(socket, path);
            });
        }
    }

    void answer(QTcpSocket *socket, const QByteArray &path)
    {
        AuthAnswer reply{404, "Not found", {}};
        if (auto it = m_answers.find(path); it != m_answers.end() && !it->second.empty()) {
            reply = it->second.front();
            if (it->second.size() > 1)
                it->second.erase(it->second.begin());
        }
        QByteArray out = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\n";
        for (const auto &[name, value] : reply.headers)
            out += name + ": " + value + "\r\n";
        out += "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\nConnection: close\r\n\r\n" + reply.body;
        socket->write(out);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    std::map<QByteArray, std::vector<AuthAnswer>> m_answers;
};

/// A GET as the browser would send it; the status, or -1 when nothing answers.
inline int browse(const QUrl &url, QByteArray *body = nullptr)
{
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkReply *reply = nam.get(QNetworkRequest(url));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10'000, &loop, &QEventLoop::quit);
    loop.exec();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (body)
        *body = reply->readAll();
    reply->deleteLater();
    return status.isValid() ? status.toInt() : -1;
}

/// What the user's approval in the browser sends back to a sign-in page's 127.0.0.1 callback.
inline QUrl approval(const QUrl &page, const QString &code = QStringLiteral("code-1"))
{
    const QUrlQuery query(page);
    if (query.hasQueryItem(QStringLiteral("callback_url"))) {
        QUrl callback(query.queryItemValue(QStringLiteral("callback_url"), QUrl::FullyDecoded));
        callback.setQuery(QStringLiteral("code=") + code);
        return callback;
    }
    QUrl callback(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    callback.setHost(QStringLiteral("127.0.0.1"));
    callback.setQuery(QStringLiteral("code=%1&state=%2").arg(code, query.queryItemValue(QStringLiteral("state"))));
    return callback;
}

inline quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

/// A clock that moves only when a sign-in waits; each wait still yields to the event loop for `realMs`.
struct FakeSignInClock {
    std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    std::vector<int> waits;
    int realMs = 0;

    backends::ai::SignInClock clock()
    {
        return {.now = [this] { return now; },
                .wait = [this](int ms, std::function<void()> then) {
                    waits.push_back(ms);
                    now += ms;
                    QTimer::singleShot(realMs, std::move(then));
                }};
    }
};

}
