#include "hikari/backends/ai_mcp_clients.h"

#include "hikari/api/api.h"
#include "hikari/application/ai_providers.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QProcessEnvironment>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <ranges>

namespace hikari::backends::ai {

namespace app = application::ai;
using api::Json;

namespace {

std::string stringAt(const Json &json, std::string_view key)
{
    const Json *value = json.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

QStringList stringsAt(const Json &json, std::string_view key)
{
    QStringList out;
    if (const Json *values = json.find(key); values && values->isArray())
        for (const Json &v : values->asArray())
            if (v.isString())
                out << QString::fromStdString(v.asString());
    return out;
}

Json errorResponse(const Json &id, int code, const std::string &message)
{
    return Json::object({{"jsonrpc", "2.0"}, {"id", id}, {"error", Json::object({{"code", code}, {"message", message}})}});
}

QByteArray line(const Json &message)
{
    return QByteArray::fromStdString(message.dump()) + '\n';
}

constexpr std::array kSidecarMethods = {"initialize", "ping", "tools/list", "tools/call", "notifications/initialized",
                                        "notifications/cancelled"};

class StdioGuard final : public AgentGuard {
public:
    explicit StdioGuard(std::shared_ptr<McpTraffic> traffic) : m_traffic(std::move(traffic)) {}

    Lines fromSidecar(const QByteArray &bytes) override
    {
        const auto message = Json::parse(bytes.trimmed().toStdString());
        if (!message)
            return {};
        if (const auto refused = m_traffic->fromSidecar(*message)) {
            const Json *id = message->find("id");
            if (!id)
                return {};
            return {{}, {line(errorResponse(*id, -32600, *refused))}};
        }
        return {{line(*message)}, {}};
    }

    Lines fromAgent(const QByteArray &bytes) override
    {
        if (const auto message = Json::parse(bytes.trimmed().toStdString()))
            m_traffic->fromServer(*message);
        return {{}, {bytes.endsWith('\n') ? bytes : bytes + '\n'}};
    }

private:
    std::shared_ptr<McpTraffic> m_traffic;
};

std::optional<std::string> guardedBody(McpTraffic &traffic, const QByteArray &body)
{
    const auto parsed = Json::parse(body.toStdString());
    if (!parsed)
        return "MCP requests must be JSON-RPC";
    if (!parsed->isArray())
        return traffic.fromSidecar(*parsed);
    for (const Json &message : parsed->asArray())
        if (auto refused = traffic.fromSidecar(message))
            return refused;
    return std::nullopt;
}

}

Json McpServerEntry::toJson() const
{
    Json approvedTools = Json::array();
    for (const std::string &tool : approved)
        approvedTools.push(tool);
    Json out = Json::object({{"id", id}, {"name", name}, {"transport", transport == Transport::Stdio ? "stdio" : "http"}});
    if (transport == Transport::Stdio) {
        out["command"] = command.toStdString();
        Json args = Json::array();
        for (const QString &a : arguments)
            args.push(a.toStdString());
        out["arguments"] = std::move(args);
    } else {
        out["url"] = url.toStdString();
    }
    out["trusted"] = trusted;
    out["approved"] = std::move(approvedTools);
    out["revision"] = revision;
    return out;
}

std::optional<McpServerEntry> McpServerEntry::fromJson(const Json &json)
{
    if (!json.isObject())
        return std::nullopt;
    McpServerEntry entry;
    entry.id = stringAt(json, "id");
    entry.name = stringAt(json, "name");
    const std::string transport = stringAt(json, "transport");
    if (entry.id.empty() || entry.name.empty() || (transport != "stdio" && transport != "http"))
        return std::nullopt;
    entry.transport = transport == "stdio" ? Transport::Stdio : Transport::Http;
    entry.command = QString::fromStdString(stringAt(json, "command"));
    entry.arguments = stringsAt(json, "arguments");
    entry.url = QString::fromStdString(stringAt(json, "url"));
    if (entry.transport == Transport::Stdio ? entry.command.isEmpty() : !QUrl(entry.url).isValid() || entry.url.isEmpty())
        return std::nullopt;
    const Json *trusted = json.find("trusted");
    entry.trusted = trusted && trusted->isBool() && trusted->asBool();
    for (const QString &tool : stringsAt(json, "approved"))
        entry.approved.insert(tool.toStdString());
    if (const Json *revision = json.find("revision"); revision && revision->isInt())
        entry.revision = revision->asInt();
    return entry;
}

std::vector<McpServerEntry> mcpServersFromJson(const Json &json)
{
    std::vector<McpServerEntry> out;
    if (!json.isArray())
        return out;
    for (const Json &item : json.asArray())
        if (auto entry = McpServerEntry::fromJson(item);
            entry && std::ranges::none_of(out, [&](const McpServerEntry &e) { return e.id == entry->id; }))
            out.push_back(std::move(*entry));
    return out;
}

Json mcpServersToJson(const std::vector<McpServerEntry> &servers)
{
    Json out = Json::array();
    for (const McpServerEntry &server : servers)
        out.push(server.toJson());
    return out;
}

std::string newMcpServerId(const std::string &name, const std::vector<McpServerEntry> &servers)
{
    std::string base;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            base += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!base.empty() && base.back() != '-')
            base += '-';
        if (base.size() >= 24)
            break;
    }
    while (!base.empty() && base.back() == '-')
        base.pop_back();
    if (base.empty())
        base = "server";
    const auto taken = [&](const std::string &id) {
        return std::ranges::any_of(servers, [&](const McpServerEntry &s) { return s.id == id; });
    };
    std::string id = base;
    for (int n = 2; taken(id); ++n)
        id = base + '-' + std::to_string(n);
    return id;
}

