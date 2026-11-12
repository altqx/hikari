#pragma once

#include "hikari/backends/ai_mcp_server.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QLocalSocket;
class QNetworkAccessManager;

namespace hikari::backends::ai {

/// Relays one agent's stdio to HikariSub's MCP server over its local socket, as `hikarisub --mcp` does.
class McpBridge final : public QObject {
    Q_OBJECT
public:
    explicit McpBridge(std::function<void(const QByteArray &)> toAgent, QObject *parent = nullptr);
    ~McpBridge() override;

    /// False, with `error` worded for the agent's user, when HikariSub isn't listening.
    bool open(const McpEndpoint &endpoint, QString *error = nullptr);
    /// What the agent wrote.
    void fromAgent(const QByteArray &bytes);
    /// The agent closed stdin: send what is left, then finish.
    void agentClosed();

signals:
    void finished();

private:
    std::function<void(const QByteArray &)> m_toAgent;
    std::unique_ptr<QLocalSocket> m_socket;
};

/// Relays one agent's stdio to an agent session's HTTP channel a JSON-RPC line at a time, as
/// `hikarisub --mcp --channel-url` does.
class McpHttpBridge final : public QObject {
    Q_OBJECT
public:
    McpHttpBridge(QString url, QByteArray token, std::function<void(const QByteArray &)> toAgent,
                  QObject *parent = nullptr);
    ~McpHttpBridge() override;

    void fromAgent(const QByteArray &bytes);
    /// The agent closed stdin: answer what is in flight, then finish.
    void agentClosed();

signals:
    /// `error` is empty when the agent closed stdin, or says why the channel went away.
    void finished(const QString &error);

private:
    void post(const QByteArray &message);
    void settle();

    QString m_url;
    QByteArray m_token;
    std::function<void(const QByteArray &)> m_toAgent;
    std::unique_ptr<QNetworkAccessManager> m_network;
    QByteArray m_buffer;
    int m_inFlight = 0;
    bool m_closed = false;
};

/// The variable a private channel's token travels in, never the command line.
inline constexpr const char *kMcpTokenVariable = "HIKARI_MCP_TOKEN";

/// `hikarisub --mcp --channel <name>`: relays stdin and stdout to an agent session's channel. Never returns.
[[noreturn]] void runMcpBridge(const McpEndpoint &endpoint);
/// `hikarisub --mcp --channel-url <url>`: relays stdin and stdout to an agent session's HTTP channel, with the token
/// from HIKARI_MCP_TOKEN, until stdin closes or the channel goes away. Needs a QCoreApplication; never returns.
[[noreturn]] void runMcpHttpBridge(const QString &url, const QByteArray &token);
/// `hikarisub --mcp`: relays stdin and stdout to the server `endpointFile` names. Needs a QCoreApplication; never
/// returns.
[[noreturn]] void runMcpBridge(const QString &endpointFile);

}
