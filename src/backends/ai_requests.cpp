#include "hikari/backends/ai_requests.h"

#include "hikari/backends/ai_sidecar_host.h"

#include <QPointer>

#include <algorithm>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;
using api::Json;

namespace {

std::string stringAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

std::int64_t intAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isNumber() ? static_cast<std::int64_t>(value->asNumber()) : 0;
}

std::optional<double> numberAt(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value && value->isNumber() ? std::optional(value->asNumber()) : std::nullopt;
}

const Json &arrayAt(const Json &object, std::string_view key)
{
    static const Json empty = Json::array();
    const Json *value = object.find(key);
    return value && value->isArray() ? *value : empty;
}

Json providersJson(const app::Config &config, std::optional<app::SearchService> search)
{
    Json providers = Json::array();
    for (const app::Provider &p : config.providers)
        providers.push(Json::object({{"id", p.id},
                                     {"preset", app::presetId(p.preset)},
                                     {"wire", app::wireId(p.wire)},
                                     {"baseUrl", p.baseUrl}}));
    Json params = Json::object({{"providers", std::move(providers)}});
    if (search)
        params["searchService"] = std::string(app::searchServiceId(*search));
    return params;
}

app::Usage usageOf(const Json &usage)
{
    return {.inputTokens = intAt(usage, "inputTokens"),
            .outputTokens = intAt(usage, "outputTokens"),
            .cachedInputTokens = intAt(usage, "cachedInputTokens"),
            .cacheWriteTokens = intAt(usage, "cacheWriteTokens"),
            .cacheWriteHourTokens = intAt(usage, "cacheWriteHourTokens"),
            .reportedCost = numberAt(usage, "reportedCost")};
}

app::Finished finishedOf(const Json &result)
{
    app::Finished finished;
    finished.reason = app::stopReasonFromId(stringAt(result, "reason")).value_or(app::StopReason::End);
    finished.text = stringAt(result, "text");
    for (const Json &call : arrayAt(result, "toolCalls").asArray())
        finished.toolCalls.push_back({.id = stringAt(call, "id"),
                                      .name = stringAt(call, "name"),
                                      .argumentsJson = stringAt(call, "argumentsJson"),
                                      .signature = stringAt(call, "signature")});
    if (const Json *usage = result.find("usage"))
        finished.usage = usageOf(*usage);
    for (const Json &c : arrayAt(result, "citations").asArray())
        finished.citations.push_back({.index = static_cast<int>(intAt(c, "index")),
                                      .url = stringAt(c, "url"),
                                      .title = stringAt(c, "title"),
                                      .quote = stringAt(c, "quote")});
    finished.searchSuggestions = stringAt(result, "searchSuggestions");
    if (const Json *state = result.find("providerState"); state && state->isObject())
        finished.providerState = state->dump();
    return finished;
}

std::optional<app::Capabilities> capabilitiesOf(const Json &model)
{
    const Json *c = model.find("capabilities");
    if (!c || !c->isObject())
        return std::nullopt;
    const auto flag = [&](std::string_view key) {
        const Json *value = c->find(key);
        return value && value->isBool() && value->asBool();
    };
    return app::Capabilities{.tools = flag("tools"), .vision = flag("vision"), .video = flag("video")};
}

std::vector<app::Model> modelsOf(const Json &result)
{
    std::vector<app::Model> models;
    for (const Json &m : arrayAt(result, "models").asArray()) {
        app::Model model{.id = stringAt(m, "id"), .name = stringAt(m, "name"), .capabilities = capabilitiesOf(m)};
        if (model.id.empty())
            continue;
        if (const Json *price = m.find("price"); price && price->isObject())
            model.price = app::Price{.input = numberAt(*price, "input").value_or(0),
                                     .output = numberAt(*price, "output").value_or(0),
                                     .cachedInput = numberAt(*price, "cachedInput"),
                                     .cacheWrite = numberAt(*price, "cacheWrite"),
                                     .cacheWriteHour = numberAt(*price, "cacheWriteHour")};
        if (m.find("contextTokens"))
            model.contextTokens = intAt(m, "contextTokens");
        models.push_back(std::move(model));
    }
    return models;
}

}

app::Error requestError(const SidecarFailure &failure)
{
    if (failure.error == SidecarError::Cancelled ||
        (failure.error == SidecarError::Remote && failure.code == rpc::kRequestCancelled))
        return {.kind = app::ErrorKind::Cancelled};
    if (failure.error == SidecarError::Remote && failure.code == rpc::kProviderFailed)
        return {.kind = app::errorKindFromId(stringAt(failure.data, "kind")).value_or(app::ErrorKind::Server),
                .httpStatus = static_cast<int>(intAt(failure.data, "httpStatus")),
                .detail = stringAt(failure.data, "detail")};
    if (failure.error == SidecarError::Remote)
        return {.kind = app::ErrorKind::Malformed, .detail = failure.message};
    return {.kind = app::ErrorKind::Network, .detail = failure.message};
}

