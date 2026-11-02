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

enum class Wire { ChatCompletions, Responses, AnthropicMessages };
enum class Preset { OpenRouter, OpenAI, Anthropic, Custom };
enum class Role { Chat, Translator, Reviewer, Vision, Judge };

inline constexpr Role kRoles[] = {Role::Chat, Role::Translator, Role::Reviewer, Role::Vision, Role::Judge};

std::string_view wireId(Wire wire);
std::optional<Wire> wireFromId(std::string_view id);
std::string_view presetId(Preset preset);
std::optional<Preset> presetFromId(std::string_view id);
std::string_view roleId(Role role);
std::optional<Role> roleFromId(std::string_view id);

struct Capabilities {
    bool tools = false;
    bool vision = false;
    bool operator==(const Capabilities &) const = default;
};

/// US dollars per million tokens.
struct Price {
    double input = 0;
    double output = 0;
    std::optional<double> cachedInput;
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
    Capabilities defaults;
    std::vector<Model> models;
    /// The user's corrections to the catalogue, by model id.
    std::map<std::string, Capabilities, std::less<>> capabilityOverrides;
    bool operator==(const Provider &) const = default;
};

/// A provider entry for a preset, with its endpoint, wire and default capabilities.
Provider presetProvider(Preset preset, std::string id, std::string name);
Capabilities capabilitiesOf(const Provider &provider, std::string_view modelId);

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
    /// Part of inputTokens.
    std::int64_t cachedInputTokens = 0;
    /// What the provider says the request cost, when it says.
    std::optional<double> reportedCost;
    bool operator==(const Usage &) const = default;
};

/// The provider's figure when it reports one, else the model's price applied to the usage.
std::optional<double> costOf(const Usage &usage, const std::optional<Price> &price);

struct Text {
    std::string text;
};
struct Image {
    std::string mediaType;
    std::string base64;
};
struct ToolCall {
    std::string id;
    std::string name;
    std::string argumentsJson;
    bool operator==(const ToolCall &) const = default;
};
struct ToolResult {
    std::string callId;
    std::string content;
    bool isError = false;
};
using Part = std::variant<Text, Image, ToolCall, ToolResult>;

enum class Author { User, Assistant };

/// A user message holds Text, Image and ToolResult parts; an assistant message Text and ToolCall parts.
struct Message {
    Author author = Author::User;
    std::vector<Part> parts;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    /// A JSON Schema object.
    std::string parametersJson;
};

struct Request {
    std::string model;
    std::string system;
    std::vector<Message> messages;
    std::vector<ToolDefinition> tools;
    std::optional<int> maxOutputTokens;
};

enum class StopReason { End, ToolUse, MaxTokens, Refused };

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
struct Finished {
    StopReason reason = StopReason::End;
    std::string text;
    std::vector<ToolCall> toolCalls;
    Usage usage;
};
struct Failed {
    Error error;
};
using Event = std::variant<TextDelta, ToolCallStarted, ToolCallArguments, Finished, Failed>;

/// Gets every event of one request in order; Finished or Failed comes last, once.
using EventSink = std::function<void(const Event &)>;

using StreamId = std::uint64_t;

/// Talks to a provider over the network. Runs callbacks on the thread that owns it.
class ProviderPort {
public:
    virtual ~ProviderPort() = default;
    /// `key` is empty for a provider that needs none.
    virtual StreamId stream(const Provider &provider, const std::string &key, const Request &request,
                            EventSink sink) = 0;
    /// Ends the stream with Failed{Cancelled}; nothing for a finished one.
    virtual void cancel(StreamId id) = 0;
    using ModelsResult = std::variant<std::vector<Model>, Error>;
    virtual void listModels(const Provider &provider, const std::string &key,
                            std::function<void(ModelsResult)> done) = 0;
};

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

/// The providers the user added, their keys and their role models. Requests go through it.
class Providers {
public:
    Providers(ConfigStore &configs, KeyStore &keys, ProviderPort &port);

    const Config &config() const { return m_config; }
    bool enabled() const { return m_config.enabled(); }

    /// Stores the key first; the provider is added only once the key is safe. An empty key is for
    /// providers that need none.
    void add(Provider provider, std::string key, std::function<void(std::optional<SaveError>)> done);
    void remove(const std::string &providerId, std::function<void(std::optional<SaveError>)> done);
    bool assign(Role role, ModelRef model);
    bool setCapabilities(const std::string &providerId, const std::string &modelId, Capabilities capabilities);
    void refreshModels(const std::string &providerId, std::function<void(std::optional<Error>)> done);

    /// Runs `request` on the role's model. Without a provider, fails without touching the network.
    StreamId stream(Role role, Request request, EventSink sink);
    void cancel(StreamId id);

private:
    struct Pending;
    void withKey(const Provider &provider, std::function<void(std::optional<std::string>, std::optional<KeyError>)> next);

    ConfigStore &m_configs;
    KeyStore &m_keys;
    ProviderPort &m_port;
    Config m_config;
    std::map<std::string, std::string, std::less<>> m_keyCache;
    std::map<StreamId, std::shared_ptr<Pending>> m_pending;
    StreamId m_nextId = 1;
};

}