std::string mcpSecretEntry(const std::string &server)
{
    return "mcp-server:" + server;
}

std::string McpSecrets::toJson() const
{
    Json env = Json::object();
    for (const auto &[name, value] : environment)
        env[name.toStdString()] = value.toStdString();
    return Json::object({{"environment", std::move(env)}, {"bearer", bearer.toStdString()}}).dump();
}

McpSecrets McpSecrets::fromJson(const std::string &text)
{
    McpSecrets out;
    const auto parsed = Json::parse(text);
    if (!parsed || !parsed->isObject())
        return out;
    if (const Json *env = parsed->find("environment"); env && env->isObject())
        for (const auto &[name, value] : env->asObject())
            if (value.isString())
                out.environment.emplace(QString::fromStdString(name), QString::fromStdString(value.asString()));
    out.bearer = QString::fromStdString(stringAt(*parsed, "bearer"));
    return out;
}

McpToolHints effectiveHints(const McpToolHints &listed, bool trusted)
{
    if (!trusted)
        return {};
    return {listed.readOnly, listed.readOnly ? false : listed.destructive};
}

std::optional<std::string> McpTraffic::fromSidecar(const Json &message)
{
    if (!message.isObject())
        return "MCP messages must be JSON-RPC objects";
    const Json *method = message.find("method");
    if (!method)
        return std::nullopt;
    if (!method->isString() || std::ranges::find(kSidecarMethods, method->asString()) == kSidecarMethods.end())
        return "HikariSub doesn't let the AI send " + (method->isString() ? method->asString() : std::string("that"));
    const Json *id = message.find("id");
    const Json *params = message.find("params");
    if (method->asString() == "tools/list") {
        if (id)
            m_listRequests.insert(id->dump());
        if (!params || !params->find("cursor"))
            m_listing.clear();
        return std::nullopt;
    }
    if (method->asString() != "tools/call")
        return std::nullopt;
    const std::string tool = params ? stringAt(*params, "name") : std::string();
    const Json *arguments = params ? params->find("arguments") : nullptr;
    const Json args = arguments ? *arguments : Json::object();
    const auto ticket = std::ranges::find_if(m_admitted, [&](const auto &a) { return a.first == tool && a.second == args; });
    if (ticket == m_admitted.end())
        return "HikariSub didn't allow this call to " + tool;
    m_admitted.erase(ticket);
    return std::nullopt;
}

