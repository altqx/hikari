#include "hikari/backends/ai_http_relay.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <gtest/gtest.h>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace hikari::backends::ai;
using hikari::api::Json;
namespace app = hikari::application::ai;

namespace {

constexpr const char *kKey = "sk-test-0123456789abcdef";

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() >= ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

QList<QHostAddress> addresses(std::initializer_list<const char *> texts)
{
    QList<QHostAddress> list;
    for (const char *t : texts)
        list << QHostAddress(QString::fromLatin1(t));
    return list;
}

struct Received {
    QByteArray method;
    QByteArray path;
    std::map<QByteArray, QByteArray> headers;
    QByteArray body;
};

struct Reply {
    int status = 200;
    QByteArray headers = "Content-Type: text/plain\r\n";
    std::vector<QByteArray> chunks;
    bool hang = false;
    bool stayOpen = false;
};

/// Answers each connection with the next scripted reply, then closes it.
class Server : public QObject {
public:
    Server()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                ++connections;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
            }
        });
    }

    quint16 port() const { return server.serverPort(); }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(port()); }

    QTcpServer server;
    std::deque<Reply> replies;
    std::vector<Received> received;
    int connections = 0;

private:
    void read(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        Received r;
        const QList<QByteArray> lines = buffer.first(end).split('\n');
        const QList<QByteArray> request = lines.front().trimmed().split(' ');
        r.method = request.value(0);
        r.path = request.value(1);
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines[i].indexOf(':');
            if (colon > 0)
                r.headers[lines[i].first(colon).trimmed().toLower()] = lines[i].mid(colon + 1).trimmed();
        }
        const qsizetype length = r.headers.contains("content-length") ? r.headers["content-length"].toLongLong() : 0;
        if (buffer.size() < end + 4 + length)
            return;
        r.body = buffer.mid(end + 4, length);
        m_buffers.erase(socket);
        received.push_back(r);
        Reply reply = replies.empty() ? Reply{} : replies.front();
        if (!replies.empty())
            replies.pop_front();
        if (reply.hang)
            return;
        socket->write("HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\n" + reply.headers +
                      "Connection: close\r\n\r\n");
        auto chunks = std::make_shared<std::deque<QByteArray>>(reply.chunks.begin(), reply.chunks.end());
        auto next = std::make_shared<std::function<void()>>();
        const bool stayOpen = reply.stayOpen;
        *next = [socket, chunks, next, stayOpen] {
            if (chunks->empty()) {
                if (!stayOpen)
                    socket->disconnectFromHost();
                return;
            }
            socket->write(chunks->front());
            socket->flush();
            chunks->pop_front();
            QTimer::singleShot(30, socket, *next);
        };
        (*next)();
    }

    std::map<QTcpSocket *, QByteArray> m_buffers;
};

struct Outcome {
    std::vector<std::pair<std::string, Json>> notes;
    std::optional<Json> result;
    std::optional<int> code;
    Json data;
    int answers = 0;

    RelayExchange exchange()
    {
        return {[this](const std::string &method, Json params) { notes.emplace_back(method, std::move(params)); },
                [this](Json r) {
                    result = std::move(r);
                    ++answers;
                },
                [this](int c, std::string, Json d) {
                    code = c;
                    data = std::move(d);
                    ++answers;
                }};
    }

    bool done() const { return answers > 0; }
    std::string kind() const { return data.find("kind") ? data.find("kind")->asString() : std::string(); }
    int status() const
    {
        for (const auto &[method, params] : notes)
            if (method == kHttpResponse)
                return static_cast<int>(params.find("status")->asInt());
        return 0;
    }
    QByteArray body() const
    {
        QByteArray all;
        for (const auto &[method, params] : notes)
            if (method == kHttpData)
                all += QByteArray::fromBase64(QByteArray::fromStdString(params.find("data")->asString()));
        return all;
    }
};

class HttpRelayTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_http_relay_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    std::unique_ptr<HttpRelay> relay(QString base = {}, std::string key = kKey)
    {
        if (base.isEmpty())
            base = server.base();
        RelayTargets targets = [base, key](const Json &target, std::function<void(RelayTargetResult)> done) {
            const Json *id = target.find("id");
            if (!id || id->asString() != "p")
                return done(std::unexpected(app::Error{app::ErrorKind::NoProvider, 0, ""}));
            done(RelayTarget{QUrl(base), key});
        };
        HttpRelay::Network network{[this](const QString &host, std::function<void(QList<QHostAddress>)> done) {
                                       resolved << host;
                                       done(names.value(host));
                                   },
                                   [this] { return local; }};
        return std::make_unique<HttpRelay>(std::move(targets), std::move(network));
    }

    static Json params(const QString &url, Json headers = Json::array(), const QByteArray &body = {},
                       const std::string &method = "POST", const std::string &provider = "p")
    {
        auto p = Json::object({{"stream", 7},
                               {"target", Json::object({{"kind", "provider"}, {"id", provider}})},
                               {"method", method},
                               {"url", url.toStdString()},
                               {"headers", std::move(headers)}});
        if (!body.isEmpty())
            p["body"] = body.toBase64().toStdString();
        return p;
    }

    static Json bearer() { return Json::array({Json::array({"authorization", "Bearer hikari-key"})}); }

    Outcome run(HttpRelay &r, const Json &p)
    {
        Outcome o;
        r.request(p, o.exchange());
        EXPECT_TRUE(waitFor([&] { return o.done(); }));
        EXPECT_EQ(o.answers, 1);
        return o;
    }

    Server server;
    QMap<QString, QList<QHostAddress>> names;
    QList<QHostAddress> local = addresses({"192.168.1.10"});
    QStringList resolved;
};

TEST(KeySafeOverPlainHttp, TailscaleRangesNeedALocalAddressInTheSameRange)
{
    const auto lanOnly = addresses({"192.168.1.10"});
    const auto onTailnet = addresses({"192.168.1.10", "100.101.102.103", "fd7a:115c:a1e0::5"});
    const auto ipv4Tailnet = addresses({"100.101.102.103"});
    const auto safe = [](const char *address, const QList<QHostAddress> &l) {
        return keySafeOverPlainHttp(QHostAddress(QString::fromLatin1(address)), l);
    };
    EXPECT_TRUE(safe("127.0.0.1", lanOnly));
    EXPECT_TRUE(safe("::1", lanOnly));
    EXPECT_TRUE(safe("100.64.0.1", onTailnet));
    EXPECT_TRUE(safe("100.127.255.254", onTailnet));
    EXPECT_TRUE(safe("::ffff:100.100.1.2", onTailnet));
    EXPECT_FALSE(safe("100.100.1.2", lanOnly));
    EXPECT_FALSE(safe("100.128.0.1", onTailnet));
    EXPECT_FALSE(safe("100.63.255.255", onTailnet));
    EXPECT_TRUE(safe("fd7a:115c:a1e0::1", onTailnet));
    EXPECT_TRUE(safe("fd7a:115c:a1e0:ab12:4843:cd96:6258:b240", onTailnet));
    EXPECT_FALSE(safe("fd7a:115c:a1e0::1", lanOnly));
    EXPECT_FALSE(safe("fd7a:115c:a1e0::1", ipv4Tailnet));
    EXPECT_FALSE(safe("fd7a:115c:a1e1::1", onTailnet));
    EXPECT_FALSE(safe("192.168.1.20", onTailnet));
    EXPECT_FALSE(safe("10.0.0.5", onTailnet));
    EXPECT_FALSE(safe("203.0.113.9", onTailnet));
}

TEST(WithinBase, OnlyTheBaseAndPathsUnderItOnTheSameOrigin)
{
    const QUrl base(QStringLiteral("https://api.example.com/v1"));
    const auto within = [&](const char *url) { return withinBase(QUrl(QString::fromLatin1(url)), base); };
    EXPECT_TRUE(within("https://api.example.com/v1"));
    EXPECT_TRUE(within("https://api.example.com/v1/chat/completions"));
    EXPECT_TRUE(within("https://API.example.com:443/v1/models"));
    EXPECT_FALSE(within("https://api.example.com/v1x"));
    EXPECT_FALSE(within("https://api.example.com/v2/models"));
    EXPECT_FALSE(within("http://api.example.com/v1/models"));
    EXPECT_FALSE(within("https://api.example.com:8443/v1/models"));
    EXPECT_FALSE(within("https://evil.example/v1/models"));
    EXPECT_FALSE(within("https://user:pw@api.example.com/v1/models"));
    EXPECT_FALSE(within("https://api.example.com/v1/../admin"));
    EXPECT_FALSE(within("https://api.example.com/v1/%2e%2e/admin"));
    EXPECT_TRUE(withinBase(QUrl(QStringLiteral("https://h.example/anything")), QUrl(QStringLiteral("https://h.example"))));
}

