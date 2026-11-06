#include "hikari/app/plugin_network.h"

#include <QHttpHeaders>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QtTest>

using namespace hikari;
using app::PluginNetwork;
using api::Json;

namespace {

class FakeReply : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest &request, QByteArray verb, QByteArray sent, QObject *parent)
        : QNetworkReply(parent), verb(std::move(verb)), sent(std::move(sent))
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }

    void respond(int status, QByteArray body, const QList<std::pair<QByteArray, QByteArray>> &headers = {})
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        QHttpHeaders all;
        for (const auto &[name, value] : headers)
            all.append(name, value);
        setHeaders(all);
        if (status >= 400)
            setError(ContentNotFoundError, QStringLiteral("not found"));
        m_body = std::move(body);
        emit metaDataChanged();
        emit readyRead();
        setFinished(true);
        emit finished();
    }
    void redirect(const QUrl &to)
    {
        setUrl(to);
        emit redirected(to);
    }
    void fail(NetworkError error)
    {
        setError(error, QStringLiteral("the host was not found"));
        setFinished(true);
        emit errorOccurred(error);
        emit finished();
    }
    void abort() override
    {
        if (isFinished())
            return;
        aborted = true;
        fail(OperationCanceledError);
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_read + QIODevice::bytesAvailable(); }
    bool isSequential() const override { return true; }

    QByteArray verb;
    QByteArray sent;
    bool aborted = false;

protected:
    qint64 readData(char *data, qint64 most) override
    {
        const qint64 n = std::min(most, qint64(m_body.size()) - m_read);
        std::memcpy(data, m_body.constData() + m_read, std::size_t(n));
        m_read += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_read = 0;
};

class FakeNetwork : public QNetworkAccessManager {
public:
    QList<QPointer<FakeReply>> replies;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *data) override
    {
        auto *reply = new FakeReply(request, request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray(),
                                    data ? data->readAll() : QByteArray(), this);
        replies << reply;
        return reply;
    }
};

const std::vector<std::string> kHosts{"api.example.com", "cdn.example.com"};

}

class PluginNetworkTests : public QObject {
    Q_OBJECT

    FakeNetwork m_network;
    bool m_allowed = true;
    PluginNetwork m_plugins{m_network, [this] { return m_allowed; }};

    static Json request(std::string_view url, std::string_view more = {})
    {
        return *Json::parse("{\"url\": \"" + std::string(url) + "\"" + std::string(more) + "}");
    }
    std::optional<api::Result> fetch(const Json &params)
    {
        std::optional<api::Result> answer;
        m_plugins.fetch("com.example.net", kHosts, params, [&](api::Result r) { answer = std::move(r); });
        return answer;
    }
    static QString code(const std::optional<api::Result> &answer)
    {
        if (!answer || *answer)
            return QStringLiteral("no refusal");
        const Json *reason = answer->error().data.find("reason");
        return QString::fromStdString(answer->error().code) +
               (reason ? QStringLiteral(" ") + QString::fromStdString(reason->asString()) : QString());
    }
    FakeReply *last() { return m_network.replies.isEmpty() ? nullptr : m_network.replies.last().data(); }

private slots:
    void init()
    {
        m_allowed = true;
        m_network.replies.clear();
    }

