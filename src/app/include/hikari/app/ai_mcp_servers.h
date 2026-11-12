#pragma once

#include "hikari/backends/ai_mcp_clients.h"

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class AiFeatures;

/// The MCP servers in Settings › AI: `ai.mcp.servers` holds them, the keychain their secrets. Changes apply at once,
/// like providers, because a secret goes straight to the keychain.
class AiMcpServers final : public QObject {
    Q_OBJECT
    /// Each {id, name, transport ("stdio" or "http"), command, url, trusted, approved, tools, state, error,
    /// toolCount}. command is the command line as typed; tools are {name, readOnly, alwaysAsks} for those it listed
    /// the last time it ran, after its trust.
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)

public:
    AiMcpServers(ui::SettingsStore &settings, AiFeatures &ai, QObject *parent = nullptr);
    ~AiMcpServers() override;

    QVariantList servers() const;

    /// Adds a server, or changes the one `form.id` names: {id?, name, transport, command, url, environment, bearer,
    /// trusted}. environment is NAME=value lines; it and bearer replace the stored secrets only when not empty.
    /// Answers with saved(id, error); error is "" or names the field: "name", "command", "url" or "keychain".
    Q_INVOKABLE void save(const QVariantMap &form);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void setTrusted(const QString &id, bool trusted);
    Q_INVOKABLE void setApproved(const QString &id, const QString &tool, bool approved);

signals:
    void serversChanged();
    void saved(QString id, QString error);

private:
    std::vector<backends::ai::McpServerEntry> entries() const;
    void store(const std::vector<backends::ai::McpServerEntry> &servers);
    void watch();

    ui::SettingsStore &m_settings;
    AiFeatures &m_ai;
    QMetaObject::Connection m_status;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
