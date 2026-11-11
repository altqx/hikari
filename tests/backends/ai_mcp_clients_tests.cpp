#include "hikari/application/ai_providers.h"
#include "hikari/backends/ai_mcp_clients.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace hikari::backends::ai;
using hikari::api::Json;
namespace app = hikari::application::ai;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

Json message(std::string_view text)
{
    return *Json::parse(text);
}

struct Keys final : app::KeyStore {
    std::map<std::string, std::string> stored;
    void read(const std::string &id, std::function<void(app::KeyResult)> done) override
    {
        const auto it = stored.find(id);
        done(it == stored.end() ? app::KeyResult(app::KeyError::NotFound) : app::KeyResult(it->second));
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<app::KeyError>)> done) override
    {
        stored[id] = key;
        done(std::nullopt);
    }
    void remove(const std::string &id, std::function<void(std::optional<app::KeyError>)> done) override
    {
        stored.erase(id);
        done(std::nullopt);
    }
};

const char *kList = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
const char *kListed = R"({"jsonrpc":"2.0","id":1,"result":{"tools":[
    {"name":"read","annotations":{"readOnlyHint":true}},
    {"name":"tidy","annotations":{"destructiveHint":false}},
    {"name":"wipe"}]}})";
const char *kCall = R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"wipe","arguments":{"all":true}}})";

}

TEST(McpTraffic, QtLearnsTheHintsFromTheServersOwnListAndFillsInMcpsDefaults)
{
    McpTraffic traffic;
    EXPECT_FALSE(traffic.fromSidecar(message(kList)));
    traffic.fromServer(message(kListed));
    EXPECT_EQ(traffic.tools(), (std::vector<std::string>{"read", "tidy", "wipe"}));
    EXPECT_TRUE(traffic.hints("read")->readOnly);
    EXPECT_FALSE(traffic.hints("tidy")->destructive);
    EXPECT_TRUE(traffic.hints("wipe")->destructive);
    EXPECT_FALSE(traffic.hints("other"));
}

TEST(McpTraffic, AListNobodyAskedForChangesNothing)
{
    McpTraffic traffic;
    traffic.fromServer(message(kListed));
    EXPECT_TRUE(traffic.tools().empty());
}

TEST(McpTraffic, AListInPagesReplacesTheOldOneOnlyOnceItsLastPageArrives)
{
    McpTraffic traffic;
    traffic.fromSidecar(message(kList));
    traffic.fromServer(message(kListed));
    traffic.fromSidecar(message(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})"));
    traffic.fromServer(message(R"({"jsonrpc":"2.0","id":3,"result":{"tools":[{"name":"a"}],"nextCursor":"p2"}})"));
    EXPECT_EQ(traffic.tools().size(), 3u);
    traffic.fromSidecar(message(R"({"jsonrpc":"2.0","id":4,"method":"tools/list","params":{"cursor":"p2"}})"));
    traffic.fromServer(message(R"({"jsonrpc":"2.0","id":4,"result":{"tools":[{"name":"b"}]}})"));
    EXPECT_EQ(traffic.tools(), (std::vector<std::string>{"a", "b"}));
}

TEST(McpTraffic, OnlyACallQtAdmittedReachesTheServerAndOnlyOnce)
{
    McpTraffic traffic;
    EXPECT_TRUE(traffic.fromSidecar(message(kCall)));
    traffic.admit("wipe", message(R"({"all":false})"));
    EXPECT_TRUE(traffic.fromSidecar(message(kCall)));
    traffic.admit("wipe", message(R"({"all":true})"));
    EXPECT_FALSE(traffic.fromSidecar(message(kCall)));
    EXPECT_TRUE(traffic.fromSidecar(message(kCall)));
}

TEST(McpTraffic, TheSidecarMayOnlyHandshakeListAndCall)
{
    McpTraffic traffic;
    for (const char *ok : {R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
                           R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
                           R"({"jsonrpc":"2.0","id":9,"method":"ping"})", R"({"jsonrpc":"2.0","id":5,"result":{}})"})
        EXPECT_FALSE(traffic.fromSidecar(message(ok))) << ok;
    for (const char *refused : {R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{}})",
                                R"({"jsonrpc":"2.0","id":1,"method":"sampling/createMessage"})", R"([1])"})
        EXPECT_TRUE(traffic.fromSidecar(message(refused))) << refused;
}

