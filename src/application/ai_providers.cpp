#include "hikari/application/ai_providers.h"

#include <algorithm>
#include <utility>

namespace hikari::application::ai {

namespace {

template <typename Enum, std::size_t N>
std::optional<Enum> fromId(const std::pair<Enum, std::string_view> (&table)[N], std::string_view id)
{
    for (const auto &[value, name] : table)
        if (name == id)
            return value;
    return std::nullopt;
}

template <typename Enum, std::size_t N>
std::string_view toId(const std::pair<Enum, std::string_view> (&table)[N], Enum value)
{
    for (const auto &[v, name] : table)
        if (v == value)
            return name;
    return {};
}

constexpr std::pair<Wire, std::string_view> kWires[] = {
    {Wire::ChatCompletions, "chat-completions"},
    {Wire::Responses, "responses"},
    {Wire::AnthropicMessages, "anthropic-messages"},
};
constexpr std::pair<Preset, std::string_view> kPresets[] = {
    {Preset::OpenRouter, "openrouter"},
    {Preset::OpenAI, "openai"},
    {Preset::Anthropic, "anthropic"},
    {Preset::Custom, "custom"},
};
constexpr std::pair<Role, std::string_view> kRoleIds[] = {
    {Role::Chat, "chat"}, {Role::Translator, "translator"}, {Role::Reviewer, "reviewer"},
    {Role::Vision, "vision"}, {Role::Judge, "judge"},
};

SaveError saveErrorOf(KeyError error)
{
    switch (error) {
    case KeyError::Unavailable:
        return SaveError::KeychainUnavailable;
    case KeyError::Denied:
        return SaveError::KeychainDenied;
    case KeyError::NotFound:
    case KeyError::Other:
        break;
    }
    return SaveError::Key;
}

}

std::string_view wireId(Wire wire) { return toId(kWires, wire); }
std::optional<Wire> wireFromId(std::string_view id) { return fromId(kWires, id); }
std::string_view presetId(Preset preset) { return toId(kPresets, preset); }
std::optional<Preset> presetFromId(std::string_view id) { return fromId(kPresets, id); }
std::string_view roleId(Role role) { return toId(kRoleIds, role); }
std::optional<Role> roleFromId(std::string_view id) { return fromId(kRoleIds, id); }

Provider presetProvider(Preset preset, std::string id, std::string name)
{
    Provider p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.preset = preset;
    switch (preset) {
    case Preset::OpenRouter:
        p.wire = Wire::ChatCompletions;
        p.baseUrl = "https://openrouter.ai/api/v1";
        p.defaults = {.tools = false, .vision = false};
        break;
    case Preset::OpenAI:
        p.wire = Wire::Responses;
        p.baseUrl = "https://api.openai.com/v1";
        p.defaults = {.tools = true, .vision = true};
        break;
    case Preset::Anthropic:
        p.wire = Wire::AnthropicMessages;
        p.baseUrl = "https://api.anthropic.com/v1";
        p.defaults = {.tools = true, .vision = true};
        break;
    case Preset::Custom:
        p.wire = Wire::ChatCompletions;
        p.needsKey = false;
        break;
    }
    return p;
}

Capabilities capabilitiesOf(const Provider &provider, std::string_view modelId)
{
    if (const auto it = provider.capabilityOverrides.find(modelId); it != provider.capabilityOverrides.end())
        return it->second;
    for (const Model &model : provider.models)
        if (model.id == modelId && model.capabilities)
            return *model.capabilities;
    return provider.defaults;
}

const Provider *Config::provider(std::string_view id) const
{
    const auto it = std::ranges::find(providers, id, &Provider::id);
    return it == providers.end() ? nullptr : &*it;
}

std::optional<ModelRef> Config::modelFor(Role role) const
{
    for (const Role r : {role, Role::Chat}) {
        const auto it = roles.find(r);
        if (it != roles.end() && provider(it->second.providerId))
            return it->second;
    }
    return std::nullopt;
}

std::optional<double> costOf(const Usage &usage, const std::optional<Price> &price)
{
    if (usage.reportedCost)
        return usage.reportedCost;
    if (!price)
        return std::nullopt;
    const double cached = static_cast<double>(usage.cachedInputTokens);
    const double writtenHour = static_cast<double>(usage.cacheWriteHourTokens);
    const double written = static_cast<double>(usage.cacheWriteTokens - usage.cacheWriteHourTokens);
    const double uncached = static_cast<double>(usage.inputTokens - usage.cachedInputTokens - usage.cacheWriteTokens);
    const double writePrice = price->cacheWrite.value_or(price->input);
    return (uncached * price->input + cached * price->cachedInput.value_or(price->input) + written * writePrice +
            writtenHour * price->cacheWriteHour.value_or(writePrice) +
            static_cast<double>(usage.outputTokens) * price->output) /
           1'000'000.0;
}

std::chrono::seconds cacheTtl(CacheLifetime lifetime)
{
    return lifetime == CacheLifetime::Hour ? std::chrono::hours(1) : std::chrono::minutes(5);
}

void ProviderPort::createCache(const Provider &, const std::string &, const Request &, std::chrono::seconds,
                               std::function<void(CacheResult)> done)
{
    done(Error{.kind = ErrorKind::Rejected, .httpStatus = 0, .detail = "no explicit caches"});
}

void ProviderPort::deleteCache(const Provider &, const std::string &, const std::string &) {}

struct Providers::Pending {
    EventSink sink;
    std::optional<StreamId> portId;
    bool cancelled = false;
};


Providers::Providers(ConfigStore &configs, KeyStore &keys, ProviderPort &port)
    : m_configs(configs), m_keys(keys), m_port(port), m_config(configs.load())
{
}

void Providers::add(Provider provider, std::string key, std::function<void(std::optional<SaveError>)> done)
{
    if (provider.needsKey && key.empty()) {
        done(SaveError::Key);
        return;
    }
    auto commit = [this, provider, key, done](std::optional<KeyError> error) mutable {
        if (error) {
            done(saveErrorOf(*error));
            return;
        }
        Config next = m_config;
        std::erase_if(next.providers, [&](const Provider &p) { return p.id == provider.id; });
        next.providers.push_back(provider);
        if (!next.roles.contains(Role::Chat) && !provider.models.empty())
            next.roles[Role::Chat] = {provider.id, provider.models.front().id};
        if (!m_configs.save(next)) {
            if (!key.empty())
                m_keys.remove(provider.id, [](std::optional<KeyError>) {});
            done(SaveError::Config);
            return;
        }
        m_config = std::move(next);
        if (key.empty())
            m_keyCache.erase(provider.id);
        else
            m_keyCache[provider.id] = key;
        done(std::nullopt);
    };
    if (key.empty())
        commit(std::nullopt);
    else
        m_keys.write(provider.id, key, std::move(commit));
}

void Providers::remove(const std::string &providerId, std::function<void(std::optional<SaveError>)> done)
{
    Config next = m_config;
    std::erase_if(next.providers, [&](const Provider &p) { return p.id == providerId; });
    std::erase_if(next.roles, [&](const auto &entry) { return entry.second.providerId == providerId; });
    if (!m_configs.save(next)) {
        done(SaveError::Config);
        return;
    }
    std::vector<std::pair<std::string, std::string>> caches;
    for (const auto &[cacheKey, cache] : m_caches)
        if (cache.providerId == providerId)
            caches.emplace_back(cacheKey, cache.name);
    for (const auto &[cacheKey, name] : caches)
        forgetCache(cacheKey, name);
    m_config = std::move(next);
    m_keyCache.erase(providerId);
    m_keys.remove(providerId, [done](std::optional<KeyError> error) {
        done(error && *error != KeyError::NotFound ? std::optional(saveErrorOf(*error)) : std::nullopt);
    });
}

bool Providers::assign(Role role, ModelRef model)
{
    if (!m_config.provider(model.providerId) || model.modelId.empty())
        return false;
    Config next = m_config;
    next.roles[role] = std::move(model);
    if (!m_configs.save(next))
        return false;
    m_config = std::move(next);
    return true;
}

bool Providers::setCapabilities(const std::string &providerId, const std::string &modelId, Capabilities capabilities)
{
    Config next = m_config;
    const auto it = std::ranges::find(next.providers, providerId, &Provider::id);
    if (it == next.providers.end())
        return false;
    it->capabilityOverrides[modelId] = capabilities;
    if (!m_configs.save(next))
        return false;
    m_config = std::move(next);
    return true;
}

void Providers::withKey(const Provider &provider,
                        std::function<void(std::optional<std::string>, std::optional<KeyError>)> next)
{
    if (!provider.needsKey) {
        next(std::string(), std::nullopt);
        return;
    }
    if (const auto it = m_keyCache.find(provider.id); it != m_keyCache.end()) {
        next(it->second, std::nullopt);
        return;
    }
    m_keys.read(provider.id, [this, id = provider.id, next = std::move(next)](KeyResult result) {
        if (const auto *error = std::get_if<KeyError>(&result)) {
            next(std::nullopt, *error);
            return;
        }
        m_keyCache[id] = std::get<std::string>(result);
        next(std::get<std::string>(result), std::nullopt);
    });
}

void Providers::refreshModels(const std::string &providerId, std::function<void(std::optional<Error>)> done)
{
    const Provider *provider = m_config.provider(providerId);
    if (!provider) {
        done(Error{.kind = ErrorKind::NoProvider});
        return;
    }
    withKey(*provider, [this, providerId, done](std::optional<std::string> key, std::optional<KeyError>) {
        const Provider *current = m_config.provider(providerId);
        if (!current) {
            done(Error{.kind = ErrorKind::NoProvider});
            return;
        }
        if (!key) {
            done(Error{.kind = ErrorKind::Key});
            return;
        }
        m_port.listModels(*current, *key, [this, providerId, done](ProviderPort::ModelsResult result) {
            if (const auto *error = std::get_if<Error>(&result)) {
                done(*error);
                return;
            }
            Config next = m_config;
            const auto it = std::ranges::find(next.providers, providerId, &Provider::id);
            if (it == next.providers.end()) {
                done(Error{.kind = ErrorKind::NoProvider});
                return;
            }
            it->models = std::move(std::get<std::vector<Model>>(result));
            if (!m_configs.save(next)) {
                done(Error{.kind = ErrorKind::Rejected, .detail = "config"});
                return;
            }
            m_config = std::move(next);
            done(std::nullopt);
        });
    });
}

StreamId Providers::stream(Role role, Request request, EventSink sink)
{
    const StreamId id = m_nextId++;
    if (!enabled()) {
        sink(Failed{{.kind = ErrorKind::NoProvider}});
        return id;
    }
    const std::optional<ModelRef> ref = m_config.modelFor(role);
    if (!ref) {
        sink(Failed{{.kind = ErrorKind::NoModel}});
        return id;
    }
    const Provider provider = *m_config.provider(ref->providerId);
    request.model = ref->modelId;
    auto pending = std::make_shared<Pending>();
    pending->sink = std::move(sink);
    m_pending[id] = pending;
    withKey(provider, [this, id, provider, request = std::move(request), pending](std::optional<std::string> key,
                                                                                   std::optional<KeyError>) {
        if (pending->cancelled)
            return;
        if (!key) {
            m_pending.erase(id);
            pending->sink(Failed{{.kind = ErrorKind::Key}});
            return;
        }
        if (!request.explicitCache || request.cacheKey.empty()) {
            send(id, provider, *key, request, pending);
            return;
        }
        withCache(provider, *key, request, [this, id, provider, key = *key, pending](const Request &cached) {
            send(id, provider, key, cached, pending);
        });
    });
    return id;
}

void Providers::send(StreamId id, const Provider &provider, const std::string &key, const Request &request,
                     const std::shared_ptr<Pending> &pending)
{
    if (pending->cancelled)
        return;
    pending->portId = m_port.stream(
        provider, key, request,
        [this, id, pending, cacheKey = request.cacheKey, cache = request.providerCache](const Event &event) {
            if (const auto *failed = std::get_if<Failed>(&event);
                failed && !cache.empty() && failed->error.kind != ErrorKind::Cancelled)
                forgetCache(cacheKey, cache);
            if (std::holds_alternative<Finished>(event) || std::holds_alternative<Failed>(event))
                m_pending.erase(id);
            pending->sink(event);
        });
}

bool Providers::Cache::holds(const Provider &provider, const Request &request) const
{
    return providerId == provider.id && model == request.model && system == request.system && tools == request.tools &&
           context == request.context;
}

void Providers::withCache(const Provider &provider, const std::string &key, Request request,
                          std::function<void(Request)> next)
{
    constexpr auto kRenewBefore = std::chrono::seconds(30);
    const auto now = m_now();
    if (const auto it = m_caches.find(request.cacheKey); it != m_caches.end()) {
        const Cache &cache = it->second;
        if (cache.holds(provider, request) && cache.name.empty()) {
            next(std::move(request));
            return;
        }
        if (cache.holds(provider, request) && now + kRenewBefore < cache.expires) {
            request.providerCache = cache.name;
            next(std::move(request));
            return;
        }
        forgetCache(it->first, cache.name);
    }
    const std::chrono::seconds ttl = cacheTtl(request.cacheLifetime);
    Cache entry{.providerId = provider.id,
                .model = request.model,
                .system = request.system,
                .tools = request.tools,
                .context = request.context,
                .name = {},
                .expires = now + ttl};
    m_port.createCache(provider, key, request, ttl,
                       [this, entry = std::move(entry), request, next = std::move(next)](ProviderPort::CacheResult result) mutable {
                           if (const auto *name = std::get_if<std::string>(&result)) {
                               entry.name = *name;
                               request.providerCache = *name;
                               m_caches[request.cacheKey] = std::move(entry);
                           } else if (const ErrorKind kind = std::get<Error>(result).kind;
                                      kind == ErrorKind::Rejected || kind == ErrorKind::Unauthorized) {
                               m_caches[request.cacheKey] = std::move(entry);
                           }
                           next(std::move(request));
                       });
}

void Providers::forgetCache(const std::string &cacheKey, const std::string &name)
{
    const auto it = m_caches.find(cacheKey);
    if (it == m_caches.end() || it->second.name != name)
        return;
    const Cache cache = std::move(it->second);
    m_caches.erase(it);
    const Provider *provider = m_config.provider(cache.providerId);
    if (cache.name.empty() || !provider)
        return;
    withKey(*provider, [this, provider = *provider, name = cache.name](std::optional<std::string> key, std::optional<KeyError>) {
        if (key)
            m_port.deleteCache(provider, *key, name);
    });
}

void Providers::endConversation(const std::string &cacheKey)
{
    if (const auto it = m_caches.find(cacheKey); it != m_caches.end())
        forgetCache(cacheKey, it->second.name);
}

void Providers::cancel(StreamId id)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const std::shared_ptr<Pending> pending = it->second;
    if (pending->portId) {
        m_port.cancel(*pending->portId);
        return;
    }
    pending->cancelled = true;
    m_pending.erase(it);
    pending->sink(Failed{{.kind = ErrorKind::Cancelled}});
}

}
