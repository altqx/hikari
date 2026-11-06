#include "hikari/app/ai_mcp.h"

#include "hikari/app/ai_features.h"
#include "settings_store.h"

#include <QDebug>

namespace hikari::app {

AiMcp::AiMcp(ui::SettingsStore &settings, AiFeatures &ai, QString endpointFile, QObject *parent)
    : QObject(parent), m_settings(settings), m_ai(ai), m_endpointFile(std::move(endpointFile))
{
    m_endpoint = (m_endpointFile.isEmpty() ? std::nullopt : backends::ai::readMcpEndpoint(m_endpointFile))
                     .value_or(backends::ai::McpEndpoint{});
    if (m_endpoint.token.isEmpty())
        m_endpoint.token = backends::ai::newMcpToken();
    connect(&m_settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String(kSetting) || id == QLatin1String(kHttpSetting))
            apply();
    });
    connect(&m_ai, &AiFeatures::switched, this, &AiMcp::apply);
    m_busySource = m_ai.addBusySource([this] { return m_server && m_server->connections() > 0; });
    apply();
}

AiMcp::~AiMcp()
{
    m_ai.removeBusySource(m_busySource);
    if (!m_server)
        return;
    m_server.reset();
    m_endpoint.socket.clear();
    save();
}

void AiMcp::apply()
{
    if (!m_ai.on() || !m_settings.boolean(kSetting)) {
        if (!m_server)
            return;
        m_server.reset();
        m_endpoint.socket.clear();
        save();
        emit changed();
        return;
    }
    if (!m_server) {
        m_server = std::make_unique<backends::ai::McpServer>([this] { return m_ai.sidecarForMcp(); }, m_endpoint.token);
        QString error;
        if (!m_server->listenLocal(&error)) {
            qWarning() << "MCP server: can't listen:" << error;
            m_server.reset();
            return;
        }
        m_endpoint.socket = m_server->socketName();
    }
    const bool http = m_settings.boolean(kHttpSetting);
    if (http && m_server->httpPort() == 0) {
        QString error;
        if ((m_endpoint.port == 0 || !m_server->listenHttp(m_endpoint.port)) && !m_server->listenHttp(0, &error))
            qWarning() << "MCP server: can't listen on 127.0.0.1:" << error;
        else
            m_endpoint.port = m_server->httpPort();
    } else if (!http) {
        m_server->closeHttp();
    }
    save();
    emit changed();
}

void AiMcp::save()
{
    if (!m_endpointFile.isEmpty() && !backends::ai::writeMcpEndpoint(m_endpointFile, m_endpoint))
        qWarning() << "MCP server: can't write" << m_endpointFile;
}

}
