#pragma once

#include "hikari/api/json.h"
#include "hikari/backends/sandbox.h"

#include <QByteArray>
#include <QObject>
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

/// An agent CLI Qt starts for the sidecar.
struct AgentSpec {
    /// From Qt's settings or PATH, never from the sidecar.
    QString program;
    /// Go before the sidecar's arguments.
    QStringList arguments;
    sandbox::Profile profile;
    QString workingDirectory;
    /// Added to the clean environment the sidecar gets.
    QProcessEnvironment environment;
    /// The variables the sidecar may set.
    QStringList sidecarEnvironment;
    /// Why the sidecar's arguments are refused; empty when they're fine.
    std::function<QString(const QStringList &arguments)> refuse;
    std::function<std::unique_ptr<AgentGuard>()> guard;
};

/// Refuses permission bypass, extra folders, plugins, inline settings and any MCP server but the SDK's in-process
/// ones, and requires Claude's built-in tools off (`--tools=`).
QString refusedClaudeArguments(const QStringList &arguments);
/// Codex app-server under Hikari: only a conversation's methods pass, every thread is read-only with no config
/// overrides, and Qt itself declines every command, file change and permission the agent asks for.
std::unique_ptr<AgentGuard> codexGuard();

/// Claude Code at `program` with only its own state writable: `<home>/.claude`, `<home>/.claude.json` and `work`.
AgentSpec claudeAgent(const QString &program, const QString &home, const QString &work);
/// Codex app-server behind `program`, which may be npm's launcher, with Hikari's own `CODEX_HOME`. Empty when no
/// native binary is found.
std::optional<AgentSpec> codexAgent(const QString &program, const QString &codexHome, const QString &work);

/// Answers the sidecar's `_hikari/agent.*` requests: starts the agents Qt knows, each in its own sandbox, and relays
/// their stdio as `_hikari/agent.output` and `_hikari/agent.exit` notifications. Every agent ends with the sidecar
/// that started it.
class AgentBroker final : public QObject {
    Q_OBJECT
public:
    using Answer = std::function<void(std::expected<api::Json, std::pair<int, std::string>>)>;
    using Notify = std::function<void(const std::string &method, api::Json params)>;

    explicit AgentBroker(QObject *parent = nullptr);
    ~AgentBroker() override;

    void add(const std::string &agent, AgentSpec spec);
    void attach(AiSidecarHost &host);
    void call(const std::string &method, const api::Json &params, const Answer &answer);
    void setNotify(Notify notify) { m_notify = std::move(notify); }
    void killAll();
    std::size_t running() const { return m_running.size(); }

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
    std::uint64_t m_next = 1;
    Notify m_notify;
};

}
