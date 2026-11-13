#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_mcp_bridge.h"
#include "hikari/backends/ai_mcp_server.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_signin.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QLibraryInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

using namespace hikari::backends::ai;
using hikari::api::Json;

namespace {

#ifdef _WIN32
// MUST KILL: sandbox grants are never revoked, so a test can't grant Qt's own folder; agents load a copy instead.
class QtCopy
{
public:
    QtCopy() : m_path(qgetenv("PATH"))
    {
        HMODULE modules[1024];
        DWORD bytes = 0;
        if (!m_dir.isValid() || !EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &bytes))
            return;
        const QString qtBin = QDir::cleanPath(QLibraryInfo::path(QLibraryInfo::BinariesPath));
        for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
            wchar_t name[MAX_PATH];
            const DWORD length = GetModuleFileNameW(modules[i], name, MAX_PATH);
            const QFileInfo module(QDir::fromNativeSeparators(QString::fromWCharArray(name, int(length))));
            if (module.absolutePath().compare(qtBin, Qt::CaseInsensitive) == 0 &&
                !QFile::copy(module.absoluteFilePath(), m_dir.filePath(module.fileName())))
                return;
        }
        qputenv("PATH", QDir::toNativeSeparators(m_dir.path()).toLocal8Bit() + ';' + m_path);
        m_ready = true;
    }
    ~QtCopy() { qputenv("PATH", m_path); }
    QtCopy(const QtCopy &) = delete;
    QtCopy &operator=(const QtCopy &) = delete;

    bool ready() const { return m_ready; }
    QString folder() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_path;
    bool m_ready = false;
};
#endif

void ensureApp()
{
    static int argc = 1;
    static char name[] = "hikari_backends_ai_agent_broker_tests";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
}

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
    EXPECT_EQ(spec.profile->readExecute, QStringList{QFileInfo(program).canonicalPath()});
    EXPECT_EQ(spec.profile->readWrite,
              (QStringList{QStringLiteral("/h/.claude"), QStringLiteral("/h/.claude.json"), dir.filePath(QStringLiteral("work"))}));
    EXPECT_EQ(spec.profile->network, hikari::backends::sandbox::Network::Any);
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
    const QString bridge = dir.filePath(QStringLiteral("app/HikariSub.exe"));
    const auto spec =
        codexAgent(launcher, dir.filePath(QStringLiteral("home")), dir.filePath(QStringLiteral("work")), lease, bridge);
    ASSERT_TRUE(spec);
    EXPECT_EQ(spec->program, QFileInfo(native).canonicalFilePath());
    EXPECT_EQ(spec->arguments, (QStringList{QStringLiteral("app-server"), QStringLiteral("-c"),
                                            QStringLiteral("features.multi_agent=false"), QStringLiteral("-c"),
                                            QStringLiteral("features.multi_agent_v2=false")}));
    EXPECT_EQ(spec->environment.value(QStringLiteral("CODEX_HOME")),
              QDir::toNativeSeparators(dir.filePath(QStringLiteral("home"))));
    EXPECT_TRUE(QFileInfo(dir.filePath(QStringLiteral("home"))).isDir());
    EXPECT_TRUE(spec->guard);
    EXPECT_TRUE(spec->refuse({}).isEmpty());
    EXPECT_FALSE(spec->refuse(QStringList{QStringLiteral("--dangerously-bypass-approvals-and-sandbox")}).isEmpty());
    EXPECT_EQ(spec->channelArguments({.url = QStringLiteral("http://127.0.0.1:9/mcp")}),
              strings({"-c", R"(mcp_servers.hikari.url="http://127.0.0.1:9/mcp")", "-c",
                       R"(mcp_servers.hikari.bearer_token_env_var="HIKARI_MCP_TOKEN")", "-c",
                       R"(mcp_servers.hikari.default_tools_approval_mode="approve")", "-c",
                       "mcp_servers.hikari.tool_timeout_sec=86400"}));
    const QString command = QStringLiteral("mcp_servers.hikari.command=\"%1\"")
                                .arg(QDir::toNativeSeparators(bridge).replace(QLatin1Char('\\'), QLatin1String("\\\\")));
    EXPECT_EQ(spec->channelArguments({.pipe = QStringLiteral(R"(\\.\pipe\hikarisub-ch-1)")}),
              QStringList({QStringLiteral("-c"), command}) +
                  strings({"-c", R"(mcp_servers.hikari.args=["--mcp","--channel","\\\\.\\pipe\\hikarisub-ch-1"])", "-c",
                           R"(mcp_servers.hikari.env_vars=["HIKARI_MCP_TOKEN"])", "-c",
                           R"(mcp_servers.hikari.default_tools_approval_mode="approve")", "-c",
                           "mcp_servers.hikari.tool_timeout_sec=86400"}));
