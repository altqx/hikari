#include "hikari/backends/ai_mcp_bridge.h"

#include <QCoreApplication>
#include <QLocalSocket>

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

void runMcpBridge(const QString &endpointFile)
{
    runMcpBridge(readMcpEndpoint(endpointFile).value_or(McpEndpoint{}));
}

void runMcpBridge(const McpEndpoint &endpoint)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    McpBridge bridge([](const QByteArray &bytes) {
        std::fwrite(bytes.constData(), 1, std::size_t(bytes.size()), stdout);
        std::fflush(stdout);
    });
    QString error;
    if (!bridge.open(endpoint, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        std::_Exit(1);
    }
    QObject::connect(&bridge, &McpBridge::finished, QCoreApplication::instance(), &QCoreApplication::quit);
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
        QMetaObject::invokeMethod(&bridge, &McpBridge::agentClosed, Qt::QueuedConnection);
    }).detach();
    QCoreApplication::exec();
    std::fflush(stdout);
    // A blocking stdin read can't be cancelled portably, so the reader thread is never joined.
    std::_Exit(0);
}

}
