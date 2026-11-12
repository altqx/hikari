#pragma once

#include "hikari/api/json.h"
#include "hikari/application/ai_providers.h"
#include "hikari/backends/ai_http_relay.h"

#include <cstdint>
#include <expected>
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

    /// The key a trusted Project folder names for one of the user's providers (ADR 0030, Project AI keys).
    using ProjectKeys = std::function<std::optional<std::string>(const std::string &project, const std::string &providerId)>;
    void setProjectKeys(ProjectKeys keys) { m_projectKeys = std::move(keys); }

    /// `request` is the protocol's Request without its model; the role's model fills it in. With `project`, a
    /// provider the Project has a key for reaches the sidecar as an alias whose relay target carries that key.
    application::ai::StreamId complete(application::ai::Role role, api::Json request, application::ai::EventSink sink,
                                       const std::string &project = {});
    struct TranslateSink {
        /// A `_hikari/translate.progress` notification's params.
        std::function<void(const api::Json &)> progress;
        /// The TranslateResult, or why the run failed.
        std::function<void(std::expected<api::Json, application::ai::Error>)> done;
    };
    /// One Honya pipeline run: `params` is the protocol's TranslateParams without its stream, models included.
    application::ai::StreamId requestTranslation(api::Json params, TranslateSink sink, const std::string &project = {});
    /// The sink still gets Failed{Cancelled}, once; a translation's done gets Cancelled.
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
    /// Deletes the explicit provider cache the sidecar keeps for the conversation (the request's `cacheKey`), under
    /// the provider id it was sent with for `project`. Call when the thread ends.
    void endConversation(const std::string &providerId, const std::string &cacheKey, const std::string &project = {});
    /// The provider id the sidecar is given: `providerId`, or its alias while `project` has a key for it. Call before
    /// connected(), which tells the sidecar the aliases.
    std::string wireProvider(const std::string &providerId, const std::string &project);
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
    struct Alias {
        std::string project;
        std::string providerId;
    };
    AiSidecarHost *connect(const application::ai::Config &config);
    AiSidecarHost *connect() { return connect(m_providers.config()); }
    void listModels(const std::string &providerId, std::function<void(std::optional<application::ai::Error>)> done);
    void receive(const std::string &method, const api::Json &params);
    void end(application::ai::StreamId id, const application::ai::Event &event);

    application::ai::Providers &m_providers;
    HostFor m_host;
    std::map<application::ai::StreamId, std::shared_ptr<Pending>> m_pending;
    struct Translation;
    std::map<application::ai::StreamId, std::shared_ptr<Translation>> m_translations;
    application::ai::StreamId m_nextId = 1;
    bool m_caching = true;
    bool m_webSearch = false;
    std::optional<application::ai::SearchService> m_searchService;
    std::map<application::ai::SearchService, SearchTest> m_searchTests;
    std::map<std::string, Draft> m_drafts;
    ProjectKeys m_projectKeys;
    std::vector<std::string> m_aliasProjects;
    std::map<std::string, Alias, std::less<>> m_aliases;
    std::uint64_t m_lastAttempt = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// The sidecar's answer to a failed request as the panel shows it.
application::ai::Error requestError(const SidecarFailure &failure);
/// The protocol's Usage object.
application::ai::Usage usageOf(const api::Json &usage);

}
