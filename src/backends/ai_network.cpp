#include "hikari/backends/ai_network.h"

#include "hikari/backends/ai_wire.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace hikari::backends::ai {

namespace {

QNetworkRequest networkRequest(const NetworkProviderPort::Route &route, const HttpCall &call)
{
    QNetworkRequest request(route.url);
    for (const auto &[name, value] : call.headers)
        request.setRawHeader(name, value);
    if (!route.hostHeader.isEmpty())
        request.setRawHeader("Host", route.hostHeader);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    return request;
}

int statusOf(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

app::Error replyError(QNetworkReply *reply, const QByteArray &body, bool timedOut)
{
    if (timedOut)
        return {.kind = app::ErrorKind::Timeout};
    const int status = statusOf(reply);
    if (status >= 300)
        return {.kind = errorKindOfStatus(status), .httpStatus = status, .detail = errorDetail(body).toStdString()};
    return {.kind = app::ErrorKind::Network, .httpStatus = 0, .detail = reply->errorString().toStdString()};
}

QJsonObject capabilitiesJson(const app::Capabilities &c)
{
    return {{QStringLiteral("tools"), c.tools}, {QStringLiteral("vision"), c.vision}};
}

app::Capabilities capabilitiesFrom(const QJsonObject &o)
{
    return {.tools = o.value(QStringLiteral("tools")).toBool(), .vision = o.value(QStringLiteral("vision")).toBool()};
}

}

struct NetworkProviderPort::Run {
    QPointer<QNetworkReply> reply;
    QTimer idle;
    SseParser parser;
    std::unique_ptr<StreamDecoder> decoder;
    app::EventSink sink;
    QByteArray errorBody;
    bool timedOut = false;
};

bool tailscaleName(const QString &host)
{
    QString name = host.toLower();
    if (name.endsWith(u'.'))
        name.chop(1);
    return name.endsWith(QLatin1String(".ts.net")) && name.size() > 7;
}

bool keySafeOverPlainHttp(const QHostAddress &address, const QList<QHostAddress> &localAddresses)
{
    bool v4 = false;
    const QHostAddress plain(address.toIPv4Address(&v4));
    const QHostAddress a = v4 ? plain : address;
    if (a.isLoopback())
        return true;
    if (a.isInSubnet(QHostAddress(QStringLiteral("fd7a:115c:a1e0::")), 48))
        return true;
    const std::pair<QHostAddress, int> cgnat{QHostAddress(QStringLiteral("100.64.0.0")), 10};
    if (!a.isInSubnet(cgnat))
        return false;
    return std::ranges::any_of(localAddresses, [&](const QHostAddress &local) {
        bool localV4 = false;
        const QHostAddress l(local.toIPv4Address(&localV4));
        return localV4 && l.isInSubnet(cgnat);
    });
}

NetworkProviderPort::NetworkProviderPort(QObject *parent) : QObject(parent)
{
    m_network.setAutoDeleteReplies(false);
    m_localAddresses = [] { return QNetworkInterface::allAddresses(); };
    m_resolve = [this](const QString &host, std::function<void(QList<QHostAddress>)> done) {
        QHostInfo::lookupHost(host, this, [done](const QHostInfo &info) { done(info.addresses()); });
    };
}

void NetworkProviderPort::route(const QUrl &url, bool hasKey, std::function<void(RouteResult)> done)
{
    const bool https = url.scheme() == QLatin1String("https");
    if (!url.isValid() || url.host().isEmpty() || (!https && url.scheme() != QLatin1String("http"))) {
        done(app::Error{.kind = app::ErrorKind::Rejected, .httpStatus = 0, .detail = "invalid endpoint"});
        return;
    }
    const QString host = url.host();
    if (!hasKey || https || host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 || tailscaleName(host)) {
        done(Route{url, {}});
        return;
    }
    if (const QHostAddress literal(host); !literal.isNull()) {
        if (keySafeOverPlainHttp(literal, m_localAddresses()))
            done(Route{url, {}});
        else
            done(app::Error{.kind = app::ErrorKind::InsecureKey});
        return;
    }
    m_resolve(host, [this, url, done](const QList<QHostAddress> &addresses) {
        if (addresses.isEmpty()) {
            done(app::Error{.kind = app::ErrorKind::Network, .httpStatus = 0, .detail = "host not found"});
            return;
        }
        const QList<QHostAddress> local = m_localAddresses();
        for (const QHostAddress &address : addresses) {
            if (!keySafeOverPlainHttp(address, local))
                continue;
            QUrl pinned = url;
            pinned.setHost(address.toString());
            const QByteArray authority =
                url.host(QUrl::FullyEncoded).toUtf8() + (url.port() < 0 ? QByteArray() : ":" + QByteArray::number(url.port()));
            done(Route{pinned, authority});
            return;
        }
        done(app::Error{.kind = app::ErrorKind::InsecureKey});
    });
}

NetworkProviderPort::~NetworkProviderPort()
{
    for (auto &[id, run] : m_runs)
        if (run->reply) {
            run->reply->disconnect(this);
            run->reply->abort();
            delete run->reply;
        }
}

void NetworkProviderPort::end(app::StreamId id, const std::vector<app::Event> &events)
{
    const auto it = m_runs.find(id);
    if (it == m_runs.end())
        return;
    std::unique_ptr<Run> run = std::move(it->second);
    m_runs.erase(it);
    run->idle.stop();
    if (run->reply) {
        run->reply->disconnect(this);
        if (run->reply->isRunning())
            run->reply->abort();
        run->reply->deleteLater();
    }
    for (const app::Event &event : events)
        run->sink(event);
}

app::StreamId NetworkProviderPort::stream(const app::Provider &provider, const std::string &key,
                                          const app::Request &request, app::EventSink sink)
{
    const app::StreamId id = m_nextId++;
    const HttpCall call = encodeRequest(provider, QByteArray::fromStdString(key), request);
    const QUrl url(QString::fromStdString(provider.baseUrl) + call.path);
    auto run = std::make_unique<Run>();
    run->decoder = makeDecoder(provider.wire);
    run->sink = std::move(sink);
    run->idle.setSingleShot(true);
    run->idle.setInterval(m_idleTimeout);
    m_runs.emplace(id, std::move(run));
    route(url, !key.empty(), [this, id, call](const RouteResult &result) {
        if (!m_runs.contains(id))
            return;
        if (const auto *error = std::get_if<app::Error>(&result))
            end(id, {app::Failed{*error}});
        else
            send(id, std::get<Route>(result), call);
    });
    return id;
}

void NetworkProviderPort::send(app::StreamId id, const Route &route, const HttpCall &call)
{
    Run *r = m_runs.at(id).get();
    QNetworkReply *reply = m_network.post(networkRequest(route, call), call.body);
    r->reply = reply;
    connect(&r->idle, &QTimer::timeout, this, [this, id, r] {
        r->timedOut = true;
        end(id, {app::Failed{replyError(r->reply, {}, true)}});
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, id, r, reply] {
        r->idle.start();
        const int status = statusOf(reply);
        if (status < 200 || status >= 300) {
            r->errorBody += reply->readAll();
            return;
        }
        for (const SseParser::Message &message : r->parser.feed(reply->readAll())) {
            const std::vector<app::Event> decoded = r->decoder->feed(message);
            if (r->decoder->done()) {
                end(id, decoded);
                return;
            }
            for (const app::Event &event : decoded) {
                r->sink(event);
                if (!m_runs.contains(id))
                    return;
            }
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, r, reply] {
        r->errorBody += reply->readAll();
        const int status = statusOf(reply);
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300)
            end(id, r->decoder->close());
        else
            end(id, {app::Failed{replyError(reply, r->errorBody, false)}});
    });
    r->idle.start();
}

void NetworkProviderPort::cancel(app::StreamId id)
{
    end(id, {app::Failed{{.kind = app::ErrorKind::Cancelled}}});
}

void NetworkProviderPort::listModels(const app::Provider &provider, const std::string &key,
                                     std::function<void(ModelsResult)> done)
{
    const HttpCall call = modelsCall(provider, QByteArray::fromStdString(key));
    const QUrl url(QString::fromStdString(provider.baseUrl) + call.path);
    route(url, !key.empty(), [this, call, done](const RouteResult &result) {
        if (const auto *error = std::get_if<app::Error>(&result))
            done(*error);
        else
            fetchModels(std::get<Route>(result), call, done);
    });
}

void NetworkProviderPort::fetchModels(const Route &route, const HttpCall &call, std::function<void(ModelsResult)> done)
{
    QNetworkReply *reply = m_network.get(networkRequest(route, call));
    auto *idle = new QTimer(reply);
    idle->setSingleShot(true);
    auto finished = std::make_shared<bool>(false);
    connect(idle, &QTimer::timeout, this, [reply, done, finished] {
        *finished = true;
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
        done(app::Error{.kind = app::ErrorKind::Timeout});
    });
    connect(reply, &QNetworkReply::downloadProgress, idle, [idle] { idle->start(); });
    connect(reply, &QNetworkReply::finished, this, [reply, done, finished] {
        if (*finished)
            return;
        *finished = true;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = statusOf(reply);
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            done(replyError(reply, body, false));
            return;
        }
        done(decodeModels(body));
    });
    idle->start(m_idleTimeout);
}

