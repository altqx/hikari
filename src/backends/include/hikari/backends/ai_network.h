#pragma once

#include "hikari/application/ai_providers.h"

#include <QHostAddress>
#include <QList>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QObject>
#include <QString>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <variant>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;

struct HttpCall;

/// Loopback, or Tailscale's 100.64.0.0/10 or fd7a:115c:a1e0::/48 when this machine has an address in the same
/// range too (the IPv4 range is also ISPs' shared CGNAT space).
bool keySafeOverPlainHttp(const QHostAddress &address, const QList<QHostAddress> &localAddresses);

/// Streams requests over Qt Network. A key travels only over HTTPS, or plain HTTP to this machine or a Tailscale
/// peer. A host name, MagicDNS ones included, is resolved once and the request goes to the address that was checked.
class NetworkProviderPort final : public QObject, public app::ProviderPort {
public:
    struct Route {
        QUrl url;
        QByteArray hostHeader;
    };
    using Resolver = std::function<void(const QString &host, std::function<void(QList<QHostAddress>)> done)>;

    /// The first resolved address a key may reach over plain HTTP, with the original authority kept for `Host`.
    static std::optional<Route> pinnedRoute(const QUrl &url, const QList<QHostAddress> &resolved,
                                            const QList<QHostAddress> &localAddresses);

    explicit NetworkProviderPort(QObject *parent = nullptr);
    ~NetworkProviderPort() override;

    void setResolver(Resolver resolve) { m_resolve = std::move(resolve); }
    void setLocalAddresses(std::function<QList<QHostAddress>()> addresses) { m_localAddresses = std::move(addresses); }

    /// A stream or a model list that receives nothing for this long fails with Timeout.
    void setIdleTimeout(std::chrono::milliseconds timeout) { m_idleTimeout = timeout; }

    app::StreamId stream(const app::Provider &provider, const std::string &key, const app::Request &request,
                         app::EventSink sink) override;
    void cancel(app::StreamId id) override;
    void listModels(const app::Provider &provider, const std::string &key,
                    std::function<void(ModelsResult)> done) override;

private:
    struct Run;
    using RouteResult = std::variant<Route, app::Error>;
    void route(const QUrl &url, bool hasKey, std::function<void(RouteResult)> done);
    void send(app::StreamId id, const Route &route, const HttpCall &call);
    void fetchModels(const Route &route, const HttpCall &call, app::Wire wire, std::function<void(ModelsResult)> done);
    void end(app::StreamId id, const std::vector<app::Event> &events);

    QNetworkAccessManager m_network;
    std::chrono::milliseconds m_idleTimeout{std::chrono::minutes(2)};
    Resolver m_resolve;
    std::function<QList<QHostAddress>()> m_localAddresses;
    std::map<app::StreamId, std::unique_ptr<Run>> m_runs;
    app::StreamId m_nextId = 1;
};

/// Keeps the user's providers and role models in a JSON file. Keys are never part of it.
class JsonConfigStore final : public app::ConfigStore {
public:
    explicit JsonConfigStore(QString path) : m_path(std::move(path)) {}
    app::Config load() override;
    bool save(const app::Config &config) override;

private:
    QString m_path;
};

}