void McpTraffic::fromServer(const Json &message)
{
    const Json *id = message.find("id");
    if (!id || !m_listRequests.erase(id->dump()))
        return;
    const Json *result = message.find("result");
    const Json *tools = result ? result->find("tools") : nullptr;
    if (!tools || !tools->isArray())
        return;
    for (const Json &tool : tools->asArray()) {
        const std::string name = stringAt(tool, "name");
        if (name.empty())
            continue;
        McpToolHints hints;
        if (const Json *annotations = tool.find("annotations"); annotations && annotations->isObject()) {
            const Json *readOnly = annotations->find("readOnlyHint");
            const Json *destructive = annotations->find("destructiveHint");
            hints.readOnly = readOnly && readOnly->isBool() && readOnly->asBool();
            hints.destructive = !destructive || !destructive->isBool() || destructive->asBool();
        }
        m_listing[name] = hints;
    }
    if (!result->find("nextCursor") || result->find("nextCursor")->isNull())
        m_tools = m_listing;
}

std::optional<McpToolHints> McpTraffic::hints(const std::string &tool) const
{
    const auto it = m_tools.find(tool);
    return it == m_tools.end() ? std::nullopt : std::optional(it->second);
}

std::vector<std::string> McpTraffic::tools() const
{
    std::vector<std::string> out;
    for (const auto &[name, hints] : m_tools)
        out.push_back(name);
    return out;
}

void McpTraffic::admit(std::string tool, Json arguments)
{
    m_admitted.emplace_back(std::move(tool), std::move(arguments));
}

std::unique_ptr<AgentGuard> mcpStdioGuard(std::shared_ptr<McpTraffic> traffic)
{
    return std::make_unique<StdioGuard>(std::move(traffic));
}

void McpResponseReader::push(const QByteArray &bytes)
{
    m_buffer += bytes;
    if (const QByteArray trimmed = m_buffer.trimmed(); trimmed.startsWith('{') || trimmed.startsWith('[')) {
        const auto parsed = Json::parse(trimmed.toStdString());
        if (!parsed)
            return;
        m_buffer.clear();
        if (!parsed->isArray())
            return m_message(*parsed);
        for (const Json &message : parsed->asArray())
            m_message(message);
        return;
    }
    for (qsizetype end = m_buffer.indexOf('\n'); end >= 0; end = m_buffer.indexOf('\n')) {
        const QByteArray l = m_buffer.left(end).trimmed();
        m_buffer.remove(0, end + 1);
        if (!l.startsWith("data:"))
            continue;
        if (const auto parsed = Json::parse(l.mid(5).trimmed().toStdString()))
            m_message(*parsed);
    }
}

Json mcpCallParams(const McpServerEntry &server, const std::string &tool, const Json &arguments,
                   const McpToolHints &hints)
{
    return Json::object({{"server", server.id},
                         {"serverName", server.name},
                         {"tool", tool},
                         {"arguments", arguments},
                         {"readOnly", hints.readOnly},
                         {"destructive", hints.destructive},
                         {"approved", server.approved.contains(tool)}});
}

McpClients::McpClients(app::KeyStore &keys, QString workingDirectory, QObject *parent)
    : QObject(parent), m_keys(keys), m_workingDirectory(std::move(workingDirectory))
{
}

McpClients::~McpClients()
{
    *m_alive = false;
}

const McpServerEntry *McpClients::find(const std::string &server) const
{
    const auto it = std::ranges::find_if(m_servers, [&](const McpServerEntry &s) { return s.id == server; });
    return it == m_servers.end() ? nullptr : &*it;
}