#ifdef _WIN32
    EXPECT_TRUE(spec->profile->readExecute.contains(dir.filePath(QStringLiteral("app"))));
#else
    EXPECT_FALSE(spec->profile->readExecute.contains(dir.filePath(QStringLiteral("app"))));
#endif
    std::optional<QString> refused;
    spec->before([&](QString error) { refused = std::move(error); });
    EXPECT_TRUE(refused && !refused->isEmpty());

    EXPECT_EQ(codexAgent(native, dir.path(), dir.path(), lease, bridge)->program, QFileInfo(native).canonicalFilePath());
    QFile::remove(native);
    EXPECT_FALSE(codexAgent(launcher, dir.path(), dir.path(), lease, bridge));
}

TEST(AgentSpecs, AnAcpAgentWritesOnlyItsOwnStateAndTakesNoArgumentsFromTheSidecar)
{
    QTemporaryDir dir;
    const QString program = dir.filePath(QStringLiteral("bin/opencode"));
    writeFile(program, "ELF", true);
    const QString bridge = dir.filePath(QStringLiteral("app/bin/hikarisub"));
    const QString work = dir.filePath(QStringLiteral("work"));
    const QString state = dir.filePath(QStringLiteral("state"));
    const AgentSpec spec = acpAgent(program, strings({"acp"}), {state}, work, bridge);
    EXPECT_EQ(spec.program, QFileInfo(program).canonicalFilePath());
    EXPECT_EQ(spec.arguments, strings({"acp"}));
    EXPECT_EQ(spec.profile->readExecute, (QStringList{QFileInfo(program).canonicalPath(), dir.filePath(QStringLiteral("app/bin")),
                                                     dir.filePath(QStringLiteral("app/lib"))}));
    EXPECT_EQ(spec.profile->readWrite, (QStringList{state, work}));
    EXPECT_TRUE(spec.profile->read.isEmpty());
    EXPECT_EQ(spec.profile->network, hikari::backends::sandbox::Network::Any);
    EXPECT_EQ(spec.workingDirectory, work);
    EXPECT_TRUE(QFileInfo(state).isDir());
    EXPECT_TRUE(QFileInfo(work + QStringLiteral("/tmp")).isDir());
    EXPECT_FALSE(spec.refuse(strings({"--dangerously-skip-permissions"})).isEmpty());
    EXPECT_TRUE(spec.refuse({}).isEmpty());
    EXPECT_TRUE(spec.channelGuard);
    EXPECT_FALSE(spec.guard);
    EXPECT_FALSE(spec.channelArguments);
}

class AcpGuardTest : public ::testing::Test {
protected:
    std::vector<Json> parse(const std::vector<QByteArray> &lines)
    {
        std::vector<Json> out;
        for (const QByteArray &l : lines)
            out.push_back(*Json::parse(l.toStdString()));
        return out;
    }

    AgentGuard::Lines fromSidecar(const std::string &text) { return guard->fromSidecar(QByteArray::fromStdString(text)); }
    AgentGuard::Lines fromAgent(const std::string &text) { return guard->fromAgent(QByteArray::fromStdString(text)); }

    Json server(const std::string &newSession)
    {
        const auto out = parse(fromSidecar(newSession).toAgent);
        EXPECT_EQ(out.size(), 1u);
        return out[0].find("params")->find("mcpServers")->asArray().at(0);
    }

    std::unique_ptr<AgentGuard> guard = acpGuard({.url = QStringLiteral("http://127.0.0.1:9/mcp"), .token = "tok",
                                                  .bridge = QStringLiteral("/app/hikarisub"), .work = QStringLiteral("/w")});
};

