#pragma once

#include "hikari/api/json.h"
#include "hikari/application/ai_providers.h"

#include <QHostAddress>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace hikari::backends::ai {

class AiSidecarHost;

inline constexpr const char *kHttpRequest = "_hikari/http.request";
inline constexpr const char *kHttpResponse = "_hikari/http.response";
inline constexpr const char *kHttpData = "_hikari/http.data";
/// What the sidecar writes where the key goes; Qt puts the key there, or drops the header when there is none.
inline constexpr const char *kKeyPlaceholder = "hikari-key";

/// A destination the sidecar may reach, and the key Qt adds to its requests.
struct RelayTarget {
    QUrl base;
    /// Empty for a destination that takes no key.
    std::string key;
    /// Only GET and HEAD reach it.
    bool readOnly = false;
};

using RelayTargetResult = std::expected<RelayTarget, application::ai::Error>;
/// Looks up a request's `target`, such as {"kind": "provider", "id": "openai"}.
using RelayTargets =
    std::function<void(const api::Json &target, std::function<void(RelayTargetResult)> done)>;

/// The user's providers: {"kind": "provider", "id": …} reaches the provider's base URL with its keychain key.
/// `providers` must outlive the lookup.
RelayTargets providerTargets(application::ai::Providers &providers);

QUrl searchServiceBase(application::ai::SearchService service);
/// How the relay may reach a search service; a typed key, being tested, stands in for the keychain's.
struct SearchAccess {
    std::optional<std::string> typedKey;
};
/// Adds {"kind": "search", "id": …}: a reachable search service's base URL with its key. A service `access` answers
/// nullopt for is refused. Other targets go to `next`. `keys` must outlive the lookup.
RelayTargets withSearchTargets(RelayTargets next, application::ai::SearchKeys &keys,
                               std::function<std::optional<SearchAccess>(application::ai::SearchService)> access);

inline constexpr const char *kNpmRegistry = "https://registry.npmjs.org";
/// Adds {"kind": "npm"}: the npm registry, read-only and with no key, for the packages the sidecar fetches on first
/// use. Other targets go to `next`.
RelayTargets withRegistryTargets(RelayTargets next, QUrl registry = QUrl(QString::fromLatin1(kNpmRegistry)));

/// How the relay answers one request.
struct RelayExchange {
    std::function<void(const std::string &method, api::Json params)> notify;
    std::function<void(api::Json result)> respond;
    std::function<void(int code, std::string message, api::Json data)> fail;
};

/// Loopback, or a tailnet address while this machine has an address in the same tailnet range.
bool keySafeOverPlainHttp(const QHostAddress &address, const QList<QHostAddress> &local);
/// The base URL itself or a path under it, on the same origin.
bool withinBase(const QUrl &url, const QUrl &base);

/// Answers the sidecar's `_hikari/http.request`, its only way to the network. A request stays under its target's
/// base URL and never follows a redirect, and a key travels over plain HTTP only to this machine or a tailnet,
/// at the address that was checked. The response streams back as `_hikari/http.response` then `_hikari/http.data`
/// notifications, with the key masked, before the answer.
class HttpRelay final : public QObject {
    Q_OBJECT
public:
    struct Network {
        std::function<void(const QString &host, std::function<void(QList<QHostAddress>)> done)> resolve;
        std::function<QList<QHostAddress>()> localAddresses;
    };
    static Network systemNetwork();

    explicit HttpRelay(RelayTargets targets, Network network = systemNetwork(), QObject *parent = nullptr);
    ~HttpRelay() override;

    void attach(AiSidecarHost &host);
    /// Answers through `exchange` exactly once; the returned function cancels the request.
    std::function<void()> request(const api::Json &params, RelayExchange exchange);

private:
    struct Exchange;

    void route(const std::shared_ptr<Exchange> &exchange);
    void send(const std::shared_ptr<Exchange> &exchange, QUrl url, const QString &host);
    void finish(std::uint64_t id);

    RelayTargets m_targets;
    Network m_network;
    QNetworkAccessManager m_nam;
    std::map<std::uint64_t, std::shared_ptr<Exchange>> m_exchanges;
    std::uint64_t m_next = 1;
};

}
