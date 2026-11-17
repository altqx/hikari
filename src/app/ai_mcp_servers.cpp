#include "hikari/app/ai_mcp_servers.h"

#include "hikari/app/ai_features.h"
#include "hikari/application/ai_providers.h"
#include "settings_store.h"

#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace hikari::app {

using backends::ai::McpServerEntry;
using backends::ai::McpSecrets;

namespace {

QString quoted(const QString &argument)
{
    if (!argument.isEmpty() && !argument.contains(QRegularExpression(QStringLiteral("[\\s\"']"))))
        return argument;
    QString escaped = argument;
    escaped.replace(QLatin1Char('"'), QLatin1String("\"\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString commandLine(const McpServerEntry &server)
{
    QStringList parts{quoted(server.command)};
    for (const QString &a : server.arguments)
        parts << quoted(a);
    return parts.join(QLatin1Char(' '));
}

std::optional<std::map<QString, QString>> environmentFrom(const QString &text)
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    std::map<QString, QString> out;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0 || !name.match(line.left(equals)).hasMatch())
            return std::nullopt;
        out[line.left(equals)] = line.mid(equals + 1);
    }
    return out;
}

}

AiMcpServers::AiMcpServers(ui::SettingsStore &settings, AiFeatures &ai, QObject *parent)
    : QObject(parent), m_settings(settings), m_ai(ai)
{
    connect(&m_settings, &ui::SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String(AiFeatures::kMcpServersSetting) || id == QLatin1String(AiFeatures::kPluginMcpServersSetting))
            emit serversChanged();
    });
    connect(&m_ai, &AiFeatures::pluginMcpServersChanged, this, &AiMcpServers::serversChanged);
    connect(&m_ai, &AiFeatures::switched, this, [this] {
        watch();
        emit serversChanged();
    });
    watch();
}

AiMcpServers::~AiMcpServers()
{
    *m_alive = false;
}

void AiMcpServers::watch()
{
    disconnect(m_status);
    if (backends::ai::McpClients *clients = m_ai.mcpClients())
        m_status = connect(clients, &backends::ai::McpClients::statusChanged, this, &AiMcpServers::serversChanged);
}

std::vector<McpServerEntry> AiMcpServers::entries() const
{
    api::Json rows = api::Json::array();
    for (const QString &row : m_settings.list(AiFeatures::kMcpServersSetting))
        if (auto parsed = api::Json::parse(row.toStdString()))
            rows.push(std::move(*parsed));
    return backends::ai::mcpServersFromJson(rows);
}

void AiMcpServers::store(const std::vector<McpServerEntry> &servers)
{
    QStringList rows;
    for (const McpServerEntry &server : servers)
        rows << QString::fromStdString(server.toJson().dump());
    m_settings.set(AiFeatures::kMcpServersSetting, rows);
}

QVariantList AiMcpServers::servers() const
{
    const backends::ai::McpClients *clients = m_ai.mcpClients();
    const std::vector<McpServerEntry> chosen = choices();
    std::vector<McpServerEntry> listed = entries();
    for (McpServerEntry server : m_ai.pluginMcpServers()) {
        if (const auto choice = std::ranges::find(chosen, server.id, &McpServerEntry::id); choice != chosen.end()) {
            server.trusted = choice->trusted;
            server.approved = choice->approved;
        }
        listed.push_back(std::move(server));
    }
    QVariantList out;
    for (const McpServerEntry &server : listed) {
        QStringList approved;
        for (const std::string &tool : server.approved)
            approved << QString::fromStdString(tool);
        QVariantList tools;
        if (clients)
            for (const std::string &tool : clients->tools(server.id)) {
                const auto hints = clients->hints(server.id, tool).value_or(backends::ai::McpToolHints{});
                tools << QVariantMap{{QStringLiteral("name"), QString::fromStdString(tool)},
                                     {QStringLiteral("readOnly"), hints.readOnly},
                                     {QStringLiteral("alwaysAsks"), !hints.readOnly && hints.destructive}};
            }
        const auto status = clients ? clients->status(server.id) : std::nullopt;
        const bool stdio = server.transport == McpServerEntry::Transport::Stdio;
        const bool plugin = !server.plugin.empty();
        out << QVariantMap{{QStringLiteral("id"), QString::fromStdString(server.id)},
                           {QStringLiteral("plugin"), plugin},
                           {QStringLiteral("on"), !plugin || std::ranges::contains(chosen, server.id, &McpServerEntry::id)},
                           {QStringLiteral("name"), QString::fromStdString(server.name)},
                           {QStringLiteral("transport"), stdio ? QStringLiteral("stdio") : QStringLiteral("http")},
                           {QStringLiteral("command"), stdio && !plugin ? commandLine(server) : QString()},
                           {QStringLiteral("url"), server.url},
                           {QStringLiteral("trusted"), server.trusted},
                           {QStringLiteral("approved"), approved},
                           {QStringLiteral("tools"), tools},
                           {QStringLiteral("state"), status ? QString::fromStdString(status->state) : QString()},
                           {QStringLiteral("error"), status ? QString::fromStdString(status->error) : QString()},
                           {QStringLiteral("toolCount"), status ? status->tools : 0}};
    }
    return out;
}