TEST_F(AcpGuardTest, OnlyASessionsMethodsPassAndTheAgentGetsNoFilesOrTerminal)
{
    const auto init = parse(fromSidecar(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{"terminal":true,"fs":{"readTextFile":true}}}})")
                                .toAgent);
    ASSERT_EQ(init.size(), 1u);
    EXPECT_EQ(*init[0].find("params")->find("clientCapabilities"),
              Json::object({{"fs", Json::object({{"readTextFile", false}, {"writeTextFile", false}})}, {"terminal", false}}));
    const auto refused = fromSidecar(R"({"jsonrpc":"2.0","id":2,"method":"session/set_mode","params":{"modeId":"yolo"}})");
    EXPECT_TRUE(refused.toAgent.empty());
    EXPECT_EQ(parse(refused.toSidecar),
              std::vector<Json>{Json::object({{"id", 2}, {"error", Json::object({{"code", rpc::kMethodNotFound},
                                                                                 {"message", "Hikari doesn't let the AI use session/set_mode"}})},
                                              {"jsonrpc", "2.0"}})});
    EXPECT_TRUE(fromSidecar(R"({"jsonrpc":"2.0","id":7,"result":{"outcome":{"outcome":"selected","optionId":"yes"}}})").toAgent.empty());
    for (const char *passes : {"session/prompt", "session/cancel"})
        EXPECT_EQ(fromSidecar(std::string(R"({"jsonrpc":"2.0","id":3,"method":")") + passes + R"(","params":{}})").toAgent.size(), 1u);

    const auto fs = fromAgent(R"({"jsonrpc":"2.0","id":"f","method":"fs/write_text_file","params":{"path":"/etc/passwd"}})");
    EXPECT_TRUE(fs.toSidecar.empty());
    EXPECT_EQ(*parse(fs.toAgent).at(0).find("error")->find("message"), Json("HikariSub doesn't offer fs/write_text_file"));
    EXPECT_TRUE(parse(fromAgent(R"({"jsonrpc":"2.0","id":"t","method":"terminal/create","params":{}})").toAgent).at(0).find("error"));
    const std::string update = R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s"}})";
    EXPECT_EQ(fromAgent(update).toSidecar, std::vector{QByteArray::fromStdString(update)});
}

TEST_F(AcpGuardTest, TheSessionRunsInItsWorkFolderWithHikarisToolsOverHttpWhenTheAgentTakesIt)
{
    const std::string newSession =
        R"({"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"/","additionalDirectories":["/home"],"mcpServers":[{"name":"evil","command":"sh"}]}})";
    fromSidecar(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    EXPECT_EQ(server(newSession),
              Json::object({{"name", "hikari"}, {"command", QDir::toNativeSeparators(QStringLiteral("/app/hikarisub")).toStdString()},
                            {"args", Json::array({"--mcp", "--channel-url", "http://127.0.0.1:9/mcp"})},
                            {"env", Json::array({Json::object({{"name", "HIKARI_MCP_TOKEN"}, {"value", "tok"}})})}}));
    const auto params = *parse(fromSidecar(newSession).toAgent).at(0).find("params");
    EXPECT_EQ(*params.find("cwd"), Json(QDir::toNativeSeparators(QStringLiteral("/w")).toStdString()));
    EXPECT_FALSE(params.find("additionalDirectories"));

    fromAgent(R"({"jsonrpc":"2.0","id":99,"result":{"agentCapabilities":{"mcpCapabilities":{"http":true}}}})");
    EXPECT_EQ(server(newSession)["command"], Json(QDir::toNativeSeparators(QStringLiteral("/app/hikarisub")).toStdString()));
    fromAgent(R"({"jsonrpc":"2.0","id":1,"result":{"agentCapabilities":{"mcpCapabilities":{"http":true}}}})");
    const std::string load = R"({"jsonrpc":"2.0","id":3,"method":"session/load","params":{"sessionId":"s","cwd":"/","mcpServers":[]}})";
    EXPECT_EQ(server(load), Json::object({{"type", "http"}, {"name", "hikari"}, {"url", "http://127.0.0.1:9/mcp"},
                                          {"headers", Json::array({Json::object({{"name", "Authorization"},
                                                                                 {"value", "Bearer tok"}})})}}));
}