struct AiRequests::Pending {
    app::EventSink sink;
    QPointer<AiSidecarHost> host;
    std::uint64_t request = 0;
    bool sent = false;
};

AiRequests::AiRequests(app::Providers &providers, HostFor host) : m_providers(providers), m_host(std::move(host)) {}

AiRequests::~AiRequests()
{
    *m_alive = false;
    for (const auto &[id, pending] : m_pending)
        if (pending->sent && pending->host)
            pending->host->cancel(pending->request);
}

AiSidecarHost *AiRequests::connect(const app::Config &config)
{
    AiSidecarHost *host = m_host();
    if (!host)
        return nullptr;
    host->onNotification([this, alive = std::weak_ptr(m_alive)](const std::string &method, const Json &params) {
        if (alive.lock())
            receive(method, params);
    });
    host->request("_hikari/providers.set", providersJson(config, searchService()), [](SidecarResult) {});
    return host;
}

app::StreamId AiRequests::complete(app::Role role, Json request, app::EventSink sink)
{
    const app::StreamId id = m_nextId++;
    if (!m_providers.enabled()) {
        sink(app::Failed{{.kind = app::ErrorKind::NoProvider}});
        return id;
    }
    const std::optional<app::ModelRef> ref = m_providers.config().modelFor(role);
    if (!ref) {
        sink(app::Failed{{.kind = app::ErrorKind::NoModel}});
        return id;
    }
    request["model"] = ref->modelId;
    if (!m_caching)
        request["caching"] = false;
    if (!m_webSearch && request.isObject())
        std::erase_if(request.asObject(), [](const Json::Member &m) { return m.key == "webSearch"; });
    AiSidecarHost *host = connect();
    if (!host) {
        sink(app::Failed{{.kind = app::ErrorKind::Network, .detail = "the AI sidecar is not installed"}});
        return id;
    }
    auto pending = std::make_shared<Pending>();
    pending->sink = std::move(sink);
    pending->host = host;
    pending->sent = true;
    m_pending[id] = pending;
    Json params = Json::object({{"stream", id}, {"providerId", ref->providerId}, {"request", std::move(request)}});
    pending->request = host->request("_hikari/complete", std::move(params),
                                     [this, alive = std::weak_ptr(m_alive), id](SidecarResult result) {
                                         if (!alive.lock())
                                             return;
                                         if (result)
                                             end(id, finishedOf(*result));
                                         else
                                             end(id, app::Failed{requestError(result.error())});
                                     });
    return id;
}

void AiRequests::setWebSearch(bool on, std::optional<app::SearchService> service)
{
    m_webSearch = on;
    m_searchService = service;
}

void AiRequests::testSearch(app::SearchService service, std::optional<std::string> typedKey,
                            std::function<void(std::optional<app::Error>)> done)
{
    AiSidecarHost *host = connect();
    if (!host) {
        done(app::Error{.kind = app::ErrorKind::Network, .detail = "the AI sidecar is not installed"});
        return;
    }
    const std::uint64_t test = ++m_lastAttempt;
    m_searchTests.insert_or_assign(service, SearchTest{std::move(typedKey), test});
    host->request("_hikari/search.test", Json::object({{"service", std::string(app::searchServiceId(service))}}),
                  [this, alive = std::weak_ptr(m_alive), service, test, done = std::move(done)](SidecarResult result) {
                      if (!alive.lock())
                          return;
                      if (const auto it = m_searchTests.find(service);
                          it != m_searchTests.end() && it->second.generation == test)
                          m_searchTests.erase(it);
                      done(result ? std::nullopt : std::optional(requestError(result.error())));
                  });
}

std::optional<SearchAccess> AiRequests::searchAccess(app::SearchService service) const
{
    if (const auto it = m_searchTests.find(service); it != m_searchTests.end())
        return SearchAccess{it->second.typedKey};
    if (searchService() == service)
        return SearchAccess{};
    return std::nullopt;
}

void AiRequests::cancel(app::StreamId id)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    if (it->second->sent && it->second->host) {
        it->second->host->cancel(it->second->request);
        return;
    }
    end(id, app::Failed{{.kind = app::ErrorKind::Cancelled}});
}

