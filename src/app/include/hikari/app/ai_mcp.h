#pragma once

#include "hikari/backends/ai_mcp_server.h"

#include <QObject>
#include <QString>

#include <memory>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class AiFeatures;

/// HikariSub's MCP server: listening while AI is on and the user turned it on, on 127.0.0.1 too when they asked.
/// Where it listens goes in the endpoint file for `hikarisub --mcp`.
class AiMcp final : public QObject {
    Q_OBJECT
public:
    static constexpr const char *kSetting = "ai.mcp.server";
    static constexpr const char *kHttpSetting = "ai.mcp.http";

    /// An empty `endpointFile` keeps nothing, for tests without a user folder.
    AiMcp(ui::SettingsStore &settings, AiFeatures &ai, QString endpointFile, QObject *parent = nullptr);
    ~AiMcp() override;

    /// Null while off.
    backends::ai::McpServer *server() const { return m_server.get(); }
    /// The token survives restarts so agents' settings stay valid; the socket is empty while off.
    const backends::ai::McpEndpoint &endpoint() const { return m_endpoint; }

signals:
    void changed();

private:
    void apply();
    void save();

    ui::SettingsStore &m_settings;
    AiFeatures &m_ai;
    QString m_endpointFile;
    backends::ai::McpEndpoint m_endpoint;
    std::unique_ptr<backends::ai::McpServer> m_server;
    int m_busySource = 0;
};

}
