#include "hikari/backends/ai_http_relay.h"

#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;

namespace {

constexpr qsizetype kChunkBytes = 1 << 20;
constexpr std::array kMethods = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
constexpr std::array kDroppedRequestHeaders = {
    "host", "content-length", "connection", "transfer-encoding", "accept-encoding", "keep-alive", "upgrade", "te",
    "trailer", "proxy-authorization", "proxy-connection", "cookie",
};
constexpr std::array kDroppedResponseHeaders = {
    "content-encoding", "content-length", "transfer-encoding", "connection", "keep-alive", "set-cookie",
};

const QByteArray kMask = QByteArrayLiteral("[key]");

template <std::size_t N>
bool listed(const std::array<const char *, N> &names, const QByteArray &name)
{
    return std::ranges::any_of(names, [&](const char *n) { return name == n; });
}

bool validToken(const QByteArray &name)
{
    static const QByteArray kSeparators = QByteArrayLiteral("()<>@,;:\\\"/[]?={} \t");
    return !name.isEmpty() && std::ranges::all_of(name, [](char c) {
        return c > 0x20 && c < 0x7f && !kSeparators.contains(c);
    });
}

bool validValue(const QByteArray &value)
{
    return !value.contains('\r') && !value.contains('\n') && !value.contains('\0');
}

QHostAddress unmapped(const QHostAddress &address)
{
    bool ok = false;
    const quint32 v4 = address.toIPv4Address(&ok);
    return ok ? QHostAddress(v4) : address;
}

int defaultPort(const QUrl &url)
{
    return url.scheme() == QLatin1String("https") ? 443 : 80;
}

/// Masks every whole key in a stream of chunks; holds back only a tail that could still begin one.
class Redactor {
public:
    explicit Redactor(QByteArray key = {}) : m_key(std::move(key)) {}

    QByteArray push(const QByteArray &chunk)
    {
        if (m_key.isEmpty())
            return chunk;
        QByteArray all = m_pending + chunk;
        all.replace(m_key, kMask);
        qsizetype hold = 0;
        for (qsizetype n = std::min(m_key.size() - 1, all.size()); n > 0 && hold == 0; --n)
            if (all.endsWith(m_key.first(n)))
                hold = n;
        m_pending = all.last(hold);
        all.chop(hold);
        return all;
    }

    QByteArray flush() { return std::exchange(m_pending, {}); }

private:
    QByteArray m_key;
    QByteArray m_pending;
};

class SystemProxies final : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override { return systemProxyForQuery(query); }
};

api::Json errorData(const app::Error &error)
{
    return api::Json::object({{"kind", std::string(app::errorKindId(error.kind))},
                              {"httpStatus", error.httpStatus},
                              {"detail", error.detail}});
}

}

struct HttpRelay::Exchange {
    std::uint64_t id = 0;
    api::Json stream;
    api::Json target;
    QByteArray method;
    QUrl url;
    std::vector<std::pair<QByteArray, QByteArray>> headers;
    QByteArray body;
    RelayExchange out;
    RelayTarget destination;
    QPointer<QNetworkReply> reply;
    Redactor redactor;
    bool started = false;
    bool answered = false;

    void fail(app::ErrorKind kind, std::string detail = {})
    {
        const app::Error error{kind, 0, std::move(detail)};
        answer([&] {
            out.fail(rpc::kProviderFailed, error.detail.empty() ? std::string(app::errorKindId(kind)) : error.detail,
                     errorData(error));
        });
    }

    void answer(const std::function<void()> &send)
    {
        if (answered)
            return;
        answered = true;
        send();
    }

    QByteArray masked(QByteArray text) const
    {
        if (!destination.key.empty())
            text.replace(QByteArray::fromStdString(destination.key), kMask);
        return text;
    }
};

RelayTargets providerTargets(app::Providers &providers)
{
    return [&providers](const api::Json &target, std::function<void(RelayTargetResult)> done) {
        const api::Json *kind = target.find("kind");
        const api::Json *id = target.find("id");
        if (!kind || *kind != api::Json("provider") || !id || !id->isString())
            return done(std::unexpected(app::Error{app::ErrorKind::Rejected, 0, "unknown target"}));
        const app::Provider *provider = providers.config().provider(id->asString());
        if (!provider)
            return done(std::unexpected(app::Error{app::ErrorKind::NoProvider, 0, ""}));
        providers.readKey(provider->id, [base = QUrl(QString::fromStdString(provider->baseUrl)),
                                         done = std::move(done)](std::optional<std::string> key) {
            if (!key)
                return done(std::unexpected(app::Error{app::ErrorKind::Key, 0, ""}));
            done(RelayTarget{base, std::move(*key)});
        });
    };
}