    void requestsTheManifestDoesNotAllowNeverLeave()
    {
        m_allowed = false;
        QCOMPARE(code(fetch(request("https://api.example.com/v1"))), QStringLiteral("refused network_off"));
        m_allowed = true;
        QCOMPARE(code(fetch(request("http://api.example.com/v1"))), QStringLiteral("refused not_https"));
        QCOMPARE(code(fetch(request("ftp://api.example.com/v1"))), QStringLiteral("refused not_https"));
        const auto other = fetch(request("https://evil.example.net/v1"));
        QCOMPARE(code(other), QStringLiteral("not_granted"));
        QCOMPARE(other->error().data.find("host")->asString(), std::string("evil.example.net"));
        QCOMPARE(code(fetch(request("https://example.com/"))), QStringLiteral("not_granted"));
        QCOMPARE(code(fetch(request("https://api.example.com.evil.net/"))), QStringLiteral("not_granted"));
        QCOMPARE(code(fetch(request("https://API.Example.com:8443/"))), QStringLiteral("refused port"));
        QCOMPARE(code(fetch(request("https://user:pass@api.example.com/"))), QStringLiteral("invalid_params"));
        QCOMPARE(code(fetch(request("api.example.com/v1"))), QStringLiteral("invalid_params"));
        QCOMPARE(code(fetch(request("https://api.example.com/", ", \"method\": \"CONNECT\""))), QStringLiteral("invalid_params"));
        QCOMPARE(code(fetch(request("https://api.example.com/", ", \"headers\": {\"Host\": \"evil.example.net\"}"))),
                 QStringLiteral("invalid_params"));
        QCOMPARE(code(fetch(request("https://api.example.com/", ", \"headers\": {\"Proxy-Authorization\": \"x\"}"))),
                 QStringLiteral("invalid_params"));
        QCOMPARE(code(fetch(request("https://api.example.com/", ", \"headers\": {\"X-Note\": \"a\\r\\nHost: evil\"}"))),
                 QStringLiteral("invalid_params"));
        QCOMPARE(code(fetch(request("https://api.example.com/", ", \"body\": \"x\""))), QStringLiteral("invalid_params"));
        QCOMPARE(code(fetch(request("https://api.example.com/", ", \"socket\": true"))), QStringLiteral("invalid_params"));
        QVERIFY(m_network.replies.isEmpty());
    }

