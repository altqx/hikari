#include "hikari/backends/ai_mcp_server.h"

#include "hikari/backends/ai_sidecar_host.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <array>

#ifdef _WIN32
#include <string>
#include <vector>

#include <windows.h>
#include <sddl.h>
#endif

namespace hikari::backends::ai {

namespace {

constexpr qsizetype kMaxMessageBytes = 16 << 20;
constexpr qsizetype kMaxHeaderBytes = 64 << 10;
constexpr const char *kMcpMessage = "_hikari/mcp.message";
constexpr const char *kMcpClosed = "_hikari/mcp.closed";
constexpr const char *kHttpConnection = "http";

bool sameSecret(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char differ = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        differ |= static_cast<unsigned char>(a[i] ^ b[i]);
    return differ == 0;
}

QByteArray randomHex(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), bytes / int(sizeof(quint32)));
    return raw.toHex();
}

/// A JSON-RPC error for a client request that never reached the sidecar; none for a notification.
std::optional<std::string> errorReply(const QByteArray &text, int code, std::string message)
{
    auto parsed = api::Json::parse(std::string_view(text.constData(), std::size_t(text.size())));
    const api::Json *id = parsed && parsed->isObject() ? parsed->find("id") : nullptr;
    if (parsed && parsed->isObject() && (!id || !parsed->find("method")))
        return std::nullopt;
    auto error = api::Json::object({{"code", code}, {"message", std::move(message)}});
    return api::Json::object({{"jsonrpc", "2.0"}, {"id", id ? *id : api::Json()}, {"error", std::move(error)}}).dump();
}

QByteArray reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 411: return "Length Required";
    case 413: return "Content Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

QByteArray httpResponse(int status, const QByteArray &body, bool keepAlive, bool json = false,
                        const QByteArray &extra = {})
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n";
    if (!body.isEmpty())
        head += json ? "Content-Type: application/json\r\n" : "Content-Type: text/plain; charset=utf-8\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n" + extra;
    head += keepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    return head + body;
}

struct HttpRequest {
    QByteArray method;
    QByteArray path;
    QByteArray version;
    std::map<QByteArray, QByteArray> headers;
    QByteArray body;

    QByteArray header(const QByteArray &name) const
    {
        const auto it = headers.find(name);
        return it == headers.end() ? QByteArray() : it->second;
    }
};

}

QString mcpEndpointFile(const QString &userFolder)
{
    return userFolder + QStringLiteral("/ai/mcp.json");
}

std::optional<McpEndpoint> readMcpEndpoint(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    McpEndpoint endpoint{o.value(QStringLiteral("socket")).toString(), o.value(QStringLiteral("token")).toString().toLatin1(),
                         quint16(o.value(QStringLiteral("port")).toInt())};
    if (endpoint.token.isEmpty())
        return std::nullopt;
    return endpoint;
}

#ifdef _WIN32
namespace {

bool createForUserAlone(const QString &file)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> user(size);
    const bool read = GetTokenInformation(token, TokenUser, user.data(), size, &size) != FALSE;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!read || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &sid))
        return false;
    const std::wstring sddl = L"D:P(A;;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        return false;
    SECURITY_ATTRIBUTES attributes{DWORD(sizeof(attributes)), descriptor, FALSE};
    const HANDLE created = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(file).utf16()), GENERIC_WRITE,
                                       0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    if (created == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_EXISTS;
    CloseHandle(created);
    return true;
}

}
#endif

bool writeMcpEndpoint(const QString &file, const McpEndpoint &endpoint)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
#ifdef _WIN32
    if (!QFileInfo::exists(file) && !createForUserAlone(file))
        return false;
#endif
    QJsonObject o{{QStringLiteral("token"), QString::fromLatin1(endpoint.token)}};
    if (!endpoint.socket.isEmpty())
        o.insert(QStringLiteral("socket"), endpoint.socket);
    if (endpoint.port != 0)
        o.insert(QStringLiteral("port"), int(endpoint.port));
    const auto ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate, ownerOnly) || !f.setPermissions(ownerOnly))
        return false;
    return f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) >= 0 && f.flush();
}

QByteArray newMcpToken()
{
    return randomHex(32);
}

struct McpServer::Local {
    std::uint64_t id = 0;
    QPointer<QLocalSocket> socket;
    QByteArray buffer;
    bool authorised = false;
    std::string channel;
    std::string connection() const { return channel.empty() ? "local-" + std::to_string(id) : "channel:" + channel; }
};

