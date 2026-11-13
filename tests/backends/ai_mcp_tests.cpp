#include "hikari/backends/ai_mcp_bridge.h"
#include "hikari/backends/ai_mcp_server.h"
#include "hikari/backends/ai_sidecar_host.h"

#include "fake_host.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

using namespace hikari::backends::ai;
using hikari::api::Json;

namespace {

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

const QByteArray kToken = QByteArray(64, 'a');
const std::string kModern = R"("_meta": {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
    "io.modelcontextprotocol/clientCapabilities": {}, "io.modelcontextprotocol/clientInfo": {"name": "claude-code",
    "title": "Claude Code", "version": "3"}})";

std::string toolText(const Json &result)
{
    std::string out;
    for (const Json &c : result.find("content")->asArray())
        out += c.find("text")->asString();
    return out;
}

#ifdef _WIN32
struct FileAcl {
    bool isProtected = false;
    std::vector<std::wstring> allowed;
    std::wstring sddl;
};

std::wstring sidText(PSID sid)
{
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text))
        return {};
    std::wstring out(text);
    LocalFree(text);
    return out;
}

std::wstring currentUser()
{
    HANDLE token = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> user(size);
    GetTokenInformation(token, TokenUser, user.data(), size, &size);
    CloseHandle(token);
    return sidText(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid);
}

FileAcl aclOf(const QString &file)
{
    FileAcl acl;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(file).utf16()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
        return acl;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    GetSecurityDescriptorControl(descriptor, &control, &revision);
    acl.isProtected = (control & SE_DACL_PROTECTED) != 0;
    for (DWORD i = 0; dacl && i < dacl->AceCount; ++i) {
        void *ace = nullptr;
        if (GetAce(dacl, i, &ace) && static_cast<ACE_HEADER *>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE)
            acl.allowed.push_back(sidText(&static_cast<ACCESS_ALLOWED_ACE *>(ace)->SidStart));
    }
    LPWSTR sddl = nullptr;
    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION,
                                                             &sddl, nullptr)) {
        acl.sddl = sddl;
        LocalFree(sddl);
    }
    LocalFree(descriptor);
    return acl;
}
#endif

struct HttpAnswer {
    int status = 0;
    QByteArray head;
    QByteArray body;
};

// The real hikari-ai over the real Hikari API, reached as an agent outside HikariSub reaches it.
class AiMcpTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_mcp_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void SetUp() override
    {
        fake.api = &api;
        AiSidecarHost::Options o;
        o.program = QStringLiteral(HIKARI_AI_RUNTIME_FILE);
        o.arguments = {QStringLiteral(HIKARI_AI_BUNDLE_FILE)};
        o.workingDirectory = dir.filePath(QStringLiteral("work"));
        o.buildId = HIKARI_AI_BUILD_ID;
        o.handshakeMs = 30'000;
        sidecar = std::make_unique<AiSidecarHost>(std::move(o), &api, hikari::api::Caller{"ai", "Hikari AI", false});
        server = std::make_unique<McpServer>([this] { return sidecar.get(); }, kToken);
        ASSERT_TRUE(server->listenLocal());
    }

    void TearDown() override
    {
        server.reset();
        sidecar->shutdown();
    }

    /// Each line the bridge wrote to the agent, parsed.
    std::vector<Json> bridged;
    std::unique_ptr<McpBridge> bridge(const QByteArray &token = kToken)
    {
        auto b = std::make_unique<McpBridge>([this, buffer = std::make_shared<QByteArray>()](const QByteArray &bytes) {
            *buffer += bytes;
            for (qsizetype end; (end = buffer->indexOf('\n')) >= 0; buffer->remove(0, end + 1))
                bridged.push_back(Json::parse(buffer->left(end).toStdString()).value_or(Json()));
        });
        QString error;
        EXPECT_TRUE(b->open({server->socketName(), token, 0}, &error)) << error.toStdString();
        return b;
    }

    /// The request's result; an empty object, failing the test, when the reply is an error or doesn't come.
    Json ask(McpBridge &b, const std::string &message)
    {
        const std::size_t before = bridged.size();
        std::string line = message;
        std::replace(line.begin(), line.end(), '\n', ' ');
        b.fromAgent(QByteArray::fromStdString(line + '\n'));
        EXPECT_TRUE(waitFor([&] { return bridged.size() > before; })) << message;
        const Json *result = bridged.size() > before ? bridged.back().find("result") : nullptr;
        EXPECT_TRUE(result) << (bridged.size() > before ? bridged.back().dump() : message);
        return result ? *result : *Json::parse(R"({"content": [{"text": "{}"}]})");
    }

    HttpAnswer post(const QByteArray &body, const QByteArray &headers, const QByteArray &method = "POST")
    {
        QTcpSocket socket;
        socket.connectToHost(QStringLiteral("127.0.0.1"), server->httpPort());
        EXPECT_TRUE(waitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }));
        socket.write(method + " /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(server->httpPort())
                     + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n"
                     + headers + "\r\n" + body);
        QByteArray raw;
        waitFor([&] {
            raw += socket.readAll();
            return socket.state() == QAbstractSocket::UnconnectedState;
        });
        raw += socket.readAll();
        HttpAnswer answer;
        const qsizetype split = raw.indexOf("\r\n\r\n");
        answer.head = raw.left(split);
        answer.body = raw.mid(split + 4);
        answer.status = raw.mid(9, 3).toInt();
        return answer;
    }

    static QByteArray authorised(const QByteArray &method, const QByteArray &more = {})
    {
        return "Authorization: Bearer " + kToken + "\r\nContent-Type: application/json\r\n"
               "MCP-Protocol-Version: 2026-07-28\r\nMcp-Method: " + method + "\r\n" + more;
    }

    QTemporaryDir dir;
    hikari::api::testing::FakeHost fake;
    hikari::api::Api api{fake.host()};
    std::unique_ptr<AiSidecarHost> sidecar;
    std::unique_ptr<McpServer> server;
};