std::shared_ptr<McpTraffic> McpClients::traffic(const std::string &server)
{
    auto &slot = m_traffic[server];
    if (!slot)
        slot = std::make_shared<McpTraffic>();
    return slot;
}

void McpClients::setServers(std::vector<McpServerEntry> servers)
{
    for (const McpServerEntry &old : m_servers)
        if (std::ranges::none_of(servers, [&](const McpServerEntry &s) { return s.id == old.id && s.revision == old.revision; })) {
            m_traffic.erase(old.id);
            m_status.erase(old.id);
        }
    m_servers = std::move(servers);
    load([this] {
        registerStdio();
        announce();
    });
}

void McpClients::load(std::function<void()> done)
{
    const std::uint64_t generation = ++m_generation;
    auto remaining = std::make_shared<std::size_t>(m_servers.size());
    auto secrets = std::make_shared<std::map<std::string, McpSecrets>>();
    if (m_servers.empty()) {
        m_secrets.clear();
        return done();
    }
    for (const McpServerEntry &server : m_servers)
        m_keys.read(mcpSecretEntry(server.id), [this, alive = std::weak_ptr(m_alive), generation, remaining, secrets,
                                                id = server.id, done](app::KeyResult result) {
            if (!alive.lock() || generation != m_generation)
                return;
            if (const auto *text = std::get_if<std::string>(&result))
                (*secrets)[id] = McpSecrets::fromJson(*text);
            if (--*remaining > 0)
                return;
            m_secrets = std::move(*secrets);
            done();
        });
}

void McpClients::registerStdio()
{
    if (!m_broker)
        return;
    for (const std::string &id : std::exchange(m_registered, {}))
        m_broker->add("mcp:" + id, AgentSpec{.refuse = [](const QStringList &) {
                          return QStringLiteral("That MCP server was removed");
                      }});
    for (const McpServerEntry &server : m_servers) {
        if (server.transport != McpServerEntry::Transport::Stdio)
            continue;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        if (const auto secrets = m_secrets.find(server.id); secrets != m_secrets.end())
            for (const auto &[name, value] : secrets->second.environment)
                environment.insert(name, value);
        m_broker->add("mcp:" + server.id,
                      AgentSpec{.program = server.command,
                                .arguments = server.arguments,
                                .profile = std::nullopt,
                                .workingDirectory = m_workingDirectory,
                                .environment = environment,
                                .refuse = [](const QStringList &arguments) {
                                    return arguments.isEmpty() ? QString()
                                                               : QStringLiteral("Hikari picks an MCP server's arguments");
                                },
                                .guard = [t = traffic(server.id)] { return mcpStdioGuard(t); }});
        m_registered.insert(server.id);
    }
}

void McpClients::announce()
{
    if (!m_host || m_host->state() != AiSidecarHost::State::Ready)
        return;
    Json servers = Json::array();
    for (const McpServerEntry &server : m_servers)
        servers.push(Json::object({{"id", server.id},
                                   {"name", server.name},
                                   {"transport", server.transport == McpServerEntry::Transport::Stdio ? "stdio" : "http"},
                                   {"revision", server.revision}}));
    m_host->request(kMcpServersSet, Json::object({{"servers", std::move(servers)}}), [](SidecarResult) {});
}

