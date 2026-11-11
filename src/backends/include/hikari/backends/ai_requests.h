#pragma once

#include "hikari/api/json.h"
#include "hikari/application/ai_providers.h"
#include "hikari/backends/ai_http_relay.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace hikari::backends::ai {

class AiSidecarHost;
struct SidecarFailure;

/// Runs AI requests in the sidecar. The sidecar never gets a key: its provider calls go out through the HttpRelay,
/// which adds one. The sidecar is asked for only once a request has a provider and a model.
class AiRequests {
public:
    /// Null when AI is off or no sidecar is installed.
    using HostFor = std::function<AiSidecarHost *()>;

    AiRequests(application::ai::Providers &providers, HostFor host);
    ~AiRequests();
    AiRequests(const AiRequests &) = delete;
    AiRequests &operator=(const AiRequests &) = delete;

    /// `request` is the protocol's Request without its model; the role's model fills it in.
    application::ai::StreamId complete(application::ai::Role role, api::Json request, application::ai::EventSink sink);
    /// The sink still gets Failed{Cancelled}, once.
    void cancel(application::ai::StreamId id);
    /// Replaces the provider's catalogue with the provider's own list. A provider whose key is missing fails with Key
    /// before the sidecar is asked.
    void refreshModels(const std::string &providerId, std::function<void(std::optional<application::ai::Error>)> done);
    using Probed = std::variant<std::vector<application::ai::Model>, application::ai::Error>;
    /// Lists the catalogue of a provider that isn't stored yet, or is being edited, with the key the user typed.
    /// Nothing is saved, and the key stays in Qt: the relay finds it through relayTargets() while the probe runs.
    void probe(const application::ai::Provider &provider, const std::string &key, std::function<void(Probed)> done);
    /// Off sends every request with `caching: false`.
    void setCaching(bool on) { m_caching = on; }
    bool caching() const { return m_caching; }
    /// The sidecar, told the current providers first; null when AI is off or no sidecar is installed.
    AiSidecarHost *connected() { return connect(); }
    /// Off strips `webSearch` from every request and tells the sidecar no search service. The service searches for
    /// providers without their own search.
    void setWebSearch(bool on, std::optional<application::ai::SearchService> service);
    /// The service the sidecar searches with: nullopt while web search is off.
    bool webSearch() const { return m_webSearch; }
    std::optional<application::ai::SearchService> searchService() const
    {
        return m_webSearch ? m_searchService : std::nullopt;
    }
    /// One search with `service`, for Test connection: with `typedKey`, which stays in Qt, else its keychain key. The
    /// relay reaches the service while the test runs, even when it isn't the chosen one yet.
    void testSearch(application::ai::SearchService service, std::optional<std::string> typedKey,
                    std::function<void(std::optional<application::ai::Error>)> done);
    /// The chosen search service, or one being tested with the key to use.
    std::optional<SearchAccess> searchAccess(application::ai::SearchService service) const;
    /// Deletes the explicit provider cache the sidecar keeps for the conversation (the request's `cacheKey`). Call
    /// when the thread ends.
    void endConversation(const std::string &providerId, const std::string &cacheKey);
    /// The stored providers, with the ones being probed in their place.
    RelayTargets relayTargets();

private:
    struct Pending;
    struct Draft {
        application::ai::Provider provider;
        std::string key;
        std::uint64_t generation = 0;
    };
    struct SearchTest {
        std::optional<std::string> typedKey;
        std::uint64_t generation = 0;
    };
    AiSidecarHost *connect(const application::ai::Config &config);
    AiSidecarHost *connect() { return connect(m_providers.config()); }
    void listModels(const std::string &providerId, std::function<void(std::optional<application::ai::Error>)> done);
    void receive(const std::string &method, const api::Json &params);
    void end(application::ai::StreamId id, const application::ai::Event &event);

    application::ai::Providers &m_providers;
    HostFor m_host;
    std::map<application::ai::StreamId, std::shared_ptr<Pending>> m_pending;
    application::ai::StreamId m_nextId = 1;
    bool m_caching = true;
    bool m_webSearch = false;
    std::optional<application::ai::SearchService> m_searchService;
    std::map<application::ai::SearchService, SearchTest> m_searchTests;
    std::map<std::string, Draft> m_drafts;
    std::uint64_t m_lastAttempt = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// The sidecar's answer to a failed request as the panel shows it.
application::ai::Error requestError(const SidecarFailure &failure);
/// The protocol's Usage object.
application::ai::Usage usageOf(const api::Json &usage);

}
