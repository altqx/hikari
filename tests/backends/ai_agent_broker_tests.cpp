#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_signin.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

using namespace hikari::backends::ai;
using hikari::api::Json;

namespace {

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

QStringList strings(std::initializer_list<const char *> list)
{
    QStringList out;
    for (const char *s : list)
        out << QString::fromLatin1(s);
    return out;
}

TEST(ClaudeArguments, BypassExtraFoldersPluginsSettingsAndOutsideServersAreRefused)
{
    const QStringList sdk = strings({"--output-format", "stream-json", "--verbose", "--input-format", "stream-json",
                                     "--tools=", "--strict-mcp-config", "--permission-mode=default",
                                     R"(--mcp-config={"mcpServers":{"hikari":{"type":"sdk","name":"hikari"}}})"});
    EXPECT_EQ(refusedClaudeArguments(sdk), QString());
    const auto with = [&](std::initializer_list<const char *> extra) { return refusedClaudeArguments(sdk + strings(extra)); };
    EXPECT_FALSE(with({"--dangerously-skip-permissions"}).isEmpty());
    EXPECT_FALSE(with({"--allow-dangerously-skip-permissions"}).isEmpty());
    EXPECT_FALSE(with({"--permission-mode=bypassPermissions"}).isEmpty());
    EXPECT_FALSE(with({"--permission-mode", "bypassPermissions"}).isEmpty());
    EXPECT_FALSE(with({"--add-dir=/home"}).isEmpty());
    EXPECT_FALSE(with({"--plugin-dir", "/tmp/p"}).isEmpty());
    EXPECT_FALSE(with({"--settings={}"}).isEmpty());
    EXPECT_FALSE(with({"--tools=Bash"}).isEmpty());
    EXPECT_FALSE(with({R"(--mcp-config={"mcpServers":{"x":{"type":"stdio","command":"curl"}}})"}).isEmpty());
    EXPECT_FALSE(with({"--mcp-config=/tmp/servers.json"}).isEmpty());
    EXPECT_FALSE(refusedClaudeArguments(strings({"--tools=", "--output-format", "stream-json"})).isEmpty());
    EXPECT_FALSE(refusedClaudeArguments(strings({"--strict-mcp-config"})).isEmpty());
}

struct Keys final : hikari::application::ai::KeyStore {
    void read(const std::string &, std::function<void(hikari::application::ai::KeyResult)> done) override
    {
        done(hikari::application::ai::KeyError::NotFound);
    }
    void write(const std::string &, const std::string &, std::function<void(std::optional<hikari::application::ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
    void remove(const std::string &, std::function<void(std::optional<hikari::application::ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
};

void writeFile(const QString &path, const QByteArray &content, bool executable)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFile::ExeOwner);
}

TEST(AgentSpecs, ClaudeMayWriteOnlyItsOwnStateAndTheSidecarSetsOnlyItsMarkers)
{
    QTemporaryDir dir;
    const QString program = dir.filePath(QStringLiteral("versions/2.1.317"));
    writeFile(program, "ELF", true);
    const AgentSpec spec = claudeAgent(program, QStringLiteral("/h"), dir.filePath(QStringLiteral("work")));
    EXPECT_EQ(spec.program, QFileInfo(program).canonicalFilePath());
    EXPECT_EQ(spec.profile.readExecute, QStringList{QFileInfo(program).canonicalPath()});
    EXPECT_EQ(spec.profile.readWrite,
              (QStringList{QStringLiteral("/h/.claude"), QStringLiteral("/h/.claude.json"), dir.filePath(QStringLiteral("work"))}));
    EXPECT_EQ(spec.profile.network, hikari::backends::sandbox::Network::Any);
    EXPECT_TRUE(QFileInfo(dir.filePath(QStringLiteral("work/tmp"))).isDir());
    EXPECT_EQ(spec.environment.value(QStringLiteral("CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC")), QStringLiteral("1"));
    EXPECT_TRUE(spec.sidecarEnvironment.contains(QStringLiteral("CLAUDE_CODE_ENTRYPOINT")));
    EXPECT_FALSE(spec.sidecarEnvironment.contains(QStringLiteral("ANTHROPIC_BASE_URL")));
    EXPECT_FALSE(spec.refuse(QStringList{QStringLiteral("--dangerously-skip-permissions")}).isEmpty());
}

TEST(AgentSpecs, CodexRunsTheNativeBinaryBehindNpmsLauncherWithHikarisOwnHome)
{
    QTemporaryDir dir;
    const QString launcher = dir.filePath(QStringLiteral("node_modules/@openai/codex/bin/codex.js"));
    writeFile(launcher, "#!/usr/bin/env node\n", true);
#ifdef _WIN32
    const QString native = dir.filePath(QStringLiteral("node_modules/@openai/codex-win32-x64/vendor/x86_64-pc-windows-msvc/bin/codex.exe"));
#else
    const QString native = dir.filePath(QStringLiteral("node_modules/@openai/codex-linux-x64/vendor/x86_64-unknown-linux-musl/bin/codex"));
#endif
    writeFile(native, "ELF", true);
    Keys keys;
    const auto lease = std::make_shared<CodexLease>(keys, dir.filePath(QStringLiteral("home")));
    const auto spec = codexAgent(launcher, dir.filePath(QStringLiteral("home")), dir.filePath(QStringLiteral("work")), lease);
    ASSERT_TRUE(spec);
    EXPECT_EQ(spec->program, QFileInfo(native).canonicalFilePath());
    EXPECT_EQ(spec->arguments, QStringList{QStringLiteral("app-server")});
    EXPECT_EQ(spec->environment.value(QStringLiteral("CODEX_HOME")),
              QDir::toNativeSeparators(dir.filePath(QStringLiteral("home"))));
    EXPECT_TRUE(QFileInfo(dir.filePath(QStringLiteral("home"))).isDir());
    EXPECT_TRUE(spec->guard);
    EXPECT_TRUE(spec->refuse({}).isEmpty());
    EXPECT_FALSE(spec->refuse(QStringList{QStringLiteral("--dangerously-bypass-approvals-and-sandbox")}).isEmpty());
    EXPECT_EQ(spec->channelArguments(QStringLiteral("http://127.0.0.1:9/mcp")),
              strings({"-c", R"(mcp_servers.hikari.url="http://127.0.0.1:9/mcp")", "-c",
                       R"(mcp_servers.hikari.bearer_token_env_var="HIKARI_MCP_TOKEN")", "-c",
                       R"(mcp_servers.hikari.default_tools_approval_mode="approve")"}));
    std::optional<QString> refused;
    spec->before([&](QString error) { refused = std::move(error); });
    EXPECT_TRUE(refused && !refused->isEmpty());

    EXPECT_EQ(codexAgent(native, dir.path(), dir.path(), lease)->program, QFileInfo(native).canonicalFilePath());
    QFile::remove(native);
    EXPECT_FALSE(codexAgent(launcher, dir.path(), dir.path(), lease));
}

class AgentBrokerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_agent_broker_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    AgentSpec spec(bool guarded = false)
    {
        AgentSpec s;
        s.program = QStringLiteral(HIKARI_FAKE_AGENT);
        s.arguments = {QStringLiteral("app-server")};
        s.workingDirectory = dir.path();
        s.profile = {.name = QStringLiteral("HikariSub.Test.Agent"),
                     .readExecute = {QFileInfo(s.program).absolutePath()},
                     .readWrite = {dir.path()},
                     .network = hikari::backends::sandbox::Network::Any};
        s.sidecarEnvironment = {QStringLiteral("CLAUDE_CODE_ENTRYPOINT")};
        if (guarded)
            s.guard = codexGuard;
        return s;
    }

    std::optional<std::expected<Json, std::pair<int, std::string>>> call(const std::string &method, Json params)
    {
        std::optional<std::expected<Json, std::pair<int, std::string>>> result;
        broker.call(method, params, [&](auto r) { result = std::move(r); });
        return result;
    }

    std::uint64_t start(const std::string &agent, Json args = Json::array(), Json env = Json::object())
    {
        const std::uint64_t process = next++;
        const auto started = call(kAgentStart, Json::object({{"process", process}, {"agent", agent},
                                                             {"args", std::move(args)}, {"env", std::move(env)}}));
        EXPECT_TRUE(started && *started);
        return process;
    }

    void send(std::uint64_t process, const std::string &text)
    {
        ASSERT_TRUE(*call(kAgentWrite, Json::object({{"process", process},
                                                     {"data", QByteArray::fromStdString(text + "\n").toBase64().toStdString()}})));
    }

    /// The lines the agent printed on stdout so far.
    std::vector<Json> lines(std::uint64_t process)
    {
        QByteArray all;
        for (const auto &[method, params] : notes)
            if (method == kAgentOutput && params.find("process")->asInt() == static_cast<std::int64_t>(process) &&
                params.find("stream")->asString() == "stdout")
                all += QByteArray::fromBase64(QByteArray::fromStdString(params.find("data")->asString()));
        std::vector<Json> out;
        for (const QByteArray &l : all.split('\n'))
            if (auto parsed = Json::parse(l.toStdString()))
                out.push_back(*parsed);
        return out;
    }

    std::uint64_t startReady(const std::string &agent)
    {
        const std::uint64_t process = start(agent);
        EXPECT_TRUE(waitFor([&] { return !lines(process).empty(); }));
        return process;
    }

    std::optional<Json> withId(std::uint64_t process, const Json &id)
    {
        for (const Json &l : lines(process))
            if (const Json *i = l.find("id"); i && *i == id)
                return l;
        return std::nullopt;
    }

    std::optional<int> exitCode(std::uint64_t process)
    {
        for (const auto &[method, params] : notes)
            if (method == kAgentExit && params.find("process")->asInt() == static_cast<std::int64_t>(process))
                return static_cast<int>(params.find("code")->asInt());
        return std::nullopt;
    }

    void SetUp() override
    {
        broker.setNotify([this](const std::string &method, Json params) { notes.emplace_back(method, std::move(params)); });
    }

    QTemporaryDir dir;
    AgentBroker broker;
    std::vector<std::pair<std::string, Json>> notes;
    std::uint64_t next = 1;
};

TEST_F(AgentBrokerTest, QtPicksTheProgramAndOnlyListedVariablesComeFromTheSidecar)
{
    broker.add("fake", spec());
    EXPECT_FALSE(*call(kAgentStart, Json::object({{"process", 50}, {"agent", "bash"}})));
    EXPECT_FALSE(*call(kAgentStart, Json::object({{"agent", "fake"}})));
    const std::uint64_t process = start("fake", Json::array({"--x"}),
                                        Json::object({{"CLAUDE_CODE_ENTRYPOINT", "sdk-ts"}, {"HIKARI_TEST_SECRET", "s"}}));
    EXPECT_FALSE(*call(kAgentStart, Json::object({{"process", process}, {"agent", "fake"}})));
    EXPECT_EQ(broker.running("fake"), 1u);
    EXPECT_EQ(broker.running("claude"), 0u);
    send(process, R"({"hello":1})");
    ASSERT_TRUE(waitFor([&] { return lines(process).size() == 2; }));
    EXPECT_EQ(lines(process)[0], Json::object({{"started", Json::object({{"args", Json::array({"app-server", "--x"})},
                                                                         {"entry", "sdk-ts"},
                                                                         {"secret", ""},
                                                                         {"mcpToken", ""}})}}));
    EXPECT_EQ(lines(process)[1], Json::object({{"hello", 1}}));
    ASSERT_TRUE(*call(kAgentCloseInput, Json::object({{"process", process}})));
    ASSERT_TRUE(waitFor([&] { return exitCode(process).has_value(); }));
    EXPECT_EQ(*exitCode(process), 0);
    EXPECT_EQ(broker.running(), 0u);
    EXPECT_EQ(broker.running("fake"), 0u);
}

TEST_F(AgentBrokerTest, RefusedArgumentsStartNothing)
{
    AgentSpec s = spec();
    s.refuse = refusedClaudeArguments;
    broker.add("claude", s);
    const auto refused = call(kAgentStart, Json::object({{"process", 1}, {"agent", "claude"},
                                                         {"args", Json::array({"--add-dir=/"})}}));
    ASSERT_FALSE(*refused);
    EXPECT_EQ(refused->error(), (std::pair<int, std::string>{rpc::kInvalidParams, "--add-dir isn't allowed"}));
    EXPECT_EQ(broker.running(), 0u);
}

TEST_F(AgentBrokerTest, KillingEndsTheAgentAndReportsItsExit)
{
    broker.add("fake", spec());
    const std::uint64_t process = start("fake");
    ASSERT_TRUE(waitFor([&] { return !lines(process).empty(); }));
    ASSERT_TRUE(*call(kAgentKill, Json::object({{"process", process}})));
    EXPECT_EQ(exitCode(process), -1);
    EXPECT_FALSE(*call(kAgentWrite, Json::object({{"process", process}, {"data", ""}})));
    const std::uint64_t second = start("fake");
    ASSERT_TRUE(waitFor([&] { return !lines(second).empty(); }));
    broker.killAll();
    EXPECT_EQ(broker.running(), 0u);
}

TEST_F(AgentBrokerTest, CodexThreadsAreReadOnlyAndOtherMethodsAreRefused)
{
    broker.add("codex", spec(true));
    const std::uint64_t process = startReady("codex");
    send(process, R"({"id":1,"method":"thread/start","params":{"sandbox":"danger-full-access","approvalPolicy":"never","config":{"mcp_servers":{}},"approvalsReviewer":"guardian","cwd":"/w"}})");
    send(process, R"({"id":2,"method":"turn/start","params":{"threadId":"t","input":[],"sandboxPolicy":{"type":"dangerFullAccess"}}})");
    send(process, R"({"id":3,"method":"command/exec","params":{"command":["sh"]}})");
    send(process, R"({"id":4,"method":"fs/writeFile","params":{}})");
    ASSERT_TRUE(waitFor([&] { return lines(process).size() == 5; }));
    EXPECT_EQ(withId(process, 3), Json::object({{"id", 3}, {"error", Json::object({{"code", rpc::kMethodNotFound},
                                                                                    {"message", "Hikari doesn't let the AI use command/exec"}})}}));
    EXPECT_TRUE(withId(process, 4)->find("error"));
    const Json thread = *withId(process, 1)->find("params");
    EXPECT_EQ(*thread.find("sandbox"), Json("read-only"));
    EXPECT_EQ(*thread.find("approvalPolicy"), Json("untrusted"));
    EXPECT_FALSE(thread.find("config"));
    EXPECT_FALSE(thread.find("approvalsReviewer"));
    EXPECT_EQ(*thread.find("cwd"), Json("/w"));
    EXPECT_EQ(*withId(process, 2)->find("params")->find("sandboxPolicy"),
              Json::object({{"type", "readOnly"}, {"networkAccess", false}}));
}

TEST_F(AgentBrokerTest, QtDeclinesCodexApprovalsAndPassesOnlyAnswersToForwardedRequests)
{
    broker.add("codex", spec(true));
    const std::uint64_t process = startReady("codex");
    const auto agentSends = [&](int id, const std::string &message) {
        send(process, R"({"id":)" + std::to_string(id) + R"(,"method":"turn/interrupt","params":{"emit":)" + message + "}}");
    };
    agentSends(1, R"({"id":"a","method":"item/commandExecution/requestApproval","params":{}})");
    agentSends(2, R"({"id":"b","method":"execCommandApproval","params":{}})");
    agentSends(3, R"({"id":"c","method":"item/permissions/requestApproval","params":{}})");
    agentSends(4, R"({"id":"d","method":"item/tool/call","params":{}})");
    ASSERT_TRUE(waitFor([&] { return lines(process).size() == 5; }));
    EXPECT_EQ(withId(process, "a"), Json::object({{"id", "a"}, {"result", Json::object({{"decision", "decline"}})}}));
    EXPECT_EQ(*withId(process, "b")->find("result")->find("decision")->find("denied")->find("rejection"),
              Json("Hikari's AI can't run commands or change files."));
    EXPECT_EQ(withId(process, "c"),
              Json::object({{"id", "c"}, {"result", Json::object({{"permissions", Json::object()}, {"scope", "turn"}})}}));
    EXPECT_EQ(*withId(process, "d")->find("method"), Json("item/tool/call"));

    send(process, R"({"id":"zz","result":{"decision":"accept"}})");
    send(process, R"({"id":"d","result":{"ok":true}})");
    send(process, R"({"id":"d","result":{"again":true}})");
    ASSERT_TRUE(waitFor([&] { return lines(process).size() == 6; }));
    send(process, R"({"id":9,"method":"turn/interrupt","params":{}})");
    ASSERT_TRUE(waitFor([&] { return withId(process, 9).has_value(); }));
    const std::vector<Json> out = lines(process);
    ASSERT_EQ(out.size(), 7u);
    EXPECT_EQ(out[5], Json::object({{"id", "d"}, {"result", Json::object({{"ok", true}})}}));
}

TEST_F(AgentBrokerTest, AChannelAgentGetsItsOwnToolsChannelForTheSessionUntilItExits)
{
    AgentSpec s = spec();
    s.channelArguments = [](const QString &url) { return QStringList{QStringLiteral("--tools-url=") + url}; };
    broker.add("fake", s);
    const auto refused = call(kAgentStart, Json::object({{"process", 1}, {"agent", "fake"}}));
    ASSERT_FALSE(*refused);
    EXPECT_EQ(refused->error().first, rpc::kInvalidParams);

    const auto started = call(kAgentStart, Json::object({{"process", 2}, {"agent", "fake"}, {"session", "t1"},
                                                         {"args", Json::array({"--x"})}}));
    ASSERT_TRUE(started && *started);
    ASSERT_TRUE(waitFor([&] { return !lines(2).empty(); }));
    const Json report = *lines(2)[0].find("started");
    const std::string url = report.find("args")->asArray()[1].asString();
    ASSERT_TRUE(url.starts_with("--tools-url=http://127.0.0.1:")) << url;
    EXPECT_EQ(report.find("args")->asArray()[2], Json("--x"));
    EXPECT_EQ(report.find("mcpToken")->asString().size(), 64u);
    const QUrl tools(QString::fromStdString(url.substr(std::string_view("--tools-url=").size())));
    EXPECT_EQ(tools.path(), QStringLiteral("/mcp"));

    const auto opens = [&] {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, quint16(tools.port()));
        return socket.waitForConnected(2000);
    };
    EXPECT_TRUE(opens());
    ASSERT_TRUE(*call(kAgentKill, Json::object({{"process", 2}})));
    EXPECT_FALSE(opens());
}

TEST_F(AgentBrokerTest, AnAgentStartsOnlyAfterItsBeforeHookAndItsAfterHookRunsOnceItEnds)
{
    std::function<void(QString)> go;
    int afters = 0;
    AgentSpec s = spec();
    s.before = [&](std::function<void(QString)> g) { go = std::move(g); };
    s.after = [&] { ++afters; };
    broker.add("fake", s);

    std::optional<std::expected<Json, std::pair<int, std::string>>> first;
    broker.call(kAgentStart, Json::object({{"process", 1}, {"agent", "fake"}}), [&](auto r) { first = std::move(r); });
    EXPECT_FALSE(first);
    std::exchange(go, {})(QStringLiteral("Sign in first"));
    ASSERT_TRUE(first && !*first);
    EXPECT_EQ(first->error().second, "Sign in first");
    EXPECT_EQ(broker.running(), 0u);
    EXPECT_EQ(afters, 0);

    std::optional<std::expected<Json, std::pair<int, std::string>>> second;
    broker.call(kAgentStart, Json::object({{"process", 2}, {"agent", "fake"}}), [&](auto r) { second = std::move(r); });
    ASSERT_TRUE(*call(kAgentKill, Json::object({{"process", 2}})));
    std::exchange(go, {})(QString());
    ASSERT_TRUE(second && !*second);
    EXPECT_EQ(afters, 1);

    broker.call(kAgentStart, Json::object({{"process", 3}, {"agent", "fake"}}), [](auto) {});
    std::exchange(go, {})(QString());
    ASSERT_TRUE(waitFor([&] { return !lines(3).empty(); }));
    EXPECT_EQ(afters, 1);
    ASSERT_TRUE(*call(kAgentCloseInput, Json::object({{"process", 3}})));
    ASSERT_TRUE(waitFor([&] { return exitCode(3).has_value(); }));
    EXPECT_EQ(afters, 2);
}

#ifndef _WIN32
TEST_F(AgentBrokerTest, AnAgentRunsInItsOwnSandbox)
{
    AgentSpec s = spec();
    s.profile.readExecute = {dir.path()};
    broker.add("fake", s);
    const std::uint64_t process = start("fake");
    ASSERT_TRUE(waitFor([&] { return exitCode(process).has_value(); }));
    EXPECT_TRUE(lines(process).empty());
}
#endif

}
