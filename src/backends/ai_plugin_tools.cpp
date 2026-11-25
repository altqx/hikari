#include "hikari/backends/ai_plugin_tools.h"

#include "hikari/api/schema.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCryptographicHash>

#include <algorithm>
#include <ranges>

namespace hikari::backends::ai {

using api::Json;

namespace {

constexpr std::size_t kNameLimit = 64;

std::string stringAt(const Json &json, std::string_view key)
{
    const Json *value = json.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

void failWith(SidecarRequest &request, std::string_view code, std::string message, Json data = {})
{
    auto error = Json::object({{"code", std::string(code)}});
    if (!data.isNull())
        error["data"] = std::move(data);
    request.fail(rpc::kHikariApiError, std::move(message), std::move(error));
}

std::string at(const std::string &path)
{
    return path.empty() ? std::string("its root") : path;
}

void run(const PluginTools::Run &runner, const PluginToolEntry &tool, const Json &arguments,
         std::shared_ptr<SidecarRequest> request)
{
    if (!runner)
        return failWith(*request, api::errors::kUnavailable, tool.pluginName + " isn't running.");
    runner(tool, arguments, [request, tool](std::expected<Json, api::Error> answer) {
        if (!request->open())
            return;
        if (!answer)
            return failWith(*request, answer.error().code, std::move(answer.error().message), std::move(answer.error().data));
        if (!tool.result.isNull())
            if (auto failure = api::validate(tool.result, *answer, Json::object()))
                return failWith(*request, api::errors::kUnavailable,
                                tool.pluginName + "'s answer doesn't match its result schema at " + at(failure->path) +
                                    ": " + failure->message);
        request->respond(Json::object({{"result", std::move(*answer)}}));
    });
}

}

std::string pluginToolName(const std::string &plugin, const std::string &command)
{
    const QByteArray hash =
        QCryptographicHash::hash(QByteArray::fromStdString(plugin + "/" + command), QCryptographicHash::Sha256).toHex();
    std::string name = "p_" + hash.left(8).toStdString() + "_" + command;
    std::ranges::replace(name, '-', '_');
    if (name.size() > kNameLimit)
        name.resize(kNameLimit);
    return name;
}

Json pluginToolCallParams(const PluginToolEntry &tool, const Json &arguments)
{
    return Json::object({{"plugin", tool.plugin},
                         {"pluginName", tool.pluginName},
                         {"command", tool.command},
                         {"tool", tool.name},
                         {"arguments", arguments},
                         {"readOnly", tool.readOnly()}});
}

PluginTools::PluginTools(QObject *parent) : QObject(parent) {}

PluginTools::~PluginTools()
{
    *m_alive = false;
}

void PluginTools::setTools(std::vector<PluginToolEntry> tools)
{
    if (tools == m_tools)
        return;
    m_tools = std::move(tools);
    announce();
}

const PluginToolEntry *PluginTools::find(const std::string &name) const
{
    const auto it = std::ranges::find(m_tools, name, &PluginToolEntry::name);
    return it == m_tools.end() ? nullptr : &*it;
}

void PluginTools::announce()
{
    if (!m_host || m_host->state() != AiSidecarHost::State::Ready)
        return;
    if (m_tools.empty() && !m_sidecarHasTools)
        return;
    m_sidecarHasTools = !m_tools.empty();
    Json tools = Json::array();
    for (const PluginToolEntry &tool : m_tools)
        tools.push(Json::object({{"name", tool.name},
                                 {"description", tool.pluginName + ": " + tool.description},
                                 {"inputSchema", tool.parameters},
                                 {"readOnly", tool.readOnly()}}));
    m_host->request(kPluginToolsSet, Json::object({{"tools", std::move(tools)}}), [](SidecarResult) {});
}

void PluginTools::attach(AiSidecarHost &host)
{
    m_host = &host;
    connect(&host, &AiSidecarHost::ready, this, [this] {
        m_sidecarHasTools = false;
        announce();
    });
    host.handle(kPluginToolsCall, [this, alive = std::weak_ptr(m_alive), host = QPointer(&host)](
                                      const Json &params, std::shared_ptr<SidecarRequest> request) {
        if (!alive.lock() || !host)
            return request->fail(rpc::kBusy, "gone");
        const Json *arguments = params.find("arguments");
        const PluginToolEntry *found = find(stringAt(params, "tool"));
        if (!found || !arguments || !arguments->isObject())
            return request->fail(rpc::kInvalidParams,
                                 std::string(kPluginToolsCall) + " needs a plugin tool's name and its arguments");
        if (auto failure = api::validate(found->parameters, *arguments, Json::object()))
            return failWith(*request, api::errors::kInvalidParams,
                            found->name + "'s arguments don't fit at " + at(failure->path) + ": " + failure->message);
        const std::string session = stringAt(params, "sessionId");
        if (session.empty()) {
            if (!found->readOnly())
                return failWith(*request, api::errors::kNotGranted,
                                "HikariSub's MCP server runs only the read tools of plugins whose marks you trust.");
            return run(m_run, *found, *arguments, std::move(request));
        }
        host->gateSession(session, std::string(kPluginToolOperation), pluginToolCallParams(*found, *arguments), request,
                          [this, alive, tool = *found, args = *arguments, request](SidecarRequest &) {
                              if (alive.lock())
                                  run(m_run, tool, args, request);
                          });
    });
}

}
