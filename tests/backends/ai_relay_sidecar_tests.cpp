#include "hikari/backends/ai_http_relay.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <optional>
#include <vector>

using namespace hikari::backends::ai;
using hikari::api::Json;
namespace app = hikari::application::ai;

namespace {

constexpr const char *kKey = "sk-test-0123456789abcdef";

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
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

// The real hikari-ai, sandboxed as the app starts it, reaching a provider on loopback only through Qt.
class AiRelaySidecarTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_relay_sidecar_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void SetUp() override
    {
        ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection())
                QObject::connect(socket, &QTcpSocket::readyRead, [this, socket] {
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n"))
                        return;
                    const QByteArray body = R"({"data":[{"id":"a/b","name":"B"}]})";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
        });
        base = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
        relay = std::make_unique<HttpRelay>([this](const Json &target, std::function<void(RelayTargetResult)> done) {
            if (target.find("id") && target.find("id")->asString() == "mock")
                return done(RelayTarget{QUrl(base), kKey});
            done(std::unexpected(app::Error{app::ErrorKind::NoProvider, 0, ""}));
        });

        AiSidecarHost::Options o;
        o.program = QStringLiteral(HIKARI_AI_SIDECAR_FILE);
        o.workingDirectory = dir.filePath(QStringLiteral("work"));
        o.buildId = HIKARI_AI_BUILD_ID;
        o.handshakeMs = 30'000;
        o.sandbox = hikari::backends::sandbox::Profile{
            .name = QStringLiteral("HikariSub.Test.Sidecar"),
            .readExecute = {QFileInfo(o.program).absolutePath()},
            .readWrite = {o.workingDirectory},
        };
        sidecar = std::make_unique<AiSidecarHost>(std::move(o), nullptr, hikari::api::Caller{"ai", "Hikari AI", false});
        relay->attach(*sidecar);
    }

    void TearDown() override { sidecar->shutdown(); }

    SidecarResult call(const std::string &method, Json params)
    {
        std::optional<SidecarResult> result;
        sidecar->request(method, std::move(params), [&](SidecarResult r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); })) << method;
        return result.value_or(std::unexpected(SidecarFailure{SidecarError::SidecarLost, 0, "no reply", {}}));
    }

    QTemporaryDir dir;
    QTcpServer server;
    QByteArray request;
    QString base;
    std::unique_ptr<HttpRelay> relay;
    std::unique_ptr<AiSidecarHost> sidecar;
};

TEST_F(AiRelaySidecarTest, TheSandboxedSidecarListsModelsThroughTheRelayWithQtsKey)
{
    ASSERT_TRUE(call("_hikari/providers.set",
                     Json::object({{"providers", Json::array({Json::object({{"id", "mock"},
                                                                            {"preset", "openrouter"},
                                                                            {"wire", "chat-completions"},
                                                                            {"baseUrl", base.toStdString()}})})}})));
    const SidecarResult listed = call("_hikari/models.list", Json::object({{"providerId", "mock"}}));
    ASSERT_TRUE(listed) << listed.error().message << sidecar->diagnostics().toStdString();
    EXPECT_EQ(listed->find("models")->asArray().size(), 1u);
    EXPECT_TRUE(request.contains("Bearer " + QByteArray(kKey))) << request.toStdString();
    EXPECT_TRUE(request.startsWith("GET /v1/models")) << request.toStdString();
}

TEST_F(AiRelaySidecarTest, AnUnknownProviderFailsInQtsRelayLookup)
{
    ASSERT_TRUE(call("_hikari/providers.set",
                     Json::object({{"providers", Json::array({Json::object({{"id", "other"},
                                                                            {"preset", "openrouter"},
                                                                            {"wire", "chat-completions"},
                                                                            {"baseUrl", base.toStdString()}})})}})));
    const SidecarResult listed = call("_hikari/models.list", Json::object({{"providerId", "other"}}));
    ASSERT_FALSE(listed);
    EXPECT_EQ(requestError(listed.error()).kind, app::ErrorKind::NoProvider);
    EXPECT_TRUE(request.isEmpty());
}

}