struct McpServer::Http {
    std::uint64_t id = 0;
    QPointer<QTcpSocket> socket;
    QByteArray buffer;
    bool busy = false;
    bool continued = false;
    bool keepAlive = true;
};

McpServer::McpServer(SidecarFor sidecar, QByteArray token, QObject *parent)
    : QObject(parent), m_sidecar(std::move(sidecar)), m_token(std::move(token))
{
}

McpServer::~McpServer()
{
    *m_alive = false;
    if (!m_host)
        return;
    if (m_channel.empty())
        m_host->setMcpGate({});
    for (const auto &[id, client] : m_local)
        if (client->authorised)
            m_host->notify(kMcpClosed, api::Json::object({{"connection", client->connection()}}));
    if (m_httpServer)
        m_host->notify(kMcpClosed, api::Json::object({{"connection", httpConnection()}}));
}

bool McpServer::listenLocal(QString *error)
{
    m_localServer = std::make_unique<QLocalServer>();
    m_localServer->setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = QStringLiteral("hikarisub-mcp-") + QString::fromLatin1(randomHex(8));
    if (!m_localServer->listen(name)) {
        if (error)
            *error = m_localServer->errorString();
        m_localServer.reset();
        return false;
    }
    connect(m_localServer.get(), &QLocalServer::newConnection, this, &McpServer::acceptLocal);
    return true;
}

void McpServer::setChannel(std::string session)
{
    m_channel = std::move(session);
}

std::string McpServer::httpConnection() const
{
    return m_channel.empty() ? std::string(kHttpConnection) : "channel:" + m_channel;
}

bool McpServer::channelTaken() const
{
    return std::ranges::any_of(m_local, [](const auto &entry) { return entry.second->authorised; });
}

QString McpServer::socketName() const
{
    return m_localServer ? m_localServer->fullServerName() : QString();
}

bool McpServer::listenHttp(quint16 port, QString *error)
{
    closeHttp();
    m_httpServer = std::make_unique<QTcpServer>();
    if (!m_httpServer->listen(QHostAddress::LocalHost, port)) {
        if (error)
            *error = m_httpServer->errorString();
        m_httpServer.reset();
        return false;
    }
    connect(m_httpServer.get(), &QTcpServer::newConnection, this, &McpServer::acceptHttp);
    return true;
}

void McpServer::closeHttp()
{
    if (!m_httpServer)
        return;
    m_httpServer.reset();
    for (auto &[id, client] : m_http)
        if (client->socket)
            client->socket->deleteLater();
    m_http.clear();
    if (m_host)
        m_host->notify(kMcpClosed, api::Json::object({{"connection", httpConnection()}}));
}

quint16 McpServer::httpPort() const
{
    return m_httpServer ? m_httpServer->serverPort() : 0;
}

void McpServer::setToken(QByteArray token)
{
    m_token = std::move(token);
    for (const auto &[id, client] : m_local)
        if (client->socket)
            client->socket->disconnectFromServer();
    for (const auto &[id, client] : m_http)
        if (client->socket)
            client->socket->disconnectFromHost();
    if (m_host && m_httpServer)
        m_host->notify(kMcpClosed, api::Json::object({{"connection", httpConnection()}}));
}

std::expected<api::Caller, api::Error> McpServer::callerFor(const api::Json &mcp, const std::string &operation) const
{
    const api::Json *connection = mcp.isObject() ? mcp.find("connection") : nullptr;
    if (!connection || !connection->isString())
        return api::fail(api::errors::kNotGranted, "The call names no MCP connection.");
    const api::Operation *op = api::Api::registry().find(operation);
    if (op && op->access == api::Access::Write && operation != "document.apply")
        return api::fail(api::errors::kRefused, "Through MCP, only document.apply writes; its edits land in the review.");
    const api::Json *client = mcp.find("client");
    api::Caller caller;
    caller.id = "mcp:" + connection->asString();
    caller.label = client && client->isString() && !client->asString().empty() ? client->asString() : "MCP agent";
    caller.allDocuments = true;
    caller.reviewed = true;
    return caller;
}

AiSidecarHost *McpServer::host()
{
    AiSidecarHost *sidecar = m_sidecar ? m_sidecar() : nullptr;
    if (sidecar && sidecar != m_host) {
        if (m_host && m_channel.empty())
            m_host->setMcpGate({});
        m_host = sidecar;
        if (m_channel.empty())
            sidecar->setMcpGate([this](const api::Json &mcp, const std::string &operation) { return callerFor(mcp, operation); });
    }
    return sidecar;
}