app::Config JsonConfigStore::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    app::Config config;
    for (const QJsonValue &v : root.value(QStringLiteral("providers")).toArray()) {
        const QJsonObject o = v.toObject();
        const auto preset = app::presetFromId(o.value(QStringLiteral("preset")).toString().toStdString());
        const auto wire = app::wireFromId(o.value(QStringLiteral("wire")).toString().toStdString());
        const std::string id = o.value(QStringLiteral("id")).toString().toStdString();
        if (!preset || !wire || id.empty())
            continue;
        app::Provider p;
        p.id = id;
        p.name = o.value(QStringLiteral("name")).toString().toStdString();
        p.preset = *preset;
        p.wire = *wire;
        p.baseUrl = o.value(QStringLiteral("baseUrl")).toString().toStdString();
        p.needsKey = o.value(QStringLiteral("needsKey")).toBool(true);
        p.defaults = capabilitiesFrom(o.value(QStringLiteral("defaults")).toObject());
        for (const QJsonValue &mv : o.value(QStringLiteral("models")).toArray()) {
            const QJsonObject m = mv.toObject();
            app::Model model;
            model.id = m.value(QStringLiteral("id")).toString().toStdString();
            model.name = m.value(QStringLiteral("name")).toString().toStdString();
            if (m.contains(QStringLiteral("capabilities")))
                model.capabilities = capabilitiesFrom(m.value(QStringLiteral("capabilities")).toObject());
            if (m.contains(QStringLiteral("price"))) {
                const QJsonObject price = m.value(QStringLiteral("price")).toObject();
                model.price = app::Price{.input = price.value(QStringLiteral("input")).toDouble(),
                                         .output = price.value(QStringLiteral("output")).toDouble()};
                if (price.contains(QStringLiteral("cachedInput")))
                    model.price->cachedInput = price.value(QStringLiteral("cachedInput")).toDouble();
                if (price.contains(QStringLiteral("cacheWrite")))
                    model.price->cacheWrite = price.value(QStringLiteral("cacheWrite")).toDouble();
                if (price.contains(QStringLiteral("cacheWriteHour")))
                    model.price->cacheWriteHour = price.value(QStringLiteral("cacheWriteHour")).toDouble();
            }
            if (m.contains(QStringLiteral("contextTokens")))
                model.contextTokens = m.value(QStringLiteral("contextTokens")).toInteger();
            p.models.push_back(std::move(model));
        }
        const QJsonObject overrides = o.value(QStringLiteral("capabilityOverrides")).toObject();
        for (auto it = overrides.begin(); it != overrides.end(); ++it)
            p.capabilityOverrides[it.key().toStdString()] = capabilitiesFrom(it.value().toObject());
        config.providers.push_back(std::move(p));
    }
    const QJsonObject roles = root.value(QStringLiteral("roles")).toObject();
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        const auto role = app::roleFromId(it.key().toStdString());
        const QJsonObject ref = it.value().toObject();
        if (role)
            config.roles[*role] = {ref.value(QStringLiteral("provider")).toString().toStdString(),
                                   ref.value(QStringLiteral("model")).toString().toStdString()};
    }
    return config;
}