TEST_F(AiMcpTest, AnAgentOnTheBridgeReadsEveryDocumentAndItsEditWaitsInTheReview)
{
    auto b = bridge();
    const Json initialized = ask(*b, R"({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
        "protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "codex", "version": "1"}}})");
    ASSERT_TRUE(initialized.find("serverInfo"));
    EXPECT_EQ(*initialized.find("protocolVersion"), Json("2025-11-25"));
    EXPECT_EQ(*initialized.find("serverInfo")->find("name"), Json("hikarisub"));
    b->fromAgent(R"({"jsonrpc": "2.0", "method": "notifications/initialized"})" "\n");

    const Json documents = ask(*b, R"({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
        "name": "documents_list"}})");
    ASSERT_TRUE(Json::parse(toolText(documents))->find("documents")) << toolText(documents);
    EXPECT_EQ(Json::parse(toolText(documents))->find("documents")->asArray().size(), 2u);

    const Json other = ask(*b, R"({"jsonrpc": "2.0", "id": 3, "method": "resources/read", "params": {
        "uri": "hikari://documents/2/lines"}})");
    ASSERT_TRUE(other.find("contents"));
    const Json lines = *Json::parse(other.find("contents")->asArray()[0].find("text")->asString());
    EXPECT_EQ(*lines.find("lines")->asArray()[0].find("text"), Json("other"));

    const Json revision = *Json::parse(toolText(ask(*b, R"({"jsonrpc": "2.0", "id": 4, "method": "tools/call",
        "params": {"name": "documents_get"}})")))->find("revision");
    const Json applied = ask(*b, R"({"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {
        "name": "document_apply", "arguments": {"revision": )" + revision.dump() + R"(, "name": "Fix",
        "edits": [{"op": "set_line", "line": 1, "fields": {"text": "x"}}]}}})");
    EXPECT_FALSE(applied.find("isError") && applied.find("isError")->asBool()) << toolText(applied);
    EXPECT_EQ(fake.commits, 1);
    const auto changes = fake.review.changes(*fake.workspace().editingTarget());
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0]->caller, "codex");
    EXPECT_EQ(changes[0]->name, "Fix");
}