TEST_F(AcpGuardTest, OnAPipeEveryAgentGetsTheBridgeToThePipeEvenWhenItTakesHttp)
{
    guard = acpGuard({.pipe = QStringLiteral(R"(\\.\pipe\hikarisub-ch-1)"), .token = "tok",
                      .bridge = QStringLiteral("/app/hikarisub"), .work = QStringLiteral("/w")});
    fromSidecar(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    fromAgent(R"({"jsonrpc":"2.0","id":1,"result":{"agentCapabilities":{"mcpCapabilities":{"http":true}}}})");
    EXPECT_EQ(server(R"({"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"/","mcpServers":[]}})"),
              Json::object({{"name", "hikari"}, {"command", QDir::toNativeSeparators(QStringLiteral("/app/hikarisub")).toStdString()},
                            {"args", Json::array({"--mcp", "--channel", R"(\\.\pipe\hikarisub-ch-1)"})},
                            {"env", Json::array({Json::object({{"name", "HIKARI_MCP_TOKEN"}, {"value", "tok"}})})}}));
}

TEST_F(AcpGuardTest, QtAllowsOnceOnlyHikarisToolsAndRejectsEveryOtherPermission)
{
    const auto ask = [&](const std::string &toolCall, const std::string &options) {
        const auto lines = fromAgent(R"({"jsonrpc":"2.0","id":5,"method":"session/request_permission","params":{"sessionId":"s","toolCall":)" +
                                     toolCall + R"(,"options":)" + options + "}}");
        EXPECT_TRUE(lines.toSidecar.empty());
        return *parse(lines.toAgent).at(0).find("result")->find("outcome");
    };
    const std::string all =
        R"([{"optionId":"always","kind":"allow_always"},{"optionId":"once","kind":"allow_once"},{"optionId":"no","kind":"reject_once"},{"optionId":"never","kind":"reject_always"}])";
    EXPECT_EQ(ask(R"({"toolCallId":"1","title":"hikari_document_apply"})", all),
              Json::object({{"outcome", "selected"}, {"optionId", "once"}}));
    EXPECT_EQ(ask(R"({"toolCallId":"1","title":"lines_find","_meta":{"agy":{"serverName":"hikari"}}})", all),
              Json::object({{"outcome", "selected"}, {"optionId", "once"}}));
    EXPECT_EQ(ask(R"({"toolCallId":"2","title":"Shell: rm -rf ~","kind":"execute"})", all),
              Json::object({{"outcome", "selected"}, {"optionId", "no"}}));
    EXPECT_EQ(ask(R"({"toolCallId":"3","title":"lines_find","_meta":{"serverName":"other"}})",
                  R"([{"optionId":"always","kind":"allow_always"},{"optionId":"never","kind":"reject_always"}])"),
              Json::object({{"outcome", "selected"}, {"optionId", "never"}}));
    EXPECT_EQ(ask(R"({"toolCallId":"4","title":"Edit ~/.bashrc"})", R"([{"optionId":"yes","kind":"allow_once"}])"),
              Json::object({{"outcome", "cancelled"}}));
}

TEST(AcpTools, AToolCallIsHikarisByItsMcpNameOrItsServer)
{
    EXPECT_EQ(acpHikariTool(Json::object({{"title", "mcp__hikari__lines_find"}})), "lines_find");
    EXPECT_EQ(acpHikariTool(Json::object({{"title", "  hikari.document_apply {\"revision\":1}"}})), "document_apply");
    EXPECT_EQ(acpHikariTool(Json::object({{"title", "hikari:lines_find"}})), "lines_find");
    EXPECT_EQ(acpHikariTool(Json::object({{"title", "hikari_"}})), std::nullopt);
    EXPECT_EQ(acpHikariTool(Json::object({{"title", "hikarix_lines_find"}})), std::nullopt);
    EXPECT_EQ(acpHikariTool(Json::object({{"title", "read_file"}, {"_meta", Json::object({{"server_name", "fs"}})}})), std::nullopt);
    EXPECT_EQ(acpHikariTool(Json::object()), std::nullopt);
}

