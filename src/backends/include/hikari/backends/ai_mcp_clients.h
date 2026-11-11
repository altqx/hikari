#pragma once

#include "hikari/api/json.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_http_relay.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application::ai {
class KeyStore;
}

namespace hikari::backends::ai {

class AiSidecarHost;

inline constexpr const char *kMcpServersSet = "_hikari/mcpServers.set";
inline constexpr const char *kMcpServersStatus = "_hikari/mcpServers.status";
inline constexpr const char *kMcpServersAdmit = "_hikari/mcpServers.admit";
/// The operation an MCP tool call goes through AgentThreads' gate as; its params are mcpCallParams'.
inline constexpr std::string_view kMcpCallOperation = "mcp.call";

/// A server the user added in Settings › AI, as saved in `ai.mcp.servers`. Its secrets are in the keychain.
struct McpServerEntry {
    enum class Transport { Stdio, Http };

    std::string id;
    std::string name;
    Transport transport = Transport::Stdio;
    QString command;
    QStringList arguments;
    QString url;
    /// The user trusts its hints: readOnlyHint skips asking, and destructiveHint false lets the mode decide.
    bool trusted = false;
    /// Tools the user lets run without asking, unless they are destructive.
    std::set<std::string> approved;
    /// Grows with every change, so the sidecar connects again.
    std::int64_t revision = 1;

    api::Json toJson() const;
    static std::optional<McpServerEntry> fromJson(const api::Json &json);
    friend bool operator==(const McpServerEntry &, const McpServerEntry &) = default;
};

std::vector<McpServerEntry> mcpServersFromJson(const api::Json &json);
api::Json mcpServersToJson(const std::vector<McpServerEntry> &servers);
/// A short id from the name, not yet in `taken`: letters, digits and '-'.
std::string newMcpServerId(const std::string &name, const std::vector<McpServerEntry> &servers);
/// The keychain entry for a server's secrets.
std::string mcpSecretEntry(const std::string &server);

/// What a server's secrets hold: environment variables for a command, or a bearer token for a URL.
struct McpSecrets {
    std::map<QString, QString> environment;
    QString bearer;

    std::string toJson() const;
    static McpSecrets fromJson(const std::string &text);
};

/// A tool's hints as the server listed them; MCP's defaults for those it left out.
struct McpToolHints {
    bool readOnly = false;
    bool destructive = true;
};

/// What the server's hints allow, after trust: read-only only from a trusted server, and destructive unless a
/// trusted server says otherwise.
McpToolHints effectiveHints(const McpToolHints &listed, bool trusted);

/// Qt's view of one server's JSON-RPC: the tools it listed with their hints, and the calls Qt admitted. A call the
/// sidecar sends without an admission never reaches the server.
class McpTraffic {
public:
    /// Why a message from the sidecar can't go to the server; nullopt when it may.
    std::optional<std::string> fromSidecar(const api::Json &message);
    void fromServer(const api::Json &message);

    std::optional<McpToolHints> hints(const std::string &tool) const;
    std::vector<std::string> tools() const;
    /// Lets one `tools/call` of `tool` with exactly `arguments` through.
    void admit(std::string tool, api::Json arguments);

private:
    std::map<std::string, McpToolHints> m_tools;
    std::map<std::string, McpToolHints> m_listing;
    std::set<std::string> m_listRequests;
    std::vector<std::pair<std::string, api::Json>> m_admitted;
};

/// The relayed stdio of a stdio server, a message per line, through `traffic`.
std::unique_ptr<AgentGuard> mcpStdioGuard(std::shared_ptr<McpTraffic> traffic);

/// Reads the messages in a streamable-HTTP response, JSON or server-sent events, as its bytes arrive.
class McpResponseReader {
public:
    explicit McpResponseReader(std::function<void(const api::Json &)> message) : m_message(std::move(message)) {}
    void push(const QByteArray &bytes);

private:
    std::function<void(const api::Json &)> m_message;
    QByteArray m_buffer;
};

/// The MCP params of `_hikari/mcpServers.admit` as the gate sees them: who asks for what, and the hints after trust.
api::Json mcpCallParams(const McpServerEntry &server, const std::string &tool, const api::Json &arguments,
                        const McpToolHints &hints);

/// The servers the user added, for one running sidecar: Qt starts each stdio server unsandboxed (ADR 0029), relays
/// each HTTP server's requests with its secrets, and admits each tool call through the session's gate.
class McpClients final : public QObject {
    Q_OBJECT
public:
    struct Status {
        std::string state; // connecting, ready or failed
        int tools = 0;
        std::string error;
    };

    McpClients(application::ai::KeyStore &keys, QString workingDirectory, QObject *parent = nullptr);
    ~McpClients() override;

    /// Reads each server's secrets, registers the stdio ones with `broker`, then tells the sidecar.
    void setServers(std::vector<McpServerEntry> servers);
    const std::vector<McpServerEntry> &servers() const { return m_servers; }
    void attach(AiSidecarHost &host, AgentBroker &broker);
    /// Adds {"kind": "mcp", "id": …}: the server's URL with its bearer token, its traffic guarded. Other targets go
    /// to `next`.
    RelayTargets relayTargets(RelayTargets next);

    std::optional<Status> status(const std::string &server) const;
    /// The tools the server listed the last time it ran here.
    std::vector<std::string> tools(const std::string &server) const;
    /// A listed tool's hints after the server's trust.
    std::optional<McpToolHints> hints(const std::string &server, const std::string &tool) const;

signals:
    void statusChanged(QString server);

private:
    void load(std::function<void()> done);
    void announce();
    void registerStdio();
    std::shared_ptr<McpTraffic> traffic(const std::string &server);
    const McpServerEntry *find(const std::string &server) const;

    application::ai::KeyStore &m_keys;
    QString m_workingDirectory;
    std::vector<McpServerEntry> m_servers;
    std::map<std::string, McpSecrets> m_secrets;
    std::map<std::string, std::shared_ptr<McpTraffic>> m_traffic;
    std::map<std::string, Status> m_status;
    std::set<std::string> m_registered;
    QPointer<AiSidecarHost> m_host;
    QPointer<AgentBroker> m_broker;
    std::uint64_t m_generation = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