TEST(McpResponseReader, ReadsAJsonAnswerAndServerSentEventsAsTheyArrive)
{
    std::vector<Json> seen;
    McpResponseReader json([&](const Json &m) { seen.push_back(m); });
    json.push(R"({"jsonrpc":"2.0",)");
    EXPECT_TRUE(seen.empty());
    json.push(R"("id":1,"result":{}})");
    ASSERT_EQ(seen.size(), 1u);

    McpResponseReader events([&](const Json &m) { seen.push_back(m); });
    events.push("event: message\ndata: {\"jsonrpc\":\"2.0\",\"id\":2,");
    events.push("\"result\":{}}\n\ndata: {\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{}}\n\n");
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(*seen[2].find("id"), Json(3));
}

TEST(McpServerSettings, ServersSurviveTheirSettingsRowAndBadRowsAreDropped)
{
    McpServerEntry stdio{.id = "notes", .name = "Notes", .command = QStringLiteral("npx"),
                         .arguments = {QStringLiteral("-y"), QStringLiteral("notes-mcp")}, .trusted = true,
                         .approved = {"tidy"}, .revision = 3};
    McpServerEntry http{.id = "remote", .name = "Remote", .transport = McpServerEntry::Transport::Http,
                        .url = QStringLiteral("https://mcp.example.com/mcp")};
    Json rows = mcpServersToJson({stdio, http});
    rows.push(Json::object({{"id", "x"}, {"name", "No command"}, {"transport", "stdio"}}));
    rows.push(stdio.toJson());
    EXPECT_EQ(mcpServersFromJson(rows), (std::vector<McpServerEntry>{stdio, http}));
    EXPECT_EQ(newMcpServerId("My Notes!", {stdio}), "my-notes");
    EXPECT_EQ(newMcpServerId("Notes", {stdio}), "notes-2");
    EXPECT_EQ(newMcpServerId("日本", {}), "server");
    const McpSecrets secrets{{{QStringLiteral("TOKEN"), QStringLiteral("t")}}, QStringLiteral("b")};
    const McpSecrets back = McpSecrets::fromJson(secrets.toJson());
    EXPECT_EQ(back.environment, secrets.environment);
    EXPECT_EQ(back.bearer, secrets.bearer);
}

class McpClientsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_mcp_clients_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    std::optional<std::expected<Json, std::pair<int, std::string>>> call(const std::string &method, Json params)
    {
        std::optional<std::expected<Json, std::pair<int, std::string>>> result;
        broker.call(method, params, [&](auto r) { result = std::move(r); });
        return result;
    }

    std::vector<Json> lines(std::int64_t process)
    {
        QByteArray all;
        for (const auto &[method, params] : notes)
            if (method == kAgentOutput && params.find("process")->asInt() == process)
                all += QByteArray::fromBase64(QByteArray::fromStdString(params.find("data")->asString()));
        std::vector<Json> out;
        for (const QByteArray &l : all.split('\n'))
            if (auto parsed = Json::parse(l.toStdString()))
                out.push_back(*parsed);
        return out;
    }

    void send(std::int64_t process, std::string_view text)
    {
        ASSERT_TRUE(*call(kAgentWrite, Json::object({{"process", process},
                                                     {"data", QByteArray::fromStdString(std::string(text) + "\n")
                                                                  .toBase64()
                                                                  .toStdString()}})));
    }

    bool exited(std::int64_t process)
    {
        return std::ranges::any_of(notes, [&](const auto &n) {
            return n.first == kAgentExit && n.second.find("process")->asInt() == process;
        });
    }

    void SetUp() override
    {
        clients.attach(host, broker);
        broker.setNotify([this](const std::string &method, Json params) { notes.emplace_back(method, std::move(params)); });
    }

    Keys keys;
    AiSidecarHost host{AiSidecarHost::Options{}, nullptr, hikari::api::Caller{"ai", "AI"}};
    AgentBroker broker;
    McpClients clients{keys, QCoreApplication::applicationDirPath()};
    std::vector<std::pair<std::string, Json>> notes;
    McpServerEntry fake{.id = "fake", .name = "Fake", .command = QStringLiteral(HIKARI_FAKE_AGENT)};
};

