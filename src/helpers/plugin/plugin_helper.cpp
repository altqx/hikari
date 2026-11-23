#include "hikari/backends/helper_endpoint.h"
#include "hikari/backends/plugin_protocol.h"
#include "plugin_state.h"

#include <memory>

using namespace hikari::backends::helper;
namespace protocol = hikari::backends::plugin;
using hikari::api::Json;
using hikari::plugin_helper::Area;
using hikari::plugin_helper::Host;
using hikari::plugin_helper::Level;
using hikari::plugin_helper::Options;
using hikari::plugin_helper::PluginState;
using hikari::plugin_helper::Tier;

namespace {

const std::string kStopped = "HikariSub stopped waiting for this plugin";

class ServiceHost : public Host {
public:
    explicit ServiceHost(Responder &responder) : m_responder(responder) {}

    std::expected<Json, Json> call(const std::string &operation, const Json &params) override
    {
        return envelope(protocol::Service::Call, operation, params);
    }

    std::expected<Json, Json> request(Area area, const std::string &operation, const Json &params) override
    {
        switch (area) {
        case Area::Files: return envelope(protocol::Service::Files, operation, params);
        case Area::Net: return envelope(protocol::Service::Net, operation, params);
        case Area::Job: return envelope(protocol::Service::Job, operation, params);
        case Area::Timer: return envelope(protocol::Service::Timer, operation, params);
        case Area::Ui: break;
        }
        return envelope(protocol::Service::Ui, operation, params);
    }

    void notify(Level level, bool toast, const std::string &text) override
    {
        Writer w;
        w.i32(static_cast<std::int32_t>(protocol::Service::Notify))
            .i32(static_cast<std::int32_t>(level))
            .u8(toast ? 1 : 0)
            .str(text);
        (void)m_responder.call(w.take());
    }

    std::expected<void, std::string> setSetting(const std::string &id, const Json &value) override
    {
        Writer w;
        w.i32(static_cast<std::int32_t>(protocol::Service::SetSetting)).str(id).str(value.dump());
        return refusal(w.take());
    }

    std::expected<void, std::string> subscribe(const std::string &event) override
    {
        Writer w;
        w.i32(static_cast<std::int32_t>(protocol::Service::Subscribe)).str(event);
        return refusal(w.take());
    }

    std::expected<void, std::string> lock() override
    {
        Writer w;
        w.i32(static_cast<std::int32_t>(protocol::Service::Lock));
        return refusal(w.take());
    }

    void unlock() override
    {
        Writer w;
        w.i32(static_cast<std::int32_t>(protocol::Service::Unlock));
        (void)m_responder.call(w.take());
    }

private:
    std::expected<Json, Json> envelope(protocol::Service service, const std::string &operation, const Json &params)
    {
        Writer w;
        w.i32(static_cast<std::int32_t>(service)).str(operation).str(params.dump());
        const auto reply = answerText(w.take());
        const auto parsed = Json::parse(reply ? *reply : std::string());
        if (!parsed || !parsed->isObject())
            return std::unexpected(Json::object({{"code", "unavailable"}, {"message", kStopped}}));
        if (const Json *result = parsed->find("result"))
            return *result;
        const Json *error = parsed->find("error");
        return std::unexpected(error ? *error : Json::object({{"code", "unavailable"}, {"message", kStopped}}));
    }

    std::expected<std::string, Outcome> answerText(std::vector<std::byte> payload)
    {
        auto answer = m_responder.call(std::move(payload));
        if (!answer)
            return std::unexpected(answer.error());
        Reader in(*answer);
        std::string text = in.str();
        if (!in.ok() || !in.atEnd())
            return std::unexpected(Outcome::InvalidInput);
        return text;
    }

    std::expected<void, std::string> refusal(std::vector<std::byte> payload)
    {
        auto refused = answerText(std::move(payload));
        if (!refused)
            return std::unexpected(kStopped);
        if (!refused->empty())
            return std::unexpected(*refused);
        return {};
    }

