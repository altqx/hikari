#pragma once

#include "hikari/backends/ai_mcp_clients.h"

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
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
    /// Each {id, plugin, on, name, transport ("stdio" or "http"), command, url, trusted, approved, tools, state, error,
    /// toolCount}: the user's servers, then those of the plugins that are on. command is the command line as typed,
    /// empty for a plugin's; tools are {name, readOnly, alwaysAsks} for those it listed the last time it ran, after its
    /// trust. A plugin's server is `on` once the user turns it on.
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
    Q_INVOKABLE void setOn(const QString &id, bool on);
    Q_INVOKABLE void setTrusted(const QString &id, bool trusted);
    Q_INVOKABLE void setApproved(const QString &id, const QString &tool, bool approved);

signals:
    void serversChanged();
    void saved(QString id, QString error);

private:
    std::vector<backends::ai::McpServerEntry> entries() const;
    void store(const std::vector<backends::ai::McpServerEntry> &servers);
    std::vector<backends::ai::McpServerEntry> choices() const;
    void storeChoices(const std::vector<backends::ai::McpServerEntry> &chosen);
    // False when `id` is no plugin server.
    bool changeChoice(const QString &id, const std::function<void(backends::ai::McpServerEntry &)> &change);
    void watch();

    ui::SettingsStore &m_settings;
    AiFeatures &m_ai;
    QMetaObject::Connection m_status;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
