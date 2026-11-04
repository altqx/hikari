#pragma once

#include "hikari/backends/ai_mcp_server.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QLocalSocket;

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

/// `hikarisub --mcp`: relays stdin and stdout to the server `endpointFile` names. Needs a QCoreApplication; never
/// returns.
[[noreturn]] void runMcpBridge(const QString &endpointFile);

}
