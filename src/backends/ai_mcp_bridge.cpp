#include "hikari/backends/ai_mcp_bridge.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hikari::backends::ai {

namespace {

const QString kNotListening = QStringLiteral(
    "HikariSub isn't running with its MCP server on. Start HikariSub, turn the MCP server on in Settings > AI, "
    "then connect again.");

void writeStdout(const QByteArray &bytes)
{
    std::fwrite(bytes.constData(), 1, std::size_t(bytes.size()), stdout);
    std::fflush(stdout);
}

template <typename Bridge>
void relayStdin(Bridge &bridge)
{
    std::thread([&bridge] {
        char buffer[64 << 10];
        for (;;) {
#ifdef _WIN32
            DWORD read = 0;
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof buffer, &read, nullptr) || read == 0)
                break;
#else
            const ssize_t read = ::read(STDIN_FILENO, buffer, sizeof buffer);
            if (read < 0 && errno == EINTR)
                continue;
            if (read <= 0)
                break;
#endif
            QMetaObject::invokeMethod(&bridge, [&bridge, bytes = QByteArray(buffer, qsizetype(read))] {
                bridge.fromAgent(bytes);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(&bridge, &Bridge::agentClosed, Qt::QueuedConnection);
    }).detach();
}

[[noreturn]] void exitBridge(int code)
{
    std::fflush(stdout);
    // A blocking stdin read can't be cancelled portably, so the reader thread is never joined.
    std::_Exit(code);
}

}

McpBridge::McpBridge(std::function<void(const QByteArray &)> toAgent, QObject *parent)
    : QObject(parent), m_toAgent(std::move(toAgent))
{
}

McpBridge::~McpBridge() = default;

bool McpBridge::open(const McpEndpoint &endpoint, QString *error)
{
    if (endpoint.socket.isEmpty()) {
        if (error)
            *error = kNotListening;
        return false;
    }
    m_socket = std::make_unique<QLocalSocket>();
    m_socket->connectToServer(endpoint.socket);
    if (!m_socket->waitForConnected(5'000)) {
        if (error)
            *error = kNotListening;
        m_socket.reset();
        return false;
    }
    connect(m_socket.get(), &QLocalSocket::readyRead, this, [this] { m_toAgent(m_socket->readAll()); });
    connect(m_socket.get(), &QLocalSocket::disconnected, this, &McpBridge::finished, Qt::QueuedConnection);
    m_socket->write(endpoint.token + '\n');
    return true;
}

void McpBridge::fromAgent(const QByteArray &bytes)
{
    if (m_socket)
        m_socket->write(bytes);
}

void McpBridge::agentClosed()
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        m_socket->disconnectFromServer();
    else
        emit finished();
}

McpHttpBridge::McpHttpBridge(QString url, QByteArray token, std::function<void(const QByteArray &)> toAgent,
                             QObject *parent)
    : QObject(parent), m_url(std::move(url)), m_token(std::move(token)), m_toAgent(std::move(toAgent)),
      m_network(std::make_unique<QNetworkAccessManager>())
{
    m_network->setProxy(QNetworkProxy::NoProxy);
}

McpHttpBridge::~McpHttpBridge() = default;

void McpHttpBridge::fromAgent(const QByteArray &bytes)
{
    m_buffer += bytes;
    for (qsizetype newline = m_buffer.indexOf('\n'); newline >= 0; newline = m_buffer.indexOf('\n')) {
        const QByteArray message = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (!message.isEmpty())
            post(message);
    }
}

void McpHttpBridge::agentClosed()
{
    if (const QByteArray rest = m_buffer.trimmed(); !rest.isEmpty())
        post(rest);
    m_buffer.clear();
    m_closed = true;
    settle();
}

void McpHttpBridge::post(const QByteArray &message)
{
    QNetworkRequest request{QUrl(m_url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("Authorization", "Bearer " + m_token);
    request.setTransferTimeout(0);
    QNetworkReply *reply = m_network->post(request, message);
    ++m_inFlight;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        --m_inFlight;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403 || status == 404 ||
            (reply->error() != QNetworkReply::NoError && status == 0)) {
            emit finished(QStringLiteral("HikariSub closed this session's tools channel."));
            return;
        }
        if (const QByteArray body = reply->readAll().trimmed(); !body.isEmpty())
            m_toAgent(body + '\n');
        settle();
    });
}

void McpHttpBridge::settle()
{
    if (m_closed && m_inFlight == 0)
        emit finished({});
}

void runMcpHttpBridge(const QString &url, const QByteArray &token)
{
    if (const QUrl channel(url); channel.scheme() != QLatin1String("http") || channel.host() != QLatin1String("127.0.0.1")) {
        std::fprintf(stderr, "%s\n", "HikariSub's tools channel is on 127.0.0.1 only.");
        std::_Exit(1);
    }
    McpHttpBridge bridge(url, token, writeStdout);
    QObject::connect(&bridge, &McpHttpBridge::finished, QCoreApplication::instance(), [](const QString &error) {
        if (!error.isEmpty())
            std::fprintf(stderr, "%s\n", qPrintable(error));
        exitBridge(error.isEmpty() ? 0 : 1);
    });
    relayStdin(bridge);
    QCoreApplication::exec();
    exitBridge(0);
}

void runMcpBridge(const QString &endpointFile)
{
    runMcpBridge(readMcpEndpoint(endpointFile).value_or(McpEndpoint{}));
}

void runMcpBridge(const McpEndpoint &endpoint)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    McpBridge bridge(writeStdout);
    QString error;
    if (!bridge.open(endpoint, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        std::_Exit(1);
    }
    QObject::connect(&bridge, &McpBridge::finished, QCoreApplication::instance(), &QCoreApplication::quit);
    relayStdin(bridge);
    QCoreApplication::exec();
    exitBridge(0);
}

}
