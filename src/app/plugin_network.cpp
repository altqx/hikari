#include "hikari/app/plugin_network.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>
#include <array>
#include <map>

namespace hikari::app {

namespace {

using api::Json;
namespace errors = api::errors;

constexpr std::size_t kMaxHeaders = 64;
constexpr std::size_t kMaxHeaderBytes = 8192;
constexpr std::array kMethods{"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE"};
constexpr std::array kHopHeaders{"host", "cookie", "connection", "content-length", "transfer-encoding", "upgrade", "te",
                                 "trailer", "keep-alive", "expect"};

std::string inQuotes(std::string_view s)
{
    return "\xE2\x80\x9C" + std::string(s) + "\xE2\x80\x9D";
}

std::unexpected<api::Error> badParam(std::string_view pointer, std::string message)
{
    return api::fail(errors::kInvalidParams, std::move(message), Json::object({{"pointer", std::string(pointer)}}));
}

std::unexpected<api::Error> refused(std::string_view reason, std::string message)
{
    return api::fail(errors::kRefused, std::move(message), Json::object({{"reason", std::string(reason)}}));
}

bool tokenChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           std::string_view("!#$%&'*+-.^_`|~").contains(c);
}

std::string lowered(std::string s)
{
    std::ranges::transform(s, s.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    return s;
}

std::expected<std::vector<std::pair<QByteArray, QByteArray>>, api::Error> requestHeaders(const Json *headers)
{
    std::vector<std::pair<QByteArray, QByteArray>> out;
    if (!headers)
        return out;
    if (!headers->isObject() || headers->asObject().size() > kMaxHeaders)
        return badParam("/headers", "headers must be a table of at most 64 names and text values");
    for (const Json::Member &header : headers->asObject()) {
        const std::string at = "/headers/" + header.key;
        if (header.key.empty() || !std::ranges::all_of(header.key, tokenChar))
            return badParam(at, inQuotes(header.key) + " isn't a header name");
        const std::string name = lowered(header.key);
        if (std::ranges::find(kHopHeaders, name) != kHopHeaders.end() || name.starts_with("proxy-"))
            return badParam(at, "HikariSub sets " + inQuotes(header.key) + " itself");
        if (!header.value.isString() || header.value.asString().size() > kMaxHeaderBytes ||
            header.value.asString().find_first_of(std::string_view("\r\n\0", 3)) != std::string::npos)
            return badParam(at, "a header value is one line of text");
        out.emplace_back(QByteArray::fromStdString(header.key), QByteArray::fromStdString(header.value.asString()));
    }
    return out;
}

}

struct PluginNetwork::Open {
    std::string plugin;
    std::vector<std::string> hosts;
    QNetworkReply *reply = nullptr;
    api::Reply answer;
    bool base64 = false;
    QByteArray body;
    std::optional<api::Error> refusal;
};

PluginNetwork::PluginNetwork(QNetworkAccessManager &network, std::function<bool()> allowed, QObject *parent)
    : QObject(parent), m_network(network), m_allowed(std::move(allowed))
{
}

PluginNetwork::~PluginNetwork()
{
    for (const auto &open : m_open) {
        disconnect(open->reply, nullptr, this, nullptr);
        open->reply->abort();
        open->reply->deleteLater();
    }
}

std::optional<api::Error> PluginNetwork::refusal(const std::vector<std::string> &hosts, const QUrl &url)
{
    if (!url.isValid() || url.isRelative())
        return badParam("/url", "not a URL").error();
    if (url.scheme() != QStringLiteral("https"))
        return refused("not_https", "plugins reach the network over HTTPS only").error();
    if (!url.userInfo().isEmpty())
        return badParam("/url", "a URL can't carry a user name or password").error();
    if (url.port() != -1 && url.port() != 443)
        return refused("port", "plugins reach port 443 only").error();
    const std::string host = url.host(QUrl::FullyEncoded).toStdString();
    if (std::ranges::find(hosts, host) == hosts.end())
        return api::fail(errors::kNotGranted, inQuotes(host) + " isn't a host the plugin declared", Json::object({{"host", host}}))
            .error();
    return std::nullopt;
}

void PluginNetwork::fetch(const std::string &plugin, const std::vector<std::string> &hosts, const Json &params, api::Reply reply)
{
    if (!m_allowed || !m_allowed()) {
        reply(refused("network_off", "plugin network access is turned off in Settings"));
        return;
    }
    if (!params.isObject()) {
        reply(badParam("", "the request must be a table"));
        return;
    }
    for (const Json::Member &member : params.asObject())
        if (member.key != "url" && member.key != "method" && member.key != "headers" && member.key != "body" &&
            member.key != "encoding") {
            reply(badParam("/" + member.key, inQuotes(member.key) + " isn't part of a request"));
            return;
        }
    const Json *url = params.find("url");
    if (!url || !url->isString()) {
        reply(badParam("/url", "needs a url"));
        return;
    }
    const QUrl target(QString::fromStdString(url->asString()), QUrl::StrictMode);
    if (auto refusal = PluginNetwork::refusal(hosts, target)) {
        reply(std::unexpected(std::move(*refusal)));
        return;
    }
    std::string method = "GET";
    if (const Json *given = params.find("method")) {
        method = given->isString() ? given->asString() : std::string();
        if (std::ranges::find(kMethods, method) == kMethods.end()) {
            reply(badParam("/method", "the method is GET, HEAD, POST, PUT, PATCH or DELETE"));
            return;
        }
    }
    const Json *encoding = params.find("encoding");
    if (encoding && !(encoding->isString() && (encoding->asString() == "utf8" || encoding->asString() == "base64"))) {
        reply(badParam("/encoding", "the encoding is utf8 or base64"));
        return;
    }
    const bool base64 = encoding && encoding->asString() == "base64";
    const auto headers = requestHeaders(params.find("headers"));
    if (!headers) {
        reply(std::unexpected(headers.error()));
        return;
    }
    QByteArray body;
    if (const Json *given = params.find("body")) {
        if (!given->isString()) {
            reply(badParam("/body", "the body must be a string"));
            return;
        }
        if (method == "GET" || method == "HEAD") {
            reply(badParam("/body", method + " requests carry no body"));
            return;
        }
        body = QByteArray::fromStdString(given->asString());
        if (base64) {
            auto decoded = QByteArray::fromBase64Encoding(body, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) {
                reply(badParam("/body", "the body isn't base64"));
                return;
            }
            body = std::move(*decoded);
        }
        if (std::size_t(body.size()) > kMaxBodyBytes) {
            reply(refused("too_large", "the body is larger than 16 MiB"));
            return;
        }
    }

    QNetworkRequest request(target);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(kMaxRedirects);
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HikariSub/%1").arg(QCoreApplication::applicationVersion()));
    for (const auto &[name, value] : *headers)
        request.setRawHeader(name, value);

    auto open = std::make_unique<Open>();
    open->plugin = plugin;
    open->hosts = hosts;
    open->answer = std::move(reply);
    open->base64 = base64;
    QNetworkReply *sent = m_network.sendCustomRequest(request, QByteArray::fromStdString(method), body);
    open->reply = sent;
    Open *raw = open.get();
    m_open.push_back(std::move(open));
    connect(sent, &QNetworkReply::redirected, this, [raw, sent](const QUrl &next) {
        if (auto refusal = PluginNetwork::refusal(raw->hosts, next)) {
            raw->refusal = std::move(*refusal);
            sent->abort();
            return;
        }
        emit sent->redirectAllowed();
    });
    connect(sent, &QNetworkReply::readyRead, this, [raw, sent] {
        raw->body += sent->readAll();
        if (std::size_t(raw->body.size()) > kMaxBodyBytes) {
            raw->refusal = refused("too_large", "the response is larger than 16 MiB").error();
            sent->abort();
        }
    });
    connect(sent, &QNetworkReply::finished, this, [this, sent] { finish(sent); });
}

void PluginNetwork::finish(QNetworkReply *reply)
{
    const auto it = std::ranges::find(m_open, reply, &Open::reply);
    if (it == m_open.end())
        return;
    const std::unique_ptr<Open> open = std::move(*it);
    m_open.erase(it);
    reply->deleteLater();
    if (open->refusal) {
        open->answer(std::unexpected(std::move(*open->refusal)));
        return;
    }
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid()) {
        const bool late = reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError;
        open->answer(refused(late ? "timeout" : "failed",
                             late ? "the request took longer than 30 seconds" : reply->errorString().toStdString()));
        return;
    }
    open->body += reply->readAll();
    std::string body = open->body.toStdString();
    if (!open->base64 && !api::isUtf8(body)) {
        open->answer(refused("not_text", "the response is not UTF-8 text; fetch it with encoding base64"));
        return;
    }
    std::map<std::string, std::string> headers;
    for (const auto &[name, value] : reply->rawHeaderPairs()) {
        std::string &kept = headers[lowered(name.toStdString())];
        kept += (kept.empty() ? "" : ", ") + value.toStdString();
    }
    Json headerJson = Json::object();
    for (auto &[name, value] : headers)
        if (api::isUtf8(value))
            headerJson[name] = std::move(value);
    open->answer(Json::object({{"status", std::int64_t(status.toInt())},
                               {"url", reply->url().toString(QUrl::FullyEncoded).toStdString()},
                               {"headers", std::move(headerJson)},
                               {"encoding", open->base64 ? "base64" : "utf8"},
                               {"body", open->base64 ? open->body.toBase64().toStdString() : std::move(body)}}));
}

void PluginNetwork::cancel(const std::string &plugin)
{
    std::erase_if(m_open, [this, &plugin](const std::unique_ptr<Open> &open) {
        if (open->plugin != plugin)
            return false;
        disconnect(open->reply, nullptr, this, nullptr);
        open->reply->abort();
        open->reply->deleteLater();
        return true;
    });
}

}