    void aDeclaredHostIsFetchedByHikariSub()
    {
        std::optional<api::Result> answer;
        m_plugins.fetch("com.example.net", kHosts,
                        request("https://API.example.com/v1?q=1", ", \"method\": \"POST\","
                                         "\"headers\": {\"Authorization\": \"Bearer t\", \"Content-Type\": \"application/json\"},"
                                         "\"body\": \"{\\\"a\\\": 1}\""),
                        [&](api::Result r) { answer = std::move(r); });
        FakeReply *reply = last();
        QVERIFY(reply);
        QCOMPARE(reply->verb, QByteArray("POST"));
        QCOMPARE(reply->sent, QByteArray("{\"a\": 1}"));
        QCOMPARE(reply->request().url(), QUrl(QStringLiteral("https://api.example.com/v1?q=1")));
        QCOMPARE(reply->request().rawHeader("Authorization"), QByteArray("Bearer t"));
        QCOMPARE(reply->request().attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
        QCOMPARE(reply->request().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 int(QNetworkRequest::UserVerifiedRedirectPolicy));
        QVERIFY(!answer);
        reply->respond(201, "créé", {{"Content-Type", "text/plain"}, {"Vary", "Accept"}, {"vary", "Origin"}, {"Set-Cookie", "a=1"}, {"Set-Cookie", "b=2"}});
        QVERIFY(answer && *answer);
        const Json &result = **answer;
        QCOMPARE(result.find("status")->asInt(), 201);
        QCOMPARE(result.find("body")->asString(), std::string("créé"));
        QCOMPARE(result.find("encoding")->asString(), std::string("utf8"));
        QCOMPARE(result.find("headers")->find("content-type")->asString(), std::string("text/plain"));
        QCOMPARE(result.find("headers")->find("vary")->asString(), std::string("Accept, Origin"));
        QCOMPARE(result.find("headers")->find("set-cookie")->asString(), std::string("a=1\nb=2"));
        QCOMPARE(result.find("url")->asString(), std::string("https://api.example.com/v1?q=1"));
    }

    void anErrorStatusIsAnAnswerNotARefusal()
    {
        const auto answer = [&] {
            std::optional<api::Result> a;
            m_plugins.fetch("com.example.net", kHosts, request("https://cdn.example.com/missing"),
                            [&](api::Result r) { a = std::move(r); });
            last()->respond(404, "nope");
            return a;
        }();
        QVERIFY(answer && *answer);
        QCOMPARE((*answer)->find("status")->asInt(), 404);
    }

    void base64CarriesBytesBothWays()
    {
        std::optional<api::Result> answer;
        m_plugins.fetch("com.example.net", kHosts,
                        request("https://api.example.com/up", ", \"method\": \"PUT\", \"encoding\": \"base64\", \"body\": \"AP8=\""),
                        [&](api::Result r) { answer = std::move(r); });
        QCOMPARE(last()->sent, QByteArray("\x00\xFF", 2));
        last()->respond(200, QByteArray("\x89PNG", 4));
        QVERIFY(answer && *answer);
        QCOMPARE((*answer)->find("body")->asString(), std::string("iVBORw=="));

        std::optional<api::Result> text;
        m_plugins.fetch("com.example.net", kHosts, request("https://api.example.com/logo"),
                        [&](api::Result r) { text = std::move(r); });
        last()->respond(200, QByteArray("\x89PNG", 4));
        QCOMPARE(code(text), QStringLiteral("refused not_text"));
        QCOMPARE(code(fetch(request("https://api.example.com/up", ", \"method\": \"PUT\", \"encoding\": \"base64\", \"body\": \"not base64!\""))),
                 QStringLiteral("invalid_params"));
    }

    void aRedirectMustStayOnDeclaredHosts()
    {
        std::optional<api::Result> answer;
        m_plugins.fetch("com.example.net", kHosts, request("https://api.example.com/go"),
                        [&](api::Result r) { answer = std::move(r); });
        FakeReply *reply = last();
        QSignalSpy allowed(reply, &QNetworkReply::redirectAllowed);
        reply->redirect(QUrl(QStringLiteral("https://cdn.example.com/file")));
        QCOMPARE(allowed.size(), 1);
        reply->redirect(QUrl(QStringLiteral("http://cdn.example.com/file")));
        QCOMPARE(allowed.size(), 1);
        QVERIFY(reply->aborted);
        QCOMPARE(code(answer), QStringLiteral("refused not_https"));

        std::optional<api::Result> away;
        m_plugins.fetch("com.example.net", kHosts, request("https://api.example.com/go"),
                        [&](api::Result r) { away = std::move(r); });
        last()->redirect(QUrl(QStringLiteral("https://tracker.example.org/")));
        QCOMPARE(code(away), QStringLiteral("not_granted"));
    }

    void aResponseOverTheLimitIsRefused()
    {
        std::optional<api::Result> answer;
        m_plugins.fetch("com.example.net", kHosts, request("https://cdn.example.com/huge"),
                        [&](api::Result r) { answer = std::move(r); });
        last()->respond(200, QByteArray(qsizetype(PluginNetwork::kMaxBodyBytes) + 1, 'a'));
        QCOMPARE(code(answer), QStringLiteral("refused too_large"));
    }

    void failuresAndTimeoutsAreRefusals()
    {
        std::optional<api::Result> lost;
        m_plugins.fetch("com.example.net", kHosts, request("https://api.example.com/"),
                        [&](api::Result r) { lost = std::move(r); });
        last()->fail(QNetworkReply::HostNotFoundError);
        QCOMPARE(code(lost), QStringLiteral("refused failed"));
        QCOMPARE(lost->error().message, std::string("the host was not found"));

        std::optional<api::Result> slow;
        m_plugins.fetch("com.example.net", kHosts, request("https://api.example.com/"),
                        [&](api::Result r) { slow = std::move(r); });
        QCOMPARE(last()->request().transferTimeoutAsDuration().count(), PluginNetwork::kTimeoutMs);
        last()->fail(QNetworkReply::OperationCanceledError);
        QCOMPARE(code(slow), QStringLiteral("refused timeout"));
    }

    void cancellingDropsAPluginsRequestsWithoutAnswering()
    {
        bool answered = false;
        m_plugins.fetch("com.example.net", kHosts, request("https://api.example.com/"),
                        [&](api::Result) { answered = true; });
        FakeReply *mine = last();
        bool otherAnswered = false;
        m_plugins.fetch("com.example.other", kHosts, request("https://api.example.com/"),
                        [&](api::Result) { otherAnswered = true; });
        FakeReply *other = last();
        m_plugins.cancel("com.example.net");
        QVERIFY(mine->aborted);
        QVERIFY(!answered);
        QVERIFY(!other->aborted);
        other->respond(200, "ok");
        QVERIFY(otherAnswered);
    }
};

QTEST_GUILESS_MAIN(PluginNetworkTests)
#include "plugin_network_tests.moc"
