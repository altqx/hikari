#pragma once

#include "hikari/api/json.h"
#include "hikari/backends/sandbox.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::backends::ai {

class AiSidecarHost;
class CodexLease;
class McpServer;

inline constexpr const char *kAgentStart = "_hikari/agent.start";
inline constexpr const char *kAgentWrite = "_hikari/agent.write";
inline constexpr const char *kAgentCloseInput = "_hikari/agent.closeInput";
inline constexpr const char *kAgentKill = "_hikari/agent.kill";
inline constexpr const char *kAgentOutput = "_hikari/agent.output";
inline constexpr const char *kAgentExit = "_hikari/agent.exit";

/// Qt's rules for one agent's JSON-RPC stdio, applied a line at a time.
class AgentGuard {
public:
    struct Lines {
        std::vector<QByteArray> toAgent;
        std::vector<QByteArray> toSidecar;
    };
    virtual ~AgentGuard() = default;
    virtual Lines fromSidecar(const QByteArray &line) = 0;
    virtual Lines fromAgent(const QByteArray &line) = 0;
};

/// Where an agent session's private MCP channel listens: `url` on 127.0.0.1, or on Windows, where an AppContainer
/// can't reach loopback, a `pipe` that only the agent's AppContainer may open.
struct ChannelAddress {
    QString url;
    QString pipe;
};

/// An agent CLI Qt starts for the sidecar.
struct AgentSpec {
    /// From Qt's settings or PATH, never from the sidecar.
    QString program;
    /// Go before the sidecar's arguments.
    QStringList arguments;
    /// Unsandboxed when empty.
    std::optional<sandbox::Profile> profile;
    QString workingDirectory;
    /// Added to the clean environment the sidecar gets.
    QProcessEnvironment environment;
    /// The variables the sidecar may set.
    QStringList sidecarEnvironment;
    /// Why the sidecar's arguments are refused; empty when they're fine.
    std::function<QString(const QStringList &arguments)> refuse;
    std::function<std::unique_ptr<AgentGuard>()> guard;
    /// Gives each process a private MCP channel for the session the sidecar names: the arguments that point the
    /// agent at it. The token goes in HIKARI_MCP_TOKEN.
    std::function<QStringList(const ChannelAddress &channel)> channelArguments;
    /// Gives each process a private MCP channel for the session the sidecar names, through a guard that names the
    /// channel with `token` in the agent's own protocol.
    std::function<std::unique_ptr<AgentGuard>(const ChannelAddress &channel, const QByteArray &token)> channelGuard;
    /// Runs before the process starts, which it does once `go` gets no error.
    std::function<void(std::function<void(QString error)> go)> before;
    /// Runs after the process exits.
    std::function<void()> after;
};

/// Why agents in the AppContainer `containerName` can't have a private tools channel here; empty when they can, and
/// always where the channel is loopback HTTP.
QString channelProblem(const QString &containerName);

/// Refuses permission bypass, extra folders, plugins, inline settings and any MCP server but the SDK's in-process
/// ones, and requires Claude's built-in tools off (`--tools=`).
QString refusedClaudeArguments(const QStringList &arguments);
/// Codex app-server under Hikari: only a conversation's methods pass, every thread is read-only with no config
/// overrides, and Qt itself declines every command, file change and permission the agent asks for.
std::unique_ptr<AgentGuard> codexGuard();

/// Where an ACP agent finds Hikari's tools: the channel itself when the agent takes HTTP MCP servers, otherwise
/// `bridge --mcp --channel-url <url>` over stdio, or `bridge --mcp --channel <pipe>` when there is a pipe; the bridge
/// reads the token from HIKARI_MCP_TOKEN.
struct AcpChannel {
    QString url;
    QString pipe;
    QByteArray token;
    QString bridge;
    QString work;
};

/// An ACP agent under Hikari: only a conversation's methods pass, every session runs in `work` with Hikari's tools
/// as its only MCP server, the agent gets no file or terminal access through the client, and Qt allows only the
/// permission requests for Hikari's own tools.
std::unique_ptr<AgentGuard> acpGuard(AcpChannel channel);
/// The Hikari tool an ACP tool call runs, if it is one: agents name MCP tools their own way.
std::optional<std::string> acpHikariTool(const api::Json &toolCall);

/// An ACP agent: `program arguments`, writing only to `state` and `work`, reaching Hikari's tools through `bridge`
/// when it has no HTTP MCP.
AgentSpec acpAgent(const QString &program, const QStringList &arguments, const QStringList &state, const QString &work,
                   const QString &bridge);

/// OpenCode's `opencode acp`, writing only to its own XDG folders under `home` and to `work`.
AgentSpec openCodeAgent(const QString &program, const QString &home, const QString &work, const QString &bridge);

/// One of the user's own ACP agents, writing only to its private `folder`, which is its home too.
AgentSpec customAcpAgent(const QString &program, const QStringList &arguments, const QString &folder,
                         const QString &bridge);

/// Claude Code at `program` with only its own state writable: `<home>/.claude`, `<home>/.claude.json` and `work`.
AgentSpec claudeAgent(const QString &program, const QString &home, const QString &work);
/// Codex app-server behind `program`, which may be npm's launcher, with Hikari's own `CODEX_HOME`, signed in through
/// `lease` and given Hikari's tools on a private MCP channel, reached through `bridge` where the channel is a pipe.
/// Empty when no native binary is found.
std::optional<AgentSpec> codexAgent(const QString &program, const QString &codexHome, const QString &work,
                                    std::shared_ptr<CodexLease> lease, const QString &bridge);

/// Answers the sidecar's `_hikari/agent.*` requests: starts the agents Qt knows, each in its own sandbox and under
/// the process number the sidecar picked, and relays their stdio as `_hikari/agent.output` and `_hikari/agent.exit`
/// notifications. Every agent ends with the sidecar that started it.
class AgentBroker final : public QObject {
    Q_OBJECT
public:
    using Answer = std::function<void(std::expected<api::Json, std::pair<int, std::string>>)>;
    using Notify = std::function<void(const std::string &method, api::Json params)>;

    explicit AgentBroker(QObject *parent = nullptr);
    ~AgentBroker() override;

    void add(const std::string &agent, AgentSpec spec);
    /// Starts no more of `agent`; those running go on.
    void remove(const std::string &agent);
    void attach(AiSidecarHost &host);
    void call(const std::string &method, const api::Json &params, const Answer &answer);
    void setNotify(Notify notify) { m_notify = std::move(notify); }
    void killAll();
    std::size_t running() const { return m_running.size(); }
    std::size_t running(const std::string &agent) const;

    static constexpr std::size_t kMaxRunning = 8;

private:
    struct Running;

    void start(const api::Json &params, const Answer &answer);
    void write(Running &running, const QByteArray &data);
    void output(std::uint64_t id, const QByteArray &data, bool stderrData);
    void exited(std::uint64_t id, int code);
    Running *find(const api::Json &params);

    std::map<std::string, AgentSpec> m_specs;
    std::map<std::uint64_t, std::unique_ptr<Running>> m_running;
    QPointer<AiSidecarHost> m_host;
    Notify m_notify;
};

}