QUrl searchServiceBase(app::SearchService service)
{
    switch (service) {
    case app::SearchService::Brave:
        return QUrl(QStringLiteral("https://api.search.brave.com"));
    case app::SearchService::Tavily:
        return QUrl(QStringLiteral("https://api.tavily.com"));
    case app::SearchService::Exa:
        return QUrl(QStringLiteral("https://api.exa.ai"));
    case app::SearchService::Firecrawl:
        return QUrl(QStringLiteral("https://api.firecrawl.dev"));
    }
    return {};
}

RelayTargets withSearchTargets(RelayTargets next, app::SearchKeys &keys,
                               std::function<std::optional<SearchAccess>(app::SearchService)> access)
{
    return [next = std::move(next), &keys, access = std::move(access)](const api::Json &target,
                                                                        std::function<void(RelayTargetResult)> done) {
        const api::Json *kind = target.find("kind");
        if (!kind || *kind != api::Json("search"))
            return next(target, std::move(done));
        const api::Json *id = target.find("id");
        const std::optional<app::SearchService> service =
            id && id->isString() ? app::searchServiceFromId(id->asString()) : std::nullopt;
        const std::optional<SearchAccess> allowed = service ? access(*service) : std::nullopt;
        if (!allowed)
            return done(std::unexpected(app::Error{app::ErrorKind::Rejected, 0, "search service not chosen"}));
        if (allowed->typedKey)
            return done(RelayTarget{searchServiceBase(*service), *allowed->typedKey});
        keys.read(*service, [base = searchServiceBase(*service), done = std::move(done)](std::optional<std::string> key) {
            if (!key)
                return done(std::unexpected(app::Error{app::ErrorKind::Key, 0, ""}));
            done(RelayTarget{base, std::move(*key)});
        });
    };
}

bool keySafeOverPlainHttp(const QHostAddress &address, const QList<QHostAddress> &local)
{
    const QHostAddress a = unmapped(address);
    if (a.isLoopback())
        return true;
    static const std::array kTailnets = {QHostAddress::parseSubnet(QStringLiteral("100.64.0.0/10")),
                                         QHostAddress::parseSubnet(QStringLiteral("fd7a:115c:a1e0::/48"))};
    return std::ranges::any_of(kTailnets, [&](const auto &range) {
        return a.isInSubnet(range) &&
               std::ranges::any_of(local, [&](const QHostAddress &l) { return unmapped(l).isInSubnet(range); });
    });
}

bool withinBase(const QUrl &url, const QUrl &base)
{
    if (!url.isValid() || !base.isValid() || url.isRelative() || !url.userInfo().isEmpty())
        return false;
    if (url.scheme() != base.scheme() || url.host().compare(base.host(), Qt::CaseInsensitive) != 0 ||
        url.port(defaultPort(url)) != base.port(defaultPort(base)))
        return false;
    const QString path = url.path(QUrl::FullyDecoded);
    if (path.contains(QLatin1String("/../")) || path.endsWith(QLatin1String("/..")) ||
        path.contains(QLatin1String("/./")) || path.endsWith(QLatin1String("/.")))
        return false;
    QString prefix = base.path(QUrl::FullyEncoded);
    while (prefix.endsWith(QLatin1Char('/')))
        prefix.chop(1);
    const QString encoded = url.path(QUrl::FullyEncoded);
    return prefix.isEmpty() || encoded == prefix || encoded.startsWith(prefix + QLatin1Char('/'));
}

HttpRelay::Network HttpRelay::systemNetwork()
{
    return {
        [](const QString &host, std::function<void(QList<QHostAddress>)> done) {
            QHostInfo::lookupHost(host, QCoreApplication::instance(),
                                  [done = std::move(done)](const QHostInfo &info) { done(info.addresses()); });
        },
        [] { return QNetworkInterface::allAddresses(); },
    };
}

HttpRelay::HttpRelay(RelayTargets targets, Network network, QObject *parent)
    : QObject(parent), m_targets(std::move(targets)), m_network(std::move(network))
{
    m_nam.setProxyFactory(new SystemProxies);
    m_nam.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    m_nam.setCookieJar(nullptr);
}