void McpClients::attach(AiSidecarHost &host, AgentBroker &broker)
{
    m_host = &host;
    m_broker = &broker;
    registerStdio();
    connect(&host, &AiSidecarHost::ready, this, [this] { announce(); });
    host.handle(kMcpServersStatus, [this, alive = std::weak_ptr(m_alive)](const Json &params,
                                                                          std::shared_ptr<SidecarRequest> request) {
        if (!alive.lock())
            return request->fail(rpc::kBusy, "gone");
        const std::string server = stringAt(params, "server");
        if (!find(server))
            return request->fail(rpc::kInvalidParams, "No MCP server has that id");
        Status status{stringAt(params, "state"), 0, stringAt(params, "error")};
        if (const Json *tools = params.find("tools"); tools && tools->isInt())
            status.tools = static_cast<int>(tools->asInt());
        m_status[server] = std::move(status);
        request->respond(Json::object());
        emit statusChanged(QString::fromStdString(server));
    });
    host.handle(kMcpServersAdmit, [this, alive = std::weak_ptr(m_alive), host = QPointer(&host)](
                                      const Json &params, std::shared_ptr<SidecarRequest> request) {
        if (!alive.lock() || !host)
            return request->fail(rpc::kBusy, "gone");
        const std::string session = stringAt(params, "sessionId");
        const std::string tool = stringAt(params, "tool");
        const Json *arguments = params.find("arguments");
        const McpServerEntry *server = find(stringAt(params, "server"));
        if (session.empty() || tool.empty() || !arguments || !arguments->isObject() || !server)
            return request->fail(rpc::kInvalidParams, std::string(kMcpServersAdmit) +
                                                          " needs a session, a known server, a tool and its arguments");
        const auto listed = traffic(server->id)->hints(tool);
        if (!listed) {
            auto data = Json::object({{"code", std::string(api::errors::kNotFound)}});
            return request->fail(rpc::kHikariApiError, "The MCP server didn't list " + tool, std::move(data));
        }
        host->gateSession(session, std::string(kMcpCallOperation),
                          mcpCallParams(*server, tool, *arguments, effectiveHints(*listed, server->trusted)), request,
                          [t = traffic(server->id), tool, args = *arguments](SidecarRequest &admitted) {
                              t->admit(tool, args);
                              admitted.respond(Json::object());
                          });
    });
}

RelayTargets McpClients::relayTargets(RelayTargets next)
{
    return [this, alive = std::weak_ptr(m_alive), next = std::move(next)](const Json &target,
                                                                          std::function<void(RelayTargetResult)> done) {
        const Json *kind = target.find("kind");
        if (!kind || *kind != Json("mcp"))
            return next(target, std::move(done));
        const McpServerEntry *server = alive.lock() ? find(stringAt(target, "id")) : nullptr;
        if (!server || server->transport != McpServerEntry::Transport::Http)
            return done(std::unexpected(app::Error{app::ErrorKind::Rejected, 0, "MCP server not added"}));
        const auto secrets = m_secrets.find(server->id);
        const QString bearer = secrets == m_secrets.end() ? QString() : secrets->second.bearer;
        RelayTarget relay{.base = QUrl(server->url), .key = bearer.toStdString()};
        relay.exactUrl = true;
        if (!bearer.isEmpty())
            relay.headers.emplace_back("authorization", QByteArray("Bearer ") + kKeyPlaceholder);
        const auto t = traffic(server->id);
        relay.guardBody = [t](const QByteArray &body) { return guardedBody(*t, body); };
        relay.observer = [t] {
            auto reader = std::make_shared<McpResponseReader>([t](const Json &message) { t->fromServer(message); });
            return [reader](const QByteArray &bytes) { reader->push(bytes); };
        };
        done(std::move(relay));
    };
}

std::optional<McpClients::Status> McpClients::status(const std::string &server) const
{
    const auto it = m_status.find(server);
    return it == m_status.end() ? std::nullopt : std::optional(it->second);
}

std::optional<McpToolHints> McpClients::hints(const std::string &server, const std::string &tool) const
{
    const auto it = m_traffic.find(server);
    const McpServerEntry *entry = find(server);
    const auto listed = it == m_traffic.end() || !entry ? std::nullopt : it->second->hints(tool);
    return listed ? std::optional(effectiveHints(*listed, entry->trusted)) : std::nullopt;
}

std::vector<std::string> McpClients::tools(const std::string &server) const
{
    const auto it = m_traffic.find(server);
    return it == m_traffic.end() ? std::vector<std::string>() : it->second->tools();
}

}