void AiRequests::refreshModels(const std::string &providerId, std::function<void(std::optional<app::Error>)> done)
{
    if (!m_providers.config().provider(providerId)) {
        done(app::Error{.kind = app::ErrorKind::NoProvider});
        return;
    }
    m_providers.readKey(providerId, [this, alive = std::weak_ptr(m_alive), providerId,
                                     done = std::move(done)](std::optional<std::string> key) {
        if (!alive.lock())
            return;
        if (!key) {
            done(app::Error{.kind = app::ErrorKind::Key});
            return;
        }
        listModels(providerId, std::move(done));
    });
}

void AiRequests::listModels(const std::string &providerId, std::function<void(std::optional<app::Error>)> done)
{
    AiSidecarHost *host = connect();
    if (!host) {
        done(app::Error{.kind = app::ErrorKind::Network, .detail = "the AI sidecar is not installed"});
        return;
    }
    host->request("_hikari/models.list", Json::object({{"providerId", providerId}}),
                  [this, alive = std::weak_ptr(m_alive), providerId, done = std::move(done)](SidecarResult result) {
                      if (!alive.lock())
                          return;
                      if (!result) {
                          done(requestError(result.error()));
                          return;
                      }
                      if (!m_providers.config().provider(providerId)) {
                          done(app::Error{.kind = app::ErrorKind::NoProvider});
                          return;
                      }
                      if (!m_providers.setModels(providerId, modelsOf(*result)))
                          done(app::Error{.kind = app::ErrorKind::Rejected, .detail = "config"});
                      else
                          done(std::nullopt);
                  });
}

void AiRequests::probe(const app::Provider &provider, const std::string &key, std::function<void(Probed)> done)
{
    app::Config config = m_providers.config();
    const auto it = std::ranges::find(config.providers, provider.id, &app::Provider::id);
    if (it == config.providers.end())
        config.providers.push_back(provider);
    else
        *it = provider;
    AiSidecarHost *host = connect(config);
    if (!host) {
        done(app::Error{.kind = app::ErrorKind::Network, .detail = "the AI sidecar is not installed"});
        return;
    }
    const std::uint64_t draft = ++m_lastAttempt;
    m_drafts.insert_or_assign(provider.id, Draft{provider, key, draft});
    host->request("_hikari/models.list", Json::object({{"providerId", provider.id}}),
                  [this, alive = std::weak_ptr(m_alive), id = provider.id, draft,
                   done = std::move(done)](SidecarResult result) {
                      if (!alive.lock())
                          return;
                      if (const auto it = m_drafts.find(id); it != m_drafts.end() && it->second.generation == draft)
                          m_drafts.erase(it);
                      if (result)
                          done(modelsOf(*result));
                      else
                          done(requestError(result.error()));
                  });
}

RelayTargets AiRequests::relayTargets()
{
    return [this, saved = providerTargets(m_providers)](const Json &target, std::function<void(RelayTargetResult)> done) {
        const Json *kind = target.find("kind");
        const Json *id = target.find("id");
        if (kind && *kind == Json("provider") && id && id->isString())
            if (const auto it = m_drafts.find(id->asString()); it != m_drafts.end())
                return done(RelayTarget{QUrl(QString::fromStdString(it->second.provider.baseUrl)), it->second.key});
        saved(target, std::move(done));
    };
}

void AiRequests::endConversation(const std::string &providerId, const std::string &cacheKey)
{
    if (!m_providers.config().provider(providerId) || cacheKey.empty())
        return;
    if (AiSidecarHost *host = connect())
        host->request("_hikari/conversation.end", Json::object({{"providerId", providerId}, {"cacheKey", cacheKey}}),
                      [](SidecarResult) {});
}

void AiRequests::receive(const std::string &method, const Json &params)
{
    if (method != "session/update")
        return;
    const Json *stream = params.find("stream");
    const Json *update = params.find("update");
    if (!stream || !stream->isInt() || !update)
        return;
    const auto it = m_pending.find(static_cast<app::StreamId>(stream->asInt()));
    if (it == m_pending.end())
        return;
    const std::string kind = stringAt(*update, "kind");
    const int index = static_cast<int>(intAt(*update, "index"));
    if (kind == "text")
        it->second->sink(app::TextDelta{stringAt(*update, "text")});
    else if (kind == "toolCall")
        it->second->sink(app::ToolCallStarted{index, stringAt(*update, "id"), stringAt(*update, "name")});
    else if (kind == "toolArguments")
        it->second->sink(app::ToolCallArguments{index, stringAt(*update, "delta")});
}

void AiRequests::end(app::StreamId id, const app::Event &event)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const std::shared_ptr<Pending> pending = it->second;
    m_pending.erase(it);
    pending->sink(event);
}

}
