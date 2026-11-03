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
    {Wire::Gemini, "gemini"},
};
constexpr std::pair<Preset, std::string_view> kPresets[] = {
    {Preset::OpenRouter, "openrouter"},
    {Preset::OpenAI, "openai"},
    {Preset::Anthropic, "anthropic"},
    {Preset::Gemini, "gemini"},
    {Preset::Custom, "custom"},
};
constexpr std::pair<StopReason, std::string_view> kStopReasons[] = {
    {StopReason::End, "end"}, {StopReason::ToolUse, "toolUse"}, {StopReason::MaxTokens, "maxTokens"},
    {StopReason::Refused, "refused"},
};
constexpr std::pair<ErrorKind, std::string_view> kErrorKinds[] = {
    {ErrorKind::NoProvider, "noProvider"}, {ErrorKind::NoModel, "noModel"},
    {ErrorKind::Key, "key"}, {ErrorKind::InsecureKey, "insecureKey"},
    {ErrorKind::Network, "network"}, {ErrorKind::Timeout, "timeout"},
    {ErrorKind::Unauthorized, "unauthorized"}, {ErrorKind::RateLimited, "rateLimited"},
    {ErrorKind::Rejected, "rejected"}, {ErrorKind::Server, "server"},
    {ErrorKind::Malformed, "malformed"}, {ErrorKind::Cancelled, "cancelled"},
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
std::optional<StopReason> stopReasonFromId(std::string_view id) { return fromId(kStopReasons, id); }
std::string_view errorKindId(ErrorKind kind) { return toId(kErrorKinds, kind); }
std::optional<ErrorKind> errorKindFromId(std::string_view id) { return fromId(kErrorKinds, id); }

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
    case Preset::Gemini:
        p.wire = Wire::Gemini;
        p.baseUrl = "https://generativelanguage.googleapis.com/v1beta";
        p.defaults = {.tools = true, .vision = true, .video = true};
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
    const auto tokens = [](std::int64_t n) { return static_cast<double>(n); };
    const double hourWrites = tokens(usage.cacheWriteHourTokens);
    const double minuteWrites = tokens(usage.cacheWriteTokens - usage.cacheWriteHourTokens);
    const double uncached = tokens(usage.inputTokens - usage.cachedInputTokens - usage.cacheWriteTokens);
    const double write = price->cacheWrite.value_or(price->input);
    return (uncached * price->input + tokens(usage.cachedInputTokens) * price->cachedInput.value_or(price->input) +
            minuteWrites * write + hourWrites * price->cacheWriteHour.value_or(write) +
            static_cast<double>(usage.outputTokens) * price->output) /
           1'000'000.0;
}

Providers::Providers(ConfigStore &configs, KeyStore &keys)
    : m_configs(configs), m_keys(keys), m_config(configs.load())
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

bool Providers::setModels(const std::string &providerId, std::vector<Model> models)
{
    Config next = m_config;
    const auto it = std::ranges::find(next.providers, providerId, &Provider::id);
    if (it == next.providers.end())
        return false;
    it->models = std::move(models);
    if (!m_configs.save(next))
        return false;
    m_config = std::move(next);
    return true;
}

void Providers::readKey(const std::string &providerId, std::function<void(std::optional<std::string>)> done)
{
    const Provider *provider = m_config.provider(providerId);
    if (!provider) {
        done(std::nullopt);
        return;
    }
    if (!provider->needsKey) {
        done(std::string());
        return;
    }
    if (const auto it = m_keyCache.find(providerId); it != m_keyCache.end()) {
        done(it->second);
        return;
    }
    m_keys.read(providerId, [this, providerId, done = std::move(done)](KeyResult result) {
        auto *key = std::get_if<std::string>(&result);
        if (!key) {
            done(std::nullopt);
            return;
        }
        m_keyCache[providerId] = *key;
        done(*key);
    });
}

}