TEST_F(AiMcpTest, TheLocalSocketDropsAClientWithoutTheToken)
{
    QLocalSocket socket;
    socket.connectToServer(server->socketName());
    ASSERT_TRUE(waitFor([&] { return socket.state() == QLocalSocket::ConnectedState; }));
    socket.write("not-the-token\n{\"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"ping\"}\n");
    EXPECT_TRUE(waitFor([&] { return socket.state() == QLocalSocket::UnconnectedState; }));
    EXPECT_TRUE(socket.readAll().isEmpty());
    EXPECT_EQ(sidecar->state(), AiSidecarHost::State::Idle);
}

TEST_F(AiMcpTest, StreamableHttpServesAModernRequestWithTheTokenFromThisOriginOnly)
{
    ASSERT_TRUE(server->listenHttp(0));
    const QByteArray list = "{\"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"tools/list\", \"params\": {"
                            + QByteArray::fromStdString(kModern) + "}}";
    const HttpAnswer ok = post(list, authorised("tools/list"));
    ASSERT_EQ(ok.status, 200) << ok.head.toStdString() << ok.body.toStdString();
    EXPECT_TRUE(ok.head.contains("Content-Type: application/json"));
    const Json reply = *Json::parse(ok.body.toStdString());
    EXPECT_EQ(*reply.find("result")->find("resultType"), Json("complete"));
    EXPECT_GE(reply.find("result")->find("tools")->asArray().size(), 25u);

    const QByteArray port = QByteArray::number(server->httpPort());
    EXPECT_EQ(post(list, authorised("tools/list", "Origin: http://127.0.0.1:" + port + "\r\n")).status, 200);
    EXPECT_EQ(post(list, authorised("tools/list", "Origin: https://evil.example\r\n")).status, 403);
    EXPECT_EQ(post(list, "Content-Type: application/json\r\n").status, 401);
    EXPECT_EQ(post(list, "Authorization: Bearer nope\r\nContent-Type: application/json\r\n").status, 401);
    EXPECT_EQ(post({}, authorised("tools/list"), "GET").status, 405);
    EXPECT_EQ(post(list, authorised("tools/call")).status, 400);
    EXPECT_EQ(post(R"({"jsonrpc": "2.0", "method": "notifications/initialized"})", authorised("x")).status, 202);
}

TEST_F(AiMcpTest, StreamableHttpRefusesAnotherHost)
{
    ASSERT_TRUE(server->listenHttp(0));
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), server->httpPort());
    ASSERT_TRUE(waitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }));
    socket.write("POST /mcp HTTP/1.1\r\nHost: rebound.example:80\r\nContent-Length: 2\r\n" + authorised("ping")
                 + "\r\n{}");
    QByteArray raw;
    waitFor([&] {
        raw += socket.readAll();
        return socket.state() == QAbstractSocket::UnconnectedState;
    });
    EXPECT_TRUE(raw.startsWith("HTTP/1.1 403")) << raw.toStdString();
}

TEST_F(AiMcpTest, CallsForAConnectionSeeEveryDocumentUnderTheClientsNameAndOnlyApplyWrites)
{
    const auto caller = server->callerFor(*Json::parse(R"({"connection": "local-1", "client": "Claude Code"})"),
                                          "lines.list");
    ASSERT_TRUE(caller);
    EXPECT_EQ(caller->label, "Claude Code");
    EXPECT_TRUE(caller->allDocuments);
    EXPECT_TRUE(caller->reviewed);
    const auto apply = server->callerFor(*Json::parse(R"({"connection": "local-2"})"), "document.apply");
    ASSERT_TRUE(apply);
    EXPECT_NE(apply->id, caller->id);
    const auto write = server->callerFor(*Json::parse(R"({"connection": "local-1"})"), "settings.set");
    ASSERT_FALSE(write);
    EXPECT_EQ(write.error().code, hikari::api::errors::kRefused);
    EXPECT_FALSE(server->callerFor(Json::object(), "lines.list"));
}