void AiMcpServers::save(const QVariantMap &form)
{
    std::vector<McpServerEntry> servers = entries();
    const std::string id = form.value(QStringLiteral("id")).toString().toStdString();
    const auto existing = std::ranges::find_if(servers, [&](const McpServerEntry &s) { return s.id == id; });
    McpServerEntry entry = existing == servers.end() ? McpServerEntry{} : *existing;
    const auto fail = [this, id](const char *field) {
        emit saved(QString::fromStdString(id), QString::fromLatin1(field));
    };
    entry.name = form.value(QStringLiteral("name")).toString().trimmed().toStdString();
    if (entry.name.empty())
        return fail("name");
    entry.transport = form.value(QStringLiteral("transport")).toString() == QLatin1String("http")
                          ? McpServerEntry::Transport::Http
                          : McpServerEntry::Transport::Stdio;
    entry.trusted = form.value(QStringLiteral("trusted")).toBool();
    if (entry.transport == McpServerEntry::Transport::Stdio) {
        QStringList parts = QProcess::splitCommand(form.value(QStringLiteral("command")).toString());
        if (parts.isEmpty())
            return fail("command");
        entry.command = parts.takeFirst();
        entry.arguments = parts;
        entry.url.clear();
    } else {
        const QUrl url(form.value(QStringLiteral("url")).toString().trimmed(), QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")) ||
            url.host().isEmpty())
            return fail("url");
        entry.url = url.toString();
        entry.command.clear();
        entry.arguments.clear();
    }
    const QString environmentText = form.value(QStringLiteral("environment")).toString();
    const auto environment = environmentFrom(environmentText);
    if (!environment)
        return fail("environment");
    const QString bearer = form.value(QStringLiteral("bearer")).toString().trimmed();
    if (existing == servers.end()) {
        entry.id = backends::ai::newMcpServerId(entry.name, servers);
        entry.revision = 1;
    } else {
        ++entry.revision;
    }
    const auto finish = [this, entry, alive = std::weak_ptr(m_alive)] {
        if (!alive.lock())
            return;
        std::vector<McpServerEntry> now = entries();
        const auto at = std::ranges::find_if(now, [&](const McpServerEntry &s) { return s.id == entry.id; });
        if (at == now.end())
            now.push_back(entry);
        else
            *at = entry;
        store(now);
        emit saved(QString::fromStdString(entry.id), QString());
    };
    application::ai::KeyStore *keys = m_ai.keys();
    if (environment->empty() && bearer.isEmpty())
        return finish();
    if (!keys)
        return fail("keychain");
    const std::string secretEntry = backends::ai::mcpSecretEntry(entry.id);
    keys->read(secretEntry, [this, keys, secretEntry, environment = *environment, bearer, finish, id = entry.id,
                             alive = std::weak_ptr(m_alive)](application::ai::KeyResult result) {
        if (!alive.lock())
            return;
        const auto *text = std::get_if<std::string>(&result);
        McpSecrets secrets = text ? McpSecrets::fromJson(*text) : McpSecrets{};
        if (!environment.empty())
            secrets.environment = environment;
        if (!bearer.isEmpty())
            secrets.bearer = bearer;
        keys->write(secretEntry, secrets.toJson(), [this, finish, id, alive](std::optional<application::ai::KeyError> e) {
            if (!alive.lock())
                return;
            if (e)
                return emit saved(QString::fromStdString(id), QStringLiteral("keychain"));
            finish();
        });
    });
}

void AiMcpServers::remove(const QString &id)
{
    std::vector<McpServerEntry> servers = entries();
    std::erase_if(servers, [&](const McpServerEntry &s) { return s.id == id.toStdString(); });
    store(servers);
    if (application::ai::KeyStore *keys = m_ai.keys())
        keys->remove(backends::ai::mcpSecretEntry(id.toStdString()), [](std::optional<application::ai::KeyError>) {});
}

std::vector<McpServerEntry> AiMcpServers::choices() const
{
    return pluginMcpChoices(m_settings.list(AiFeatures::kPluginMcpServersSetting));
}

void AiMcpServers::storeChoices(const std::vector<McpServerEntry> &chosen)
{
    QStringList rows;
    for (const McpServerEntry &choice : chosen)
        rows << pluginMcpChoiceRow(choice);
    m_settings.set(AiFeatures::kPluginMcpServersSetting, rows);
}

bool AiMcpServers::changeChoice(const QString &id, const std::function<void(McpServerEntry &)> &change)
{
    std::vector<McpServerEntry> chosen = choices();
    const auto choice = std::ranges::find(chosen, id.toStdString(), &McpServerEntry::id);
    if (choice == chosen.end())
        return std::ranges::contains(m_ai.pluginMcpServers(), id.toStdString(), &McpServerEntry::id);
    change(*choice);
    storeChoices(chosen);
    return true;
}

void AiMcpServers::setOn(const QString &id, bool on)
{
    const auto &available = m_ai.pluginMcpServers();
    const auto server = std::ranges::find(available, id.toStdString(), &McpServerEntry::id);
    std::vector<McpServerEntry> chosen = choices();
    const bool was = std::erase_if(chosen, [&](const McpServerEntry &c) { return c.id == id.toStdString(); }) > 0;
    if (on && server != available.end()) {
        McpServerEntry choice;
        choice.id = server->id;
        choice.plugin = server->plugin;
        chosen.push_back(std::move(choice));
    } else if (!was) {
        return;
    }
    storeChoices(chosen);
}

void AiMcpServers::setTrusted(const QString &id, bool trusted)
{
    if (changeChoice(id, [trusted](McpServerEntry &c) { c.trusted = trusted; }))
        return;
    std::vector<McpServerEntry> servers = entries();
    for (McpServerEntry &s : servers)
        if (s.id == id.toStdString())
            s.trusted = trusted;
    store(servers);
}

void AiMcpServers::setApproved(const QString &id, const QString &tool, bool approved)
{
    const auto approve = [&](McpServerEntry &s) {
        if (approved)
            s.approved.insert(tool.toStdString());
        else
            s.approved.erase(tool.toStdString());
    };
    if (changeChoice(id, approve))
        return;
    std::vector<McpServerEntry> servers = entries();
    for (McpServerEntry &s : servers)
        if (s.id == id.toStdString())
            approve(s);
    store(servers);
}

}