TEST_F(HttpRelayTest, QtAddsTheKeyAndStreamsTheResponseBackWithTheKeyMasked)
{
    const std::string key = kKey;
    server.replies.push_back({200, "Content-Type: text/event-stream\r\nX-Echo: " + QByteArray(kKey) + "\r\n",
                              {"data: one\n\n", "data: " + QByteArray::fromStdString(key.substr(0, 9)),
                               QByteArray::fromStdString(key.substr(9)) + " two\n\n"}});
    auto r = relay();
    auto headers = bearer();
    headers.push(Json::array({"content-type", "application/json"}));
    const Outcome o = run(*r, params(server.base() + QStringLiteral("/chat/completions"), headers, "{\"x\":1}"));
    ASSERT_TRUE(o.result) << o.kind();
    ASSERT_EQ(server.received.size(), 1u);
    EXPECT_EQ(server.received[0].method, "POST");
    EXPECT_EQ(server.received[0].path, "/v1/chat/completions");
    EXPECT_EQ(server.received[0].headers["authorization"], "Bearer " + QByteArray(kKey));
    EXPECT_EQ(server.received[0].body, "{\"x\":1}");
    EXPECT_EQ(o.status(), 200);
    EXPECT_EQ(o.body(), "data: one\n\ndata: [key] two\n\n");
    EXPECT_EQ(o.notes.front().first, kHttpResponse);
    EXPECT_EQ(o.notes.front().second.find("stream")->asInt(), 7);
    EXPECT_EQ(o.notes.front().second.dump().find(kKey), std::string::npos);
    EXPECT_NE(o.notes.front().second.dump().find("[key]"), std::string::npos);
    EXPECT_TRUE(resolved.isEmpty());
}

TEST_F(HttpRelayTest, AnSseEventIsPassedOnBeforeTheNextOneArrives)
{
    server.replies.push_back({200, "Content-Type: text/event-stream\r\n", {"data: one\n\n"}, false, true});
    auto r = relay();
    Outcome o;
    const auto cancel = r->request(params(server.base() + QStringLiteral("/chat"), bearer()), o.exchange());
    ASSERT_TRUE(waitFor([&] { return o.body() == "data: one\n\n"; }));
    EXPECT_FALSE(o.done());
    cancel();
}

TEST_F(HttpRelayTest, AKeylessProviderSendsNoKeyHeaderAndMayUsePlainHttpAnywhere)
{
    auto r = relay({}, "");
    const Outcome o = run(*r, params(server.base() + QStringLiteral("/models"), bearer(), {}, "GET"));
    ASSERT_TRUE(o.result);
    ASSERT_EQ(server.received.size(), 1u);
    EXPECT_FALSE(server.received[0].headers.contains("authorization"));
}

TEST_F(HttpRelayTest, AKeyTravelsOnlyOverHttpsToThisMachineOrATailnet)
{
    for (const char *base : {"http://192.168.1.20:8000/v1", "http://100.100.1.2:8000/v1"}) {
        auto r = relay(QString::fromLatin1(base));
        EXPECT_EQ(run(*r, params(QString::fromLatin1(base) + QStringLiteral("/chat"), bearer())).kind(), "insecureKey");
    }
    names[QStringLiteral("box.lan")] = addresses({"192.168.1.20"});
    auto lan = relay(QStringLiteral("http://box.lan:8000/v1"));
    EXPECT_EQ(run(*lan, params(QStringLiteral("http://box.lan:8000/v1/chat"), bearer())).kind(), "insecureKey");
    auto nowhere = relay(QStringLiteral("http://nowhere.example/v1"));
    EXPECT_EQ(run(*nowhere, params(QStringLiteral("http://nowhere.example/v1/chat"), bearer())).kind(), "network");
    EXPECT_EQ(server.connections, 0);
}

TEST_F(HttpRelayTest, ANameIsResolvedOnceAndTheCheckedAddressIsUsed)
{
    names[QStringLiteral("peer.example")] = addresses({"192.168.1.20", "127.0.0.1"});
    const QString base = QStringLiteral("http://peer.example:%1/v1").arg(server.port());
    auto r = relay(base);
    const Outcome o = run(*r, params(base + QStringLiteral("/chat"), bearer()));
    ASSERT_TRUE(o.result) << o.kind();
    EXPECT_EQ(resolved, QStringList{QStringLiteral("peer.example")});
    ASSERT_EQ(server.received.size(), 1u);
    EXPECT_EQ(server.received[0].headers["host"], "peer.example:" + QByteArray::number(server.port()));
}