TEST(McpHttpBridge, RelaysEachLineToTheChannelAndFinishesWhenTheAgentClosesOrTheChannelGoes)
{
    ensureApp();
    auto channel = std::make_unique<McpServer>([] { return nullptr; }, QByteArray(64, 'k'));
    channel->setChannel("t1");
    ASSERT_TRUE(channel->listenHttp(0));
    const QString url = QStringLiteral("http://127.0.0.1:%1/mcp").arg(channel->httpPort());

    QByteArray out;
    std::optional<QString> finished;
    McpHttpBridge bridge(url, QByteArray(64, 'k'), [&](const QByteArray &bytes) { out += bytes; });
    QObject::connect(&bridge, &McpHttpBridge::finished, [&](const QString &error) { finished = error; });
    bridge.fromAgent(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"
                     "\n"
                     R"({"jsonrpc":"2.0","id":2,)");
    bridge.fromAgent(R"("method":"tools/list"})"
                     "\n");
    ASSERT_TRUE(waitFor([&] { return out.count('\n') == 2; }));
    for (const QByteArray &l : out.trimmed().split('\n'))
        EXPECT_TRUE(Json::parse(l.toStdString())->find("error")) << l.toStdString();
    bridge.agentClosed();
    ASSERT_TRUE(waitFor([&] { return finished.has_value(); }));
    EXPECT_TRUE(finished->isEmpty());

    std::optional<QString> wrongToken;
    McpHttpBridge stranger(url, "nope", [](const QByteArray &) {});
    QObject::connect(&stranger, &McpHttpBridge::finished, [&](const QString &error) { wrongToken = error; });
    stranger.fromAgent(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"
                       "\n");
    ASSERT_TRUE(waitFor([&] { return wrongToken.has_value(); }));
    EXPECT_FALSE(wrongToken->isEmpty());

    channel.reset();
    std::optional<QString> gone;
    McpHttpBridge late(url, QByteArray(64, 'k'), [](const QByteArray &) {});
    QObject::connect(&late, &McpHttpBridge::finished, [&](const QString &error) { gone = error; });
    late.fromAgent(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"
                   "\n");
    ASSERT_TRUE(waitFor([&] { return gone.has_value(); }));
    EXPECT_FALSE(gone->isEmpty());
}

class AgentBrokerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { ensureApp(); }

    AgentSpec spec(bool guarded = false)
    {
        AgentSpec s;
        s.program = QStringLiteral(HIKARI_FAKE_AGENT);
        s.arguments = {QStringLiteral("app-server")};
        s.workingDirectory = dir.path();
        s.profile = hikari::backends::sandbox::Profile{.name = QStringLiteral("HikariSub.Test.Agent"),
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

TEST_F(AgentBrokerTest, QtDeclinesCodexsBuiltInFileChanges)
{
    broker.add("codex", spec(true));
    const std::uint64_t process = startReady("codex");
    send(process, R"({"id":1,"method":"turn/interrupt","params":{"emit":{"id":"f","method":"item/fileChange/requestApproval","params":{"itemId":"p"}}}})");
    send(process, R"({"id":2,"method":"turn/interrupt","params":{"emit":{"id":"g","method":"applyPatchApproval","params":{}}}})");
    ASSERT_TRUE(waitFor([&] { return withId(process, "f").has_value() && withId(process, "g").has_value(); }));
    EXPECT_EQ(withId(process, "f"), Json::object({{"id", "f"}, {"result", Json::object({{"decision", "decline"}})}}));
    EXPECT_EQ(*withId(process, "g")->find("result")->find("decision")->find("denied")->find("rejection"),
              Json("Hikari's AI can't run commands or change files."));
}

TEST_F(AgentBrokerTest, AChannelAgentGetsItsOwnToolsChannelForTheSessionUntilItExits)
{
    AgentSpec s = spec();
    s.channelArguments = [](const ChannelAddress &channel) {
        return QStringList{QStringLiteral("--tools=") + channel.url + channel.pipe};
    };
    broker.add("fake", s);
    const auto refused = call(kAgentStart, Json::object({{"process", 1}, {"agent", "fake"}}));
    ASSERT_FALSE(*refused);
    EXPECT_EQ(refused->error().first, rpc::kInvalidParams);

    const auto started = call(kAgentStart, Json::object({{"process", 2}, {"agent", "fake"}, {"session", "t1"},
                                                         {"args", Json::array({"--x"})}}));
    ASSERT_TRUE(started && *started);
    ASSERT_TRUE(waitFor([&] { return !lines(2).empty(); }));
    const Json report = *lines(2)[0].find("started");
    const std::string tools = report.find("args")->asArray()[1].asString().substr(std::string_view("--tools=").size());
    EXPECT_EQ(report.find("args")->asArray()[2], Json("--x"));
    EXPECT_EQ(report.find("mcpToken")->asString().size(), 64u);
#ifdef _WIN32
    ASSERT_TRUE(tools.starts_with(R"(\\.\pipe\hikarisub-ch-)")) << tools;
    EXPECT_EQ(tools.size(), std::string_view(R"(\\.\pipe\hikarisub-ch-)").size() + 32);
    const auto outsiderAnswered = [&] {
        QLocalSocket socket;
        socket.connectToServer(QString::fromStdString(tools));
        if (!socket.waitForConnected(2000))
            return false;
        socket.write(QByteArray::fromStdString(report.find("mcpToken")->asString()) +
                     "\n{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n");
        waitFor([&] { return socket.state() == QLocalSocket::UnconnectedState || socket.bytesAvailable() > 0; }, 5'000);
        return socket.bytesAvailable() > 0 || socket.state() != QLocalSocket::UnconnectedState;
    };
    EXPECT_FALSE(outsiderAnswered());
    ASSERT_TRUE(*call(kAgentKill, Json::object({{"process", 2}})));
    QLocalSocket gone;
    gone.connectToServer(QString::fromStdString(tools));
    EXPECT_FALSE(gone.waitForConnected(2000));
#else
    ASSERT_TRUE(tools.starts_with("http://127.0.0.1:")) << tools;
    const QUrl url(QString::fromStdString(tools));
    EXPECT_EQ(url.path(), QStringLiteral("/mcp"));
    const auto opens = [&] {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, quint16(url.port()));
        return socket.waitForConnected(2000);
    };
    EXPECT_TRUE(opens());
    ASSERT_TRUE(*call(kAgentKill, Json::object({{"process", 2}})));
    EXPECT_FALSE(opens());
#endif
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

TEST_F(AgentBrokerTest, AStdioOnlyAcpAgentReachesTheSessionsChannelThroughTheBridgeThatEndsWithIt)
{
    const QString work = dir.filePath(QStringLiteral("work"));
    AgentSpec s = acpAgent(QStringLiteral(HIKARI_FAKE_ACP_AGENT), {}, {}, work, QStringLiteral(HIKARI_TEST_MCP_HTTP_BRIDGE));
#ifdef _WIN32
    const QtCopy qt;
    ASSERT_TRUE(qt.ready());
    s.profile->readExecute << qt.folder();
#else
    s.profile->readExecute << QLibraryInfo::path(QLibraryInfo::LibrariesPath) << QLibraryInfo::path(QLibraryInfo::PluginsPath);
#endif
    broker.add("acp:fake", s);
    ASSERT_FALSE(*call(kAgentStart, Json::object({{"process", 1}, {"agent", "acp:fake"}, {"args", Json::array({"--yolo"})},
                                                  {"session", "t1"}})));
    ASSERT_TRUE(*call(kAgentStart, Json::object({{"process", 2}, {"agent", "acp:fake"}, {"session", "t1"}})));
    send(2, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    send(2, R"({"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":".","mcpServers":[]}})");
    ASSERT_TRUE(waitFor([&] { return withId(2, 2).has_value(); }));
    const auto said = [&](const std::string &prompt, int id) {
        send(2, R"({"jsonrpc":"2.0","id":)" + std::to_string(id) + R"(,"method":"session/prompt","params":{"sessionId":"s1","prompt":[{"type":"text","text":")" + prompt + R"("}]}})");
        EXPECT_TRUE(waitFor([&] { return withId(2, id).has_value(); }, 30'000));
        std::string last;
        for (const Json &l : lines(2))
            if (const Json *p = l.find("params"); p && p->find("update") && l.find("method")->asString() == "session/update")
                if (const Json *t = p->find("update")->find("content")->find("text")) {
                    if (t->asString().find(prompt == "close" ? "exit" : "jsonrpc") != std::string::npos)
                        return t->asString();
                    last = t->asString();
                }
        ADD_FAILURE() << prompt << ": " << last;
        return std::string();
    };
    const std::vector<Json> out = lines(2);
    const auto noted = std::ranges::find_if(out, [](const Json &l) { return l.find("method") && *l.find("method") == Json("fake/session"); });
    ASSERT_NE(noted, out.end());
    const Json session = *noted->find("params");
    EXPECT_EQ(*session.find("cwd"), Json(QDir::toNativeSeparators(work).toStdString()));
#ifdef _WIN32
    EXPECT_EQ(session.find("server")->find("args")->asArray().at(1), Json("--channel"));
#else
    EXPECT_EQ(session.find("server")->find("args")->asArray().at(1), Json("--channel-url"));
#endif
    const std::string tools = said("tools", 3);
    ASSERT_FALSE(tools.empty());
    EXPECT_NE(tools.find("AI sidecar isn't installed"), std::string::npos) << tools;
    EXPECT_EQ(said("close", 4), "exited 0");
    ASSERT_TRUE(*call(kAgentKill, Json::object({{"process", 2}})));
}

TEST(PrivatePipe, AChannelOpensForAnAgentsAppContainer)
{
    ensureApp();
    EXPECT_EQ(channelProblem(QStringLiteral("HikariSub.Test.Agent")).toStdString(), "");
}

#ifdef _WIN32
TEST(PrivatePipe, OnlyTheAgentsOwnAppContainerOpensItsChannel)
{
    ensureApp();
    const QtCopy qt;
    ASSERT_TRUE(qt.ready());
    const auto profile = [&qt](const char *name) {
        return hikari::backends::sandbox::Profile{
            .name = QString::fromLatin1(name),
            .readExecute = {QFileInfo(QStringLiteral(HIKARI_TEST_MCP_HTTP_BRIDGE)).absolutePath(), qt.folder()},
            .network = hikari::backends::sandbox::Network::Any};
    };
    hikari::backends::sandbox::Launcher agent(profile("HikariSub.Test.Agent"));
    hikari::backends::sandbox::Launcher other(profile("HikariSub.Test.Other"));
    ASSERT_FALSE(agent.containerSid().isEmpty()) << agent.coverage().missing.toStdString();
    McpServer channel([] { return nullptr; }, newMcpToken());
    channel.setChannel("t1");
    QString why;
    ASSERT_TRUE(channel.listenPipe(agent.containerSid(), [&](qint64 pid) { return agent.runs(pid); }, &why))
        << why.toStdString();

    const auto bridge = [&](hikari::backends::sandbox::Launcher &launcher, QProcess &process) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QString::fromLatin1(kMcpTokenVariable), QString::fromLatin1(channel.token()));
        process.setProcessEnvironment(environment);
        process.setProgram(QStringLiteral(HIKARI_TEST_MCP_HTTP_BRIDGE));
        process.setArguments({QStringLiteral("--channel"), channel.socketName()});
        launcher.prepare(process);
        process.start();
        return process.waitForStarted(10'000);
    };

    QProcess outsider;
    ASSERT_TRUE(bridge(other, outsider));
    EXPECT_TRUE(outsider.waitForFinished(15'000));
    EXPECT_EQ(outsider.exitCode(), 1);
    EXPECT_TRUE(outsider.readAllStandardError().contains("isn't running"));

    QProcess inside;
    ASSERT_TRUE(bridge(agent, inside));
    inside.write(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" "\n");
    EXPECT_TRUE(waitFor([&] { return inside.bytesAvailable() > 0 || inside.state() == QProcess::NotRunning; }, 15'000));
    const QByteArray reply = inside.readAllStandardOutput();
    EXPECT_TRUE(reply.contains(R"("id":1)")) << reply.toStdString() << inside.readAllStandardError().toStdString();
    inside.closeWriteChannel();
    EXPECT_TRUE(inside.waitForFinished(10'000));
    agent.killTree();
    other.killTree();
}
#endif

#ifndef _WIN32
TEST_F(AgentBrokerTest, AnAgentRunsInItsOwnSandbox)
{
    AgentSpec s = spec();
    s.profile->readExecute = {dir.path()};
    broker.add("fake", s);
    const std::uint64_t process = start("fake");
    ASSERT_TRUE(waitFor([&] { return exitCode(process).has_value(); }));
    EXPECT_TRUE(lines(process).empty());
}
#endif

}