TEST_F(AiMcpTest, ConnectionsReadSkillsOnlyWhileTheGrantAllowsAndSeeItsProjects)
{
    const Json local = *Json::parse(R"({"connection": "local-1"})");
    EXPECT_FALSE(server->callerFor(local, "skills.list")->skills);
    std::optional<std::string> grant;
    server->setSkills([&grant] { return grant; });
    EXPECT_FALSE(server->callerFor(local, "skills.list")->skills);
    grant = "/work/show";
    const auto granted = server->callerFor(local, "skills.list");
    EXPECT_TRUE(granted->skills);
    EXPECT_EQ(granted->project, "/work/show");
    grant = std::string();
    EXPECT_TRUE(server->callerFor(local, "skills.read")->skills);
    EXPECT_EQ(server->callerFor(local, "skills.read")->project, "");
}

TEST_F(AiMcpTest, TheEndpointFileRoundTripsAndOnlyTheUserCanReadIt)
{
    QTemporaryDir dir;
    const QString file = mcpEndpointFile(dir.path());
    EXPECT_FALSE(readMcpEndpoint(file));
    const QByteArray token = newMcpToken();
    EXPECT_EQ(token.size(), 64);
    EXPECT_NE(token, newMcpToken());
    ASSERT_TRUE(writeMcpEndpoint(file, {QStringLiteral("hikarisub-mcp-1"), token, 4242}));
    const auto read = readMcpEndpoint(file);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->socket, QStringLiteral("hikarisub-mcp-1"));
    EXPECT_EQ(read->token, token);
    EXPECT_EQ(read->port, 4242);
#ifdef _WIN32
    const FileAcl created = aclOf(file);
    EXPECT_TRUE(created.isProtected);
    EXPECT_EQ(created.allowed, std::vector<std::wstring>{currentUser()});
#endif

    const QByteArray reset = newMcpToken();
    ASSERT_TRUE(writeMcpEndpoint(file, {QStringLiteral("hikarisub-mcp-1"), reset, 4242}));
    EXPECT_EQ(readMcpEndpoint(file)->token, reset);
#ifdef _WIN32
    EXPECT_EQ(aclOf(file).sddl, created.sddl);
#else
    EXPECT_EQ(QFile::permissions(file) & ~(QFile::ReadUser | QFile::WriteUser | QFile::ReadOwner | QFile::WriteOwner),
              QFile::Permissions());
#endif
}

TEST_F(AiMcpTest, ANewTokenDropsTheAgentsAndOnlyItConnects)
{
    ASSERT_TRUE(server->listenHttp(0));
    const QByteArray list = "{\"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"tools/list\", \"params\": {"
                            + QByteArray::fromStdString(kModern) + "}}";
    ASSERT_EQ(post(list, authorised("tools/list")).status, 200);
    const std::string initialize = R"({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
        "protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "codex", "version": "1"}}})";
    auto before = bridge();
    bool dropped = false;
    QObject::connect(before.get(), &McpBridge::finished, [&] { dropped = true; });
    ASSERT_TRUE(ask(*before, initialize).find("serverInfo"));

    const QByteArray token = newMcpToken();
    server->setToken(token);
    EXPECT_TRUE(waitFor([&] { return dropped; }));
    EXPECT_EQ(post(list, authorised("tools/list")).status, 401);
    QByteArray fresh = authorised("tools/list");
    fresh.replace(kToken, token);
    EXPECT_EQ(post(list, fresh).status, 200);

    McpBridge old([](const QByteArray &) {});
    bool refused = false;
    QObject::connect(&old, &McpBridge::finished, [&] { refused = true; });
    ASSERT_TRUE(old.open({server->socketName(), kToken, 0}));
    old.fromAgent(R"({"jsonrpc": "2.0", "id": 1, "method": "ping"})" "\n");
    EXPECT_TRUE(waitFor([&] { return refused; }));

    auto renewed = bridge(token);
    EXPECT_TRUE(ask(*renewed, initialize).find("serverInfo"));
}

TEST_F(AiMcpTest, TheBridgeSaysWhatToDoWhenNothingListens)
{
    McpBridge bridge([](const QByteArray &) {});
    QString error;
    EXPECT_FALSE(bridge.open({QString(), newMcpToken(), 0}, &error));
    EXPECT_NE(error.indexOf(QStringLiteral("Settings > AI")), -1);
    EXPECT_FALSE(bridge.open({QStringLiteral("hikarisub-mcp-nobody"), newMcpToken(), 0}, &error));
}

}