TEST_F(HttpRelayTest, AMagicDnsNameResolvingOutsideTheTailnetIsRefused)
{
    local << QHostAddress(QStringLiteral("100.101.102.103"));
    names[QStringLiteral("gpu-box.tail1234.ts.net")] = addresses({"203.0.113.9"});
    auto r = relay(QStringLiteral("http://gpu-box.tail1234.ts.net:8000/v1"));
    EXPECT_EQ(run(*r, params(QStringLiteral("http://gpu-box.tail1234.ts.net:8000/v1/chat"), bearer())).kind(),
              "insecureKey");
    EXPECT_EQ(server.connections, 0);
}

TEST_F(HttpRelayTest, RequestsStayUnderTheBaseUrlAndNeverFollowARedirect)
{
    server.replies.push_back({302, "Location: http://127.0.0.1:1/elsewhere\r\n", {}});
    auto r = relay();
    const Outcome redirected = run(*r, params(server.base() + QStringLiteral("/chat"), bearer()));
    ASSERT_TRUE(redirected.result);
    EXPECT_EQ(redirected.status(), 302);
    for (const QString &outside :
         {QStringLiteral("http://127.0.0.1:%1/v2/models").arg(server.port()),
          QStringLiteral("http://127.0.0.1:%1/v1x").arg(server.port()),
          QStringLiteral("http://127.0.0.1:%1/v1/../v2").arg(server.port()), QStringLiteral("https://example.com/v1/models"),
          QStringLiteral("file:///etc/passwd")})
        EXPECT_EQ(run(*r, params(outside, bearer())).kind(), "rejected") << outside.toStdString();
    EXPECT_EQ(server.received.size(), 1u);
}

TEST_F(HttpRelayTest, HopAndCookieHeadersAreDroppedAndBrokenOnesRefused)
{
    auto r = relay();
    auto headers = bearer();
    for (const char *name : {"accept-encoding", "cookie", "host", "proxy-authorization"})
        headers.push(Json::array({name, "x"}));
    ASSERT_TRUE(run(*r, params(server.base() + QStringLiteral("/chat"), headers)).result);
    ASSERT_EQ(server.received.size(), 1u);
    EXPECT_FALSE(server.received[0].headers.contains("cookie"));
    EXPECT_FALSE(server.received[0].headers.contains("proxy-authorization"));
    EXPECT_NE(server.received[0].headers["host"], "x");
    EXPECT_NE(server.received[0].headers["accept-encoding"], "x");

    auto split = Json::array({Json::array({"x-a", "1\r\nx-b: 2"})});
    EXPECT_EQ(run(*r, params(server.base() + QStringLiteral("/chat"), split)).code, rpc::kInvalidParams);
    EXPECT_EQ(run(*r, params(server.base() + QStringLiteral("/chat"), bearer(), {}, "CONNECT")).code,
              rpc::kInvalidParams);
    EXPECT_EQ(server.received.size(), 1u);
}

TEST_F(HttpRelayTest, AnUnknownTargetFailsWithItsKind)
{
    auto r = relay();
    const Outcome o = run(*r, params(server.base() + QStringLiteral("/chat"), bearer(), {}, "POST", "gone"));
    EXPECT_EQ(o.code, rpc::kProviderFailed);
    EXPECT_EQ(o.kind(), "noProvider");
    EXPECT_EQ(server.connections, 0);
}

TEST_F(HttpRelayTest, CancellingAbortsTheRequestAndAnswersOnce)
{
    server.replies.push_back({.hang = true});
    auto r = relay();
    Outcome o;
    const auto cancel = r->request(params(server.base() + QStringLiteral("/chat"), bearer()), o.exchange());
    ASSERT_TRUE(waitFor([&] { return server.received.size() == 1; }));
    cancel();
    cancel();
    ASSERT_TRUE(waitFor([&] { return o.done(); }));
    QCoreApplication::processEvents();
    EXPECT_EQ(o.answers, 1);
    EXPECT_EQ(o.code, rpc::kRequestCancelled);
}

TEST_F(HttpRelayTest, AFailedConnectionIsANetworkError)
{
    QTcpServer closed;
    ASSERT_TRUE(closed.listen(QHostAddress::LocalHost));
    const QString base = QStringLiteral("http://127.0.0.1:%1/v1").arg(closed.serverPort());
    closed.close();
    auto r = relay(base);
    const Outcome o = run(*r, params(base + QStringLiteral("/chat"), bearer()));
    EXPECT_EQ(o.code, rpc::kProviderFailed);
    EXPECT_EQ(o.kind(), "network");
}

}