bool JsonConfigStore::save(const app::Config &config)
{
    QJsonArray providers;
    for (const app::Provider &p : config.providers) {
        QJsonArray models;
        for (const app::Model &m : p.models) {
            QJsonObject model{{QStringLiteral("id"), QString::fromStdString(m.id)},
                              {QStringLiteral("name"), QString::fromStdString(m.name)}};
            if (m.capabilities)
                model.insert(QStringLiteral("capabilities"), capabilitiesJson(*m.capabilities));
            if (m.price) {
                QJsonObject price{{QStringLiteral("input"), m.price->input}, {QStringLiteral("output"), m.price->output}};
                if (m.price->cachedInput)
                    price.insert(QStringLiteral("cachedInput"), *m.price->cachedInput);
                if (m.price->cacheWrite)
                    price.insert(QStringLiteral("cacheWrite"), *m.price->cacheWrite);
                if (m.price->cacheWriteHour)
                    price.insert(QStringLiteral("cacheWriteHour"), *m.price->cacheWriteHour);
                model.insert(QStringLiteral("price"), price);
            }
            if (m.contextTokens)
                model.insert(QStringLiteral("contextTokens"), static_cast<qint64>(*m.contextTokens));
            models.append(model);
        }
        QJsonObject overrides;
        for (const auto &[model, capabilities] : p.capabilityOverrides)
            overrides.insert(QString::fromStdString(model), capabilitiesJson(capabilities));
        providers.append(QJsonObject{
            {QStringLiteral("id"), QString::fromStdString(p.id)},
            {QStringLiteral("name"), QString::fromStdString(p.name)},
            {QStringLiteral("preset"), QString::fromUtf8(app::presetId(p.preset))},
            {QStringLiteral("wire"), QString::fromUtf8(app::wireId(p.wire))},
            {QStringLiteral("baseUrl"), QString::fromStdString(p.baseUrl)},
            {QStringLiteral("needsKey"), p.needsKey},
            {QStringLiteral("defaults"), capabilitiesJson(p.defaults)},
            {QStringLiteral("models"), models},
            {QStringLiteral("capabilityOverrides"), overrides},
        });
    }
    QJsonObject roles;
    for (const auto &[role, ref] : config.roles)
        roles.insert(QString::fromUtf8(app::roleId(role)),
                     QJsonObject{{QStringLiteral("provider"), QString::fromStdString(ref.providerId)},
                                 {QStringLiteral("model"), QString::fromStdString(ref.modelId)}});
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("providers"), providers},
                           {QStringLiteral("roles"), roles}};
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return false;
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

}