void McpServer::relay(const std::string &connection, QByteArray text, api::Json http,
                      std::function<void(std::optional<std::string>, int)> answer)
{
    if (!api::isUtf8(std::string_view(text.constData(), std::size_t(text.size()))))
        return answer(errorReply(text, -32700, "Parse error"), 400);
    AiSidecarHost *sidecar = host();
    if (!sidecar)
        return answer(errorReply(text, -32603, "HikariSub's AI sidecar isn't installed; install it in Settings > AI."), 503);
    auto params = api::Json::object({{"connection", connection}, {"text", text.toStdString()}});
    if (!http.isNull())
        params["http"] = std::move(http);
    auto relayed = [alive = std::weak_ptr(m_alive), text, answer = std::move(answer)](SidecarResult result) {
        if (alive.expired())
            return;
        if (!result)
            return answer(errorReply(text, -32603, "HikariSub's AI sidecar failed: " + result.error().message), 503);
        const api::Json *reply = result->find("reply");
        const api::Json *status = result->find("status");
        answer(reply && reply->isString() ? std::optional(reply->asString()) : std::nullopt,
               status && status->isInt() ? int(status->asInt()) : 200);
    };
    sidecar->request(kMcpMessage, std::move(params), std::move(relayed));
}

void McpServer::acceptLocal()
{
    while (QLocalSocket *socket = m_localServer->nextPendingConnection()) {
        auto client = std::make_unique<Local>();
        client->id = m_nextId++;
        client->socket = socket;
        const std::uint64_t id = client->id;
        connect(socket, &QLocalSocket::readyRead, this, [this, id] {
            if (const auto it = m_local.find(id); it != m_local.end())
                readLocal(*it->second);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, id] { dropLocal(id); }, Qt::QueuedConnection);
        m_local.emplace(id, std::move(client));
    }
}

