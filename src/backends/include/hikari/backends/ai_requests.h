#pragma once

#include "hikari/api/json.h"
#include "hikari/application/ai_providers.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace hikari::backends::ai {

class AiSidecarHost;
struct SidecarFailure;

/// Runs AI requests in the sidecar. Each one reads its provider's key from the keychain and hands it over with the
/// request; the sidecar is asked for only once a request has a provider, a model and a key.
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
    /// Replaces the provider's catalogue with the provider's own list.
    void refreshModels(const std::string &providerId, std::function<void(std::optional<application::ai::Error>)> done);
    /// Deletes the explicit provider cache the sidecar keeps for the conversation (the request's `cacheKey`). Call
    /// when the thread ends.
    void endConversation(const std::string &providerId, const std::string &cacheKey);

private:
    struct Pending;
    AiSidecarHost *connect();
    void receive(const std::string &method, const api::Json &params);
    void end(application::ai::StreamId id, const application::ai::Event &event);

    application::ai::Providers &m_providers;
    HostFor m_host;
    std::map<application::ai::StreamId, std::shared_ptr<Pending>> m_pending;
    application::ai::StreamId m_nextId = 1;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// The sidecar's answer to a failed request as the panel shows it.
application::ai::Error requestError(const SidecarFailure &failure);

}
