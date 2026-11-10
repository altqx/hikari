#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hikari::application::ai {

enum class Wire { ChatCompletions, Responses, AnthropicMessages, Gemini };
enum class Preset { OpenRouter, OpenAI, Anthropic, Gemini, Custom };
enum class Role { Chat, Translator, Reviewer, Vision, Judge };

inline constexpr Role kRoles[] = {Role::Chat, Role::Translator, Role::Reviewer, Role::Vision, Role::Judge};

std::string_view wireId(Wire wire);
std::optional<Wire> wireFromId(std::string_view id);
std::string_view presetId(Preset preset);
std::optional<Preset> presetFromId(std::string_view id);
std::string_view roleId(Role role);
std::optional<Role> roleFromId(std::string_view id);

/// Searches the web for providers without a search of their own.
enum class SearchService { Brave, Tavily, Exa, Firecrawl };
std::string_view searchServiceId(SearchService service);
std::optional<SearchService> searchServiceFromId(std::string_view id);
/// The keychain entry of the service's key; no provider id contains ':'.
std::string searchKeyEntry(SearchService service);

struct Capabilities {
    bool tools = false;
    bool vision = false;
    bool video = false;
    bool operator==(const Capabilities &) const = default;
};

/// US dollars per million tokens.
struct Price {
    double input = 0;
    double output = 0;
    std::optional<double> cachedInput;
    std::optional<double> cacheWrite;
    std::optional<double> cacheWriteHour;
    bool operator==(const Price &) const = default;
};

struct Model {
    std::string id;
    std::string name;
    /// Empty when the catalogue doesn't say; the provider's defaults apply.
    std::optional<Capabilities> capabilities;
    std::optional<Price> price;
    std::optional<std::int64_t> contextTokens;
    bool operator==(const Model &) const = default;
};

struct Provider {
    std::string id;
    std::string name;
    Preset preset = Preset::Custom;
    Wire wire = Wire::ChatCompletions;
    /// Ends before the endpoint path, e.g. "https://api.openai.com/v1".
    std::string baseUrl;
    bool needsKey = true;
    /// The key came from signing in to the provider, not from a paste.
    bool signedIn = false;
    Capabilities defaults;
    std::vector<Model> models;
    /// The user's corrections to the catalogue, by model id.
    std::map<std::string, Capabilities, std::less<>> capabilityOverrides;
    bool operator==(const Provider &) const = default;
};

/// A provider entry for a preset, with its endpoint, wire and default capabilities.
Provider presetProvider(Preset preset, std::string id, std::string name);
Capabilities capabilitiesOf(const Provider &provider, std::string_view modelId);
/// The sidecar's searchesNatively: the others search through the user's search service.
bool searchesNatively(const Provider &provider);

struct ModelRef {
    std::string providerId;
    std::string modelId;
    bool operator==(const ModelRef &) const = default;
};

struct Config {
    std::vector<Provider> providers;
    std::map<Role, ModelRef> roles;

    /// AI is off, and sends nothing, until a provider is added.
    bool enabled() const { return !providers.empty(); }
    const Provider *provider(std::string_view id) const;
    /// The role's model, else the chat model.
    std::optional<ModelRef> modelFor(Role role) const;
    bool operator==(const Config &) const = default;
};

struct Usage {
    std::int64_t inputTokens = 0;
    std::int64_t outputTokens = 0;
    /// Part of inputTokens: read from the provider's cache.
    std::int64_t cachedInputTokens = 0;
    /// Part of inputTokens: written to the provider's cache.
    std::int64_t cacheWriteTokens = 0;
    /// Part of cacheWriteTokens: kept for an hour.
    std::int64_t cacheWriteHourTokens = 0;
    /// What the provider says the request cost, when it says.
    std::optional<double> reportedCost;
    bool operator==(const Usage &) const = default;
};

/// The provider's figure when it reports one, else the model's price applied to the usage.
std::optional<double> costOf(const Usage &usage, const std::optional<Price> &price);

struct ToolCall {
    std::string id;
    std::string name;
    std::string argumentsJson;
    /// Opaque provider state that must go back with the call in the history (Gemini's thought signature).
    std::string signature;
    bool operator==(const ToolCall &) const = default;
};

enum class StopReason { End, ToolUse, MaxTokens, Refused };

std::optional<StopReason> stopReasonFromId(std::string_view id);

enum class ErrorKind {
    NoProvider,
    NoModel,
    Key,
    InsecureKey,
    Network,
    Timeout,
    Unauthorized,
    RateLimited,
    Rejected,
    Server,
    Malformed,
    Cancelled,
};