void McpServer::readLocal(Local &client)
{
    client.buffer += client.socket->readAll();
    for (qsizetype end; (end = client.buffer.indexOf('\n')) >= 0;) {
        QByteArray line = client.buffer.left(end);
        client.buffer.remove(0, end + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (!client.authorised) {
            if (!sameSecret(line, m_token) || (!m_channel.empty() && channelTaken()))
                return client.socket->disconnectFromServer();
            client.authorised = true;
            client.channel = m_channel;
            continue;
        }
        if (line.trimmed().isEmpty())
            continue;
        relay(client.connection(), std::move(line), {},
              [socket = client.socket](std::optional<std::string> reply, int) {
                  if (socket && reply)
                      socket->write(QByteArray::fromStdString(*reply + '\n'));
              });
    }
    if (client.buffer.size() > (client.authorised ? kMaxMessageBytes : m_token.size() + 2))
        client.socket->disconnectFromServer();
}

void McpServer::dropLocal(std::uint64_t id)
{
    const auto it = m_local.find(id);
    if (it == m_local.end())
        return;
    if (it->second->authorised && m_host)
        m_host->notify(kMcpClosed, api::Json::object({{"connection", it->second->connection()}}));
    if (it->second->socket)
        it->second->socket->deleteLater();
    m_local.erase(it);
}

void McpServer::acceptHttp()
{
    while (QTcpSocket *socket = m_httpServer->nextPendingConnection()) {
        auto client = std::make_unique<Http>();
        client->id = m_nextId++;
        client->socket = socket;
        const std::uint64_t id = client->id;
        connect(socket, &QTcpSocket::readyRead, this, [this, id] {
            if (const auto it = m_http.find(id); it != m_http.end())
                readHttp(*it->second);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, id] { dropHttp(id); }, Qt::QueuedConnection);
        m_http.emplace(id, std::move(client));
    }
}

void McpServer::readHttp(Http &client)
{
    client.buffer += client.socket->readAll();
    if (!client.busy)
        answerHttp(client);
}

void McpServer::answerHttp(Http &client)
{
    const auto send = [&client](int status, const QByteArray &body, const QByteArray &extra = {}) {
        client.socket->write(httpResponse(status, body, client.keepAlive, false, extra));
        if (!client.keepAlive)
            client.socket->disconnectFromHost();
    };
    const auto refuse = [&](int status, const QByteArray &body, const QByteArray &extra = {}) {
        client.keepAlive = false;
        send(status, body, extra);
    };

    const qsizetype headEnd = client.buffer.indexOf("\r\n\r\n");
    if (headEnd < 0) {
        if (client.buffer.size() > kMaxHeaderBytes)
            refuse(431, "Headers too large");
        return;
    }
    HttpRequest request;
    const QList<QByteArray> lines = client.buffer.left(headEnd).split('\n');
    const QList<QByteArray> start = lines.first().trimmed().split(' ');
    if (start.size() != 3)
        return refuse(400, "Bad request line");
    request.method = start[0];
    request.path = start[1].left(start[1].indexOf('?') < 0 ? start[1].size() : start[1].indexOf('?'));
    request.version = start[2];
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines[i].indexOf(':');
        if (colon <= 0)
            return refuse(400, "Bad header");
        request.headers[lines[i].left(colon).trimmed().toLower()] = lines[i].mid(colon + 1).trimmed();
    }
    if (!request.header("transfer-encoding").isEmpty())
        return refuse(501, "Send the body with Content-Length");
    bool lengthOk = true;
    const QByteArray lengthHeader = request.header("content-length");
    const qsizetype length = lengthHeader.isEmpty() ? 0 : lengthHeader.toLongLong(&lengthOk);
    if (!lengthOk || length < 0)
        return refuse(400, "Bad Content-Length");
    if (length > kMaxMessageBytes)
        return refuse(413, "Message too large");
    if (client.buffer.size() < headEnd + 4 + length) {
        if (!client.continued && request.header("expect").toLower() == "100-continue") {
            client.continued = true;
            client.socket->write("HTTP/1.1 100 Continue\r\n\r\n");
        }
        return;
    }
    request.body = client.buffer.mid(headEnd + 4, length);
    client.buffer.remove(0, headEnd + 4 + length);
    client.continued = false;
    client.keepAlive = request.version == "HTTP/1.1" && request.header("connection").toLower() != "close";

    const QByteArray port = QByteArray::number(httpPort());
    const QByteArray host = request.header("host");
    if (host != "127.0.0.1:" + port && host != "localhost:" + port)
        return refuse(403, "Unknown Host");
    const QByteArray origin = request.header("origin");
    if (!origin.isEmpty() && origin != "http://127.0.0.1:" + port && origin != "http://localhost:" + port)
        return refuse(403, "Unknown Origin");
    if (request.path != "/mcp")
        return send(404, "Not found");
    if (request.method != "POST")
        return send(405, "Only POST", "Allow: POST\r\n");
    const QByteArray authorization = request.header("authorization");
    if (!authorization.startsWith("Bearer ") || !sameSecret(authorization.mid(7).trimmed(), m_token))
        return send(401, "Unauthorized", "WWW-Authenticate: Bearer\r\n");
    if (!request.header("content-type").toLower().startsWith("application/json"))
        return send(415, "Send application/json");

    auto headers = api::Json::object();
    for (const auto &[header, key] : std::array<std::pair<const char *, const char *>, 3>{
             {{"mcp-protocol-version", "protocolVersion"}, {"mcp-method", "method"}, {"mcp-name", "name"}}})
        if (const QByteArray value = request.header(header); !value.isEmpty())
            headers[key] = value.toStdString();
    client.busy = true;
    const std::uint64_t id = client.id;
    relay(httpConnection(), std::move(request.body), std::move(headers),
          [this, id, alive = std::weak_ptr(m_alive)](std::optional<std::string> reply, int status) {
              const auto it = m_http.find(id);
              if (alive.expired() || it == m_http.end() || !it->second->socket)
                  return;
              Http &client = *it->second;
              client.socket->write(reply ? httpResponse(status, QByteArray::fromStdString(*reply), client.keepAlive, true)
                                         : httpResponse(202, {}, client.keepAlive));
              client.busy = false;
              if (!client.keepAlive)
                  return client.socket->disconnectFromHost();
              QTimer::singleShot(0, this, [this, id] {
                  if (const auto next = m_http.find(id); next != m_http.end() && !next->second->busy)
                      answerHttp(*next->second);
              });
          });
}

void McpServer::dropHttp(std::uint64_t id)
{
    const auto it = m_http.find(id);
    if (it == m_http.end())
        return;
    if (it->second->socket)
        it->second->socket->deleteLater();
    m_http.erase(it);
}

}
