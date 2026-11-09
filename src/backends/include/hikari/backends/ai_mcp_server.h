#pragma once

#include "hikari/api/api.h"
#include "hikari/api/json.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

class QLocalServer;
class QTcpServer;

namespace hikari::backends::ai {

class AiSidecarHost;

/// Where a running MCP server listens, kept in a file only the user can read so `hikarisub --mcp` can connect.
struct McpEndpoint {
    QString socket; // empty while the server is off
    QByteArray token;
    quint16 port = 0; // the last HTTP port, kept so agents' settings stay valid
};

/// <userFolder>/ai/mcp.json
QString mcpEndpointFile(const QString &userFolder);
std::optional<McpEndpoint> readMcpEndpoint(const QString &file);
bool writeMcpEndpoint(const QString &file, const McpEndpoint &endpoint);
/// 32 random bytes as hex.
QByteArray newMcpToken();

/// HikariSub's MCP server for agents outside it, such as Claude Code or Codex. Qt listens and checks the token;
/// the sidecar speaks MCP (`_hikari/mcp.message`) and runs the tools through `_hikari/api.call`, which the
/// gate this sets on the host runs as the connection's caller.
///
/// A local socket that only the user can open serves `hikarisub --mcp`: its first line is the token, then one
/// JSON-RPC message per line each way. Streamable HTTP on 127.0.0.1 takes `POST /mcp` with
/// `Authorization: Bearer <token>`, refusing other Origins and Hosts.
class McpServer final : public QObject {
    Q_OBJECT
public:
    /// The sidecar to relay to, built on first use; null when AI is off or no sidecar is installed.
    using SidecarFor = std::function<AiSidecarHost *()>;

    McpServer(SidecarFor sidecar, QByteArray token, QObject *parent = nullptr);
    ~McpServer() override;

    /// A new randomly named socket. False with `error` set when it can't listen.
    bool listenLocal(QString *error = nullptr);
    QString socketName() const;
    /// `port` 0 picks a free one.
    bool listenHttp(quint16 port, QString *error = nullptr);
    void closeHttp();
    quint16 httpPort() const;

    /// What an MCP connection's call runs as: every Document, under the client's name, with its edits in the
    /// review (one turn per connection). document.apply is the only write it may call.
    std::expected<api::Caller, api::Error> callerFor(const api::Json &mcp, const std::string &operation) const;

    std::size_t connections() const { return m_local.size() + m_http.size(); }

private:
    struct Local;
    struct Http;

    void acceptLocal();
    void readLocal(Local &client);
    void dropLocal(std::uint64_t id);
    void acceptHttp();
    void readHttp(Http &client);
    void answerHttp(Http &client);
    void dropHttp(std::uint64_t id);
    /// Relays one client message; `answer` gets the reply text, if any, and its HTTP status.
    void relay(const std::string &connection, QByteArray text, api::Json http,
               std::function<void(std::optional<std::string>, int)> answer);
    AiSidecarHost *host();

    SidecarFor m_sidecar;
    QByteArray m_token;
    QPointer<AiSidecarHost> m_host;
    std::unique_ptr<QLocalServer> m_localServer;
    std::unique_ptr<QTcpServer> m_httpServer;
    std::map<std::uint64_t, std::unique_ptr<Local>> m_local;
    std::map<std::uint64_t, std::unique_ptr<Http>> m_http;
    std::uint64_t m_nextId = 1;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