HttpRelay::~HttpRelay()
{
    const auto exchanges = std::exchange(m_exchanges, {});
    for (const auto &[id, exchange] : exchanges) {
        if (exchange->reply) {
            exchange->reply->disconnect();
            exchange->reply->abort();
        }
        exchange->fail(app::ErrorKind::Network, "the relay stopped");
    }
}

void HttpRelay::attach(AiSidecarHost &host)
{
    host.handle(kHttpRequest, [relay = QPointer(this)](const api::Json &params,
                                                       std::shared_ptr<SidecarRequest> request) {
        if (!relay)
            return request->fail(rpc::kBusy, "the network relay is gone");
        auto cancel = relay->request(
            params, {[request](const std::string &method, api::Json p) { request->notify(method, std::move(p)); },
                     [request](api::Json result) { request->respond(std::move(result)); },
                     [request](int code, std::string message, api::Json data) {
                         request->fail(code, std::move(message), std::move(data));
                     }});
        request->onCancel(std::move(cancel));
    });
}

std::function<void()> HttpRelay::request(const api::Json &params, RelayExchange out)
{
    const api::Json *stream = params.find("stream");
    const api::Json *target = params.find("target");
    const api::Json *method = params.find("method");
    const api::Json *url = params.find("url");
    const api::Json *headers = params.find("headers");
    const api::Json *body = params.find("body");
    const auto invalid = [&](std::string why) {
        out.fail(rpc::kInvalidParams, "_hikari/http.request " + why, {});
        return [] {};
    };
    if (!stream || !target || !target->isObject() || !method || !method->isString() || !url || !url->isString())
        return invalid("needs stream, target, method and url");
    auto exchange = std::make_shared<Exchange>();
    exchange->id = m_next++;
    exchange->stream = *stream;
    exchange->target = *target;
    exchange->method = QByteArray::fromStdString(method->asString());
    if (!std::ranges::any_of(kMethods, [&](const char *m) { return exchange->method == m; }))
        return invalid("has an unsupported method");
    exchange->url = QUrl(QString::fromStdString(url->asString()), QUrl::StrictMode);
    if (headers) {
        if (!headers->isArray())
            return invalid("headers must be [name, value] pairs");
        for (const api::Json &pair : headers->asArray()) {
            if (!pair.isArray() || pair.asArray().size() != 2 || !pair.asArray()[0].isString() ||
                !pair.asArray()[1].isString())
                return invalid("headers must be [name, value] pairs");
            const QByteArray name = QByteArray::fromStdString(pair.asArray()[0].asString()).toLower();
            const QByteArray value = QByteArray::fromStdString(pair.asArray()[1].asString());
            if (!validToken(name) || !validValue(value))
                return invalid("has an invalid header");
            if (!listed(kDroppedRequestHeaders, name))
                exchange->headers.emplace_back(name, value);
        }
    }
    if (body && !body->isNull()) {
        if (!body->isString())
            return invalid("body must be base64");
        auto decoded = QByteArray::fromBase64Encoding(QByteArray::fromStdString(body->asString()),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return invalid("body must be base64");
        exchange->body = std::move(*decoded);
    }
    exchange->out = std::move(out);
    m_exchanges.emplace(exchange->id, exchange);

    m_targets(exchange->target, [relay = QPointer(this), weak = std::weak_ptr(exchange)](RelayTargetResult found) {
        const auto exchange = weak.lock();
        if (!relay || !exchange || exchange->answered)
            return;
        if (!found) {
            exchange->answer([&] {
                exchange->out.fail(rpc::kProviderFailed,
                                   found.error().detail.empty() ? std::string(app::errorKindId(found.error().kind))
                                                                : found.error().detail,
                                   errorData(found.error()));
            });
            return relay->finish(exchange->id);
        }
        exchange->destination = std::move(*found);
        exchange->redactor = Redactor(QByteArray::fromStdString(exchange->destination.key));
        relay->route(exchange);
    });

    return [relay = QPointer(this), weak = std::weak_ptr(exchange)] {
        const auto exchange = weak.lock();
        if (!exchange)
            return;
        if (exchange->reply) {
            exchange->reply->disconnect();
            exchange->reply->abort();
            exchange->reply->deleteLater();
        }
        exchange->answer([&] { exchange->out.fail(rpc::kRequestCancelled, "cancelled", {}); });
        if (relay)
            relay->finish(exchange->id);
    };
}

void HttpRelay::route(const std::shared_ptr<Exchange> &exchange)
{
    const QUrl &base = exchange->destination.base;
    QUrl url = exchange->url.adjusted(QUrl::NormalizePathSegments);
    const bool web = url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http");
    if (!web || !withinBase(url, base)) {
        exchange->fail(app::ErrorKind::Rejected, "invalid endpoint");
        return finish(exchange->id);
    }
    const QString host = url.host();
    if (exchange->destination.key.empty() || url.scheme() == QLatin1String("https") ||
        host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return send(exchange, url, {});
    if (const QHostAddress literal(host); !literal.isNull()) {
        if (!keySafeOverPlainHttp(literal, m_network.localAddresses())) {
            exchange->fail(app::ErrorKind::InsecureKey);
            return finish(exchange->id);
        }
        return send(exchange, url, {});
    }
    m_network.resolve(host, [relay = QPointer(this), weak = std::weak_ptr(exchange), url](QList<QHostAddress> found) {
        const auto exchange = weak.lock();
        if (!relay || !exchange || exchange->answered)
            return;
        if (found.isEmpty()) {
            exchange->fail(app::ErrorKind::Network, "host not found");
            return relay->finish(exchange->id);
        }
        const QList<QHostAddress> local = relay->m_network.localAddresses();
        const auto safe = std::ranges::find_if(found, [&](const QHostAddress &a) { return keySafeOverPlainHttp(a, local); });
        if (safe == found.end()) {
            exchange->fail(app::ErrorKind::InsecureKey);
            return relay->finish(exchange->id);
        }
        QUrl pinned = url;
        pinned.setHost(unmapped(*safe).toString());
        const QString port = url.port() < 0 ? QString() : QStringLiteral(":%1").arg(url.port());
        relay->send(exchange, pinned, url.host(QUrl::FullyEncoded) + port);
    });
}

void HttpRelay::send(const std::shared_ptr<Exchange> &exchange, QUrl url, const QString &host)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    const QByteArray key = QByteArray::fromStdString(exchange->destination.key);
    for (auto [name, value] : exchange->headers) {
        if (value.contains(kKeyPlaceholder)) {
            if (key.isEmpty())
                continue;
            value.replace(kKeyPlaceholder, key);
        }
        request.setRawHeader(name, value);
    }
    if (!host.isEmpty())
        request.setRawHeader("host", host.toUtf8());

    QNetworkReply *reply = m_nam.sendCustomRequest(request, exchange->method, exchange->body);
    exchange->reply = reply;
    const std::uint64_t id = exchange->id;
    const auto start = [exchange] {
        const QVariant status = exchange->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (exchange->started || !status.isValid())
            return;
        exchange->started = true;
        api::Json headers = api::Json::array();
        for (const auto &[name, value] : exchange->reply->rawHeaderPairs()) {
            const QByteArray lower = name.toLower();
            if (!listed(kDroppedResponseHeaders, lower))
                headers.push(api::Json::array({lower.toStdString(), exchange->masked(value).toStdString()}));
        }
        exchange->out.notify(kHttpResponse, api::Json::object({{"stream", exchange->stream},
                                                               {"status", status.toInt()},
                                                               {"headers", std::move(headers)}}));
    };
    const auto data = [exchange](const QByteArray &bytes) {
        for (qsizetype at = 0; at < bytes.size(); at += kChunkBytes)
            exchange->out.notify(kHttpData,
                                 api::Json::object({{"stream", exchange->stream},
                                                    {"data", bytes.mid(at, kChunkBytes).toBase64().toStdString()}}));
    };
    connect(reply, &QNetworkReply::metaDataChanged, this, start);
    connect(reply, &QNetworkReply::readyRead, this, [exchange, start, data] {
        start();
        if (exchange->started)
            data(exchange->redactor.push(exchange->reply->readAll()));
    });
    connect(reply, &QNetworkReply::finished, this, [this, exchange, start, data, id] {
        start();
        if (exchange->started) {
            data(exchange->redactor.push(exchange->reply->readAll()));
            data(exchange->redactor.flush());
            exchange->answer([&] { exchange->out.respond(api::Json::object()); });
        } else {
            const bool timeout = exchange->reply->error() == QNetworkReply::TimeoutError;
            exchange->fail(timeout ? app::ErrorKind::Timeout : app::ErrorKind::Network,
                           exchange->masked(exchange->reply->errorString().toUtf8()).toStdString());
        }
        exchange->reply->deleteLater();
        finish(id);
    });
}

void HttpRelay::finish(std::uint64_t id)
{
    m_exchanges.erase(id);
}

}