TEST_F(McpClientsTest, QtStartsAStdioServerUnsandboxedWithItsSecretsAndOnlyItsOwnArguments)
{
    keys.stored[mcpSecretEntry("fake")] = McpSecrets{{{QStringLiteral("HIKARI_TEST_SECRET"), QStringLiteral("s3")}}, {}}.toJson();
    fake.arguments = {QStringLiteral("--from-settings")};
    clients.setServers({fake});
    EXPECT_FALSE(*call(kAgentStart, Json::object({{"process", 1}, {"agent", "mcp:fake"}, {"args", Json::array({"--x"})}})));
    ASSERT_TRUE(*call(kAgentStart, Json::object({{"process", 2}, {"agent", "mcp:fake"}})));
    ASSERT_TRUE(waitFor([&] { return !lines(2).empty(); }));
    const Json &started = *lines(2)[0].find("started");
    EXPECT_EQ(*started.find("args"), Json::array({"--from-settings"}));
    EXPECT_EQ(*started.find("secret"), Json("s3"));
}

TEST_F(McpClientsTest, ACallQtDidntAdmitIsAnsweredWithAnErrorAndNeverReachesTheServer)
{
    clients.setServers({fake});
    ASSERT_TRUE(*call(kAgentStart, Json::object({{"process", 1}, {"agent", "mcp:fake"}})));
    ASSERT_TRUE(waitFor([&] { return !lines(1).empty(); }));
    send(1, R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"wipe","arguments":{},"exit":3}})");
    ASSERT_TRUE(waitFor([&] { return lines(1).size() == 2; }));
    EXPECT_EQ(*lines(1)[1].find("id"), Json(7));
    EXPECT_TRUE(lines(1)[1].find("error"));
    EXPECT_FALSE(exited(1));

    send(1, R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"emit":{"jsonrpc":"2.0","id":1,"result":{"tools":[{"name":"wipe"}]}}}})");
    ASSERT_TRUE(waitFor([&] { return clients.tools("fake") == std::vector<std::string>{"wipe"}; }));
}

TEST_F(McpClientsTest, ServersEndWithTheSidecarAndARemovedOneCantStart)
{
    clients.setServers({fake});
    ASSERT_TRUE(*call(kAgentStart, Json::object({{"process", 1}, {"agent", "mcp:fake"}})));
    ASSERT_TRUE(waitFor([&] { return !lines(1).empty(); }));
    emit host.lost(QStringLiteral("crashed"));
    ASSERT_TRUE(waitFor([&] { return exited(1); }));
    EXPECT_EQ(broker.running(), 0u);
    clients.setServers({});
    EXPECT_FALSE(*call(kAgentStart, Json::object({{"process", 2}, {"agent", "mcp:fake"}})));
}

TEST_F(McpClientsTest, AnHttpServerIsReachedAtItsOwnUrlWithItsTokenAndOnlyAdmittedCalls)
{
    keys.stored[mcpSecretEntry("remote")] = McpSecrets{{}, QStringLiteral("tok")}.toJson();
    clients.setServers({{.id = "remote", .name = "Remote", .transport = McpServerEntry::Transport::Http,
                         .url = QStringLiteral("https://mcp.example.com/mcp")}});
    std::optional<RelayTargetResult> found;
    const RelayTargets targets = clients.relayTargets([](const Json &, std::function<void(RelayTargetResult)> done) {
        done(std::unexpected(app::Error{app::ErrorKind::Rejected, 0, "next"}));
    });
    targets(Json::object({{"kind", "mcp"}, {"id", "remote"}}), [&](RelayTargetResult r) { found = std::move(r); });
    ASSERT_TRUE(found && *found);
    EXPECT_EQ((*found)->base, QUrl(QStringLiteral("https://mcp.example.com/mcp")));
    EXPECT_TRUE((*found)->exactUrl);
    EXPECT_EQ((*found)->key, "tok");
    EXPECT_EQ((*found)->headers, (std::vector<std::pair<QByteArray, QByteArray>>{{"authorization", "Bearer hikari-key"}}));
    EXPECT_TRUE((*found)->guardBody(kCall));
    EXPECT_FALSE((*found)->guardBody(kList));

    found.reset();
    targets(Json::object({{"kind", "mcp"}, {"id", "gone"}}), [&](RelayTargetResult r) { found = std::move(r); });
    ASSERT_TRUE(found);
    EXPECT_FALSE(*found);
}