    Responder &m_responder;
};

std::unique_ptr<PluginState> g_state;
bool g_loaded = false;

void load(Reader &in, Responder &r)
{
    if (g_loaded)
        return r.terminal(Outcome::InvalidInput, bytesOf("this helper has loaded its plugin already"));
    auto request = protocol::decodeLoad(in);
    if (!request)
        return r.terminal(Outcome::InvalidInput);
    auto settings = Json::parse(request->settings);
    if (!settings || !settings->isObject())
        return r.terminal(Outcome::InvalidInput);
    g_loaded = true;
    Options options;
    options.root = std::filesystem::path(std::u8string(request->root.begin(), request->root.end()));
    options.main = request->main;
    options.id = request->id;
    options.version = request->version;
    options.tier = request->tier == protocol::Tier::Unrestricted ? Tier::Unrestricted : Tier::Restricted;
    options.memoryLimit = static_cast<std::size_t>(request->memoryLimit);
    options.locale = request->locale;
    options.settings = std::move(*settings);
    options.commands = std::move(request->commands);
    options.exporters = std::move(request->exporters);
    options.importers = std::move(request->importers);
    ServiceHost host(r);
    auto state = PluginState::load(std::move(options), host);
    if (!state)
        return r.terminal(Outcome::Failed, bytesOf(state.error()));
    g_state = std::move(*state);
    r.terminal(Outcome::Ok, protocol::encodeStrings(g_state->handledCommands()));
}

void finish(Responder &r, const std::expected<void, std::string> &done)
{
    if (done)
        r.terminal(Outcome::Ok);
    else
        r.terminal(Outcome::Failed, bytesOf(done.error()));
}

void exportDocument(Reader &in, Responder &r)
{
    const std::string format = in.str();
    const auto snapshot = Json::parse(in.str());
    const auto options = Json::parse(in.str());
    if (!in.ok() || !in.atEnd() || !snapshot || !options)
        return r.terminal(Outcome::InvalidInput);
    ServiceHost host(r);
    auto bytes = g_state->exportDocument(format, *snapshot, *options, host);
    if (!bytes)
        return r.terminal(Outcome::Failed, bytesOf(bytes.error()));
    r.terminal(Outcome::Ok, bytesOf(*bytes));
}

void importFile(Reader &in, Responder &r)
{
    const std::string format = in.str();
    const std::string data = in.str();
    const std::string name = in.str();
    if (!in.ok() || !in.atEnd())
        return r.terminal(Outcome::InvalidInput);
    ServiceHost host(r);
    auto result = g_state->importFile(format, data, name, host);
    if (!result)
        return r.terminal(Outcome::Failed, bytesOf(result.error()));
    r.terminal(Outcome::Ok, bytesOf(result->dump()));
}

}

int main()
{
    const RequestHandler onRequest = [](const Frame &request, Responder &r) {
        Reader in(request.payload);
        const auto command = static_cast<protocol::Command>(in.i32());
        if (command == protocol::Command::Load)
            return load(in, r);
        if (!g_state)
            return r.terminal(Outcome::Unsupported);
        if (command == protocol::Command::Timer) {
            const std::int64_t timer = in.i64();
            const bool last = in.u8() != 0;
            if (!in.ok() || !in.atEnd())
                return r.terminal(Outcome::InvalidInput);
            ServiceHost host(r);
            return finish(r, g_state->fire(timer, last, host));
        }
        if (command == protocol::Command::Export)
            return exportDocument(in, r);
        if (command == protocol::Command::Import)
            return importFile(in, r);
        if (command != protocol::Command::Run)
            return r.terminal(Outcome::Unsupported);
        const std::string id = in.str();
        auto call = Json::parse(in.str());
        if (!in.ok() || !in.atEnd() || !call || !call->isObject())
            return r.terminal(Outcome::InvalidInput);
        ServiceHost host(r);
        finish(r, g_state->run(id, *call, host));
    };
    const RequestHandler onEvent = [](const Frame &event, Responder &r) {
        if (!g_state)
            return r.terminal(Outcome::Unsupported);
        Reader in(event.payload);
        const std::string name = in.str();
        auto data = Json::parse(in.str());
        if (!in.ok() || !in.atEnd() || !data)
            return r.terminal(Outcome::InvalidInput);
        ServiceHost host(r);
        finish(r, g_state->dispatch(name, *data, host));
    };
    return runHelper(protocol::kHelperName, protocol::kProtocolVersion, onRequest, onEvent);
}