struct Error {
    ErrorKind kind = ErrorKind::Network;
    int httpStatus = 0;
    /// The provider's own message, shown to the user as is.
    std::string detail;
    bool operator==(const Error &) const = default;
};

std::string_view errorKindId(ErrorKind kind);
std::optional<ErrorKind> errorKindFromId(std::string_view id);

struct TextDelta {
    std::string text;
};
struct ToolCallStarted {
    int index = 0;
    std::string id;
    std::string name;
};
struct ToolCallArguments {
    int index = 0;
    std::string delta;
};
/// A source the answer cites; `index` numbers one completion's sources from 1.
struct Citation {
    int index = 0;
    std::string url;
    std::string title;
    std::string quote;
    bool operator==(const Citation &) const = default;
};
struct Finished {
    StopReason reason = StopReason::End;
    std::string text;
    std::vector<ToolCall> toolCalls;
    Usage usage;
    std::vector<Citation> citations;
    /// Google's search suggestions as HTML; its terms require showing them with a Gemini answer that searched.
    std::string searchSuggestions;
    /// The protocol's providerState part as JSON text, empty when there is none. It goes back among the assistant
    /// message's parts when the conversation continues.
    std::string providerState;
};
struct Failed {
    Error error;
};
using Event = std::variant<TextDelta, ToolCallStarted, ToolCallArguments, Finished, Failed>;

/// Gets every event of one request in order; Finished or Failed comes last, once.
using EventSink = std::function<void(const Event &)>;

using StreamId = std::uint64_t;

enum class KeyError { Unavailable, Denied, NotFound, Other };

using KeyResult = std::variant<std::string, KeyError>;

/// Keeps provider keys in the OS keychain, never anywhere else.
class KeyStore {
public:
    virtual ~KeyStore() = default;
    virtual void read(const std::string &providerId, std::function<void(KeyResult)> done) = 0;
    virtual void write(const std::string &providerId, const std::string &key,
                       std::function<void(std::optional<KeyError>)> done) = 0;
    virtual void remove(const std::string &providerId, std::function<void(std::optional<KeyError>)> done) = 0;
};

/// The user's providers and role models, saved without keys.
class ConfigStore {
public:
    virtual ~ConfigStore() = default;
    virtual Config load() = 0;
    virtual bool save(const Config &config) = 0;
};

enum class SaveError { Key, KeychainUnavailable, KeychainDenied, Config };

/// The providers the user added, their keys and their role models.
class Providers {
public:
    Providers(ConfigStore &configs, KeyStore &keys);

    const Config &config() const { return m_config; }
    bool enabled() const { return m_config.enabled(); }

    /// Stores the key first; the provider is added only once the key is safe. An empty key is for
    /// providers that need none.
    void add(Provider provider, std::string key, std::function<void(std::optional<SaveError>)> done);
    /// Changes an added provider in place. Nullopt keeps its stored key; its roles stay.
    void update(Provider provider, std::optional<std::string> key, std::function<void(std::optional<SaveError>)> done);
    void remove(const std::string &providerId, std::function<void(std::optional<SaveError>)> done);
    /// Deletes a signed-in provider's key from the keychain. The provider and its roles stay, without a key, until
    /// the user signs in again or pastes one.
    void signOut(const std::string &providerId, std::function<void(std::optional<SaveError>)> done);
    bool assign(Role role, ModelRef model);
    /// The role goes back to the chat model.
    bool unassign(Role role);
    bool setCapabilities(const std::string &providerId, const std::string &modelId, Capabilities capabilities);
    /// Replaces the provider's catalogue; the user's capability corrections stay.
    bool setModels(const std::string &providerId, std::vector<Model> models);

    /// The provider's key, read from the keychain once; empty for a provider that needs none. Nullopt when the
    /// provider is gone or its key can't be read.
    void readKey(const std::string &providerId, std::function<void(std::optional<std::string>)> done);

private:
    void store(Provider provider, std::optional<std::string> key, std::function<void(std::optional<SaveError>)> done);

    ConfigStore &m_configs;
    KeyStore &m_keys;
    Config m_config;
    std::map<std::string, std::string, std::less<>> m_keyCache;
};

/// The search services' keys, kept in the keychain like the providers'.
class SearchKeys {
public:
    explicit SearchKeys(KeyStore &keys) : m_keys(keys) {}

    void store(SearchService service, std::string key, std::function<void(std::optional<SaveError>)> done);
    void remove(SearchService service, std::function<void(std::optional<SaveError>)> done);
    /// Read from the keychain once; nullopt when there is none or it can't be read.
    void read(SearchService service, std::function<void(std::optional<std::string>)> done);

private:
    KeyStore &m_keys;
    std::map<SearchService, std::string> m_cache;
};

}
