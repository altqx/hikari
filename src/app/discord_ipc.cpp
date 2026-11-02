#include "hikari/app/discord_ipc.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>
#include <QtEndian>

#include <utility>

namespace hikari::app {

namespace {

constexpr qsizetype kHeader = 8;
constexpr int kHandshakeTimeoutMs = 5000;

}

QByteArray DiscordIpc::encode(quint32 opcode, const QJsonObject &payload)
{
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame(kHeader, Qt::Uninitialized);
    qToLittleEndian<quint32>(opcode, frame.data());
    qToLittleEndian<quint32>(quint32(json.size()), frame.data() + 4);
    return frame + json;
}

std::optional<DiscordIpc::Message> DiscordIpc::decode(QByteArray &buffer, bool *broken)
{
    *broken = false;
    if (buffer.size() < kHeader)
        return std::nullopt;
    const auto opcode = qFromLittleEndian<quint32>(buffer.constData());
    const auto length = qFromLittleEndian<quint32>(buffer.constData() + 4);
    if (opcode > Pong || length > quint32(kMaxPayload)) {
        *broken = true;
        return std::nullopt;
    }
    if (buffer.size() < kHeader + qsizetype(length))
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(buffer.mid(kHeader, length), &error);
    buffer.remove(0, kHeader + length);
    if (error.error != QJsonParseError::NoError || !json.isObject()) {
        *broken = true;
        return std::nullopt;
    }
    return Message{opcode, json.object()};
}

QStringList DiscordIpc::defaultCandidates(const QProcessEnvironment &environment)
{
    QStringList candidates;
#ifdef _WIN32
    Q_UNUSED(environment)
    for (int n = 0; n < 10; ++n)
        candidates << QStringLiteral("\\\\.\\pipe\\discord-ipc-%1").arg(n);
#else
    QStringList bases;
    for (const char *name : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
        if (const QString base = environment.value(QLatin1String(name)); !base.isEmpty())
            bases << base;
    bases << QStringLiteral("/tmp");
    const QStringList folders{QString(), QStringLiteral("app/com.discordapp.Discord"),
                              QStringLiteral("app/com.discordapp.DiscordCanary"), QStringLiteral("snap.discord"),
                              QStringLiteral("snap.discord-canary")};
    for (const QString &base : bases)
        for (const QString &folder : folders)
            for (int n = 0; n < 10; ++n)
                candidates << QDir::cleanPath(base + QLatin1Char('/') + folder + QStringLiteral("/discord-ipc-%1").arg(n));
    candidates.removeDuplicates();
#endif
    return candidates;
}

DiscordIpc::DiscordIpc(QString clientId, QStringList candidates, QObject *parent)
    : QObject(parent), m_clientId(std::move(clientId)), m_candidates(std::move(candidates))
{
    m_handshakeTimer.setSingleShot(true);
    m_handshakeTimer.setInterval(kHandshakeTimeoutMs);
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this] {
        m_socket.abort();
        lost();
    });
    connect(&m_socket, &QLocalSocket::connected, this, &DiscordIpc::connected);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &DiscordIpc::failed);
    connect(&m_socket, &QLocalSocket::disconnected, this, &DiscordIpc::lost);
    connect(&m_socket, &QLocalSocket::readyRead, this, &DiscordIpc::read);
}

DiscordIpc::~DiscordIpc()
{
    m_socket.disconnect(this);
    m_handshakeTimer.disconnect(this);
}

void DiscordIpc::open()
{
    if (m_state != State::Closed)
        return;
    m_state = State::Connecting;
    m_next = 0;
    tryNext();
}

void DiscordIpc::tryNext()
{
    if (m_state != State::Connecting || m_socket.state() != QLocalSocket::UnconnectedState)
        return;
#ifndef _WIN32
    while (m_next < m_candidates.size() && !QFileInfo::exists(m_candidates.at(m_next)))
        ++m_next;
#endif
    if (m_next >= m_candidates.size()) {
        m_state = State::Closed;
        emit closed();
        return;
    }
    m_socket.connectToServer(m_candidates.at(m_next++));
}

void DiscordIpc::connected()
{
    if (m_state != State::Connecting)
        return;
    m_state = State::Handshaking;
    m_buffer.clear();
    m_handshakeTimer.start();
    write(Handshake, {{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), m_clientId}});
}

void DiscordIpc::failed()
{
    if (m_state == State::Connecting) {
        m_socket.abort();
        QTimer::singleShot(0, this, &DiscordIpc::tryNext);
    } else if (m_socket.state() == QLocalSocket::UnconnectedState) {
        lost();
    }
}

void DiscordIpc::read()
{
    m_buffer += m_socket.readAll();
    while (m_state == State::Handshaking || m_state == State::Ready) {
        bool broken = false;
        const std::optional<Message> message = decode(m_buffer, &broken);
        if (broken) {
            m_socket.abort();
            lost();
            return;
        }
        if (!message)
            return;
        switch (message->opcode) {
        case Frame:
            if (m_state == State::Handshaking &&
                message->payload.value(QStringLiteral("evt")).toString() == QLatin1String("READY")) {
                m_handshakeTimer.stop();
                m_state = State::Ready;
                emit ready();
            }
            break;
        case Ping:
            write(Pong, message->payload);
            break;
        case Close:
            m_socket.abort();
            lost();
            return;
        default:
            break;
        }
    }
}

void DiscordIpc::lost()
{
    if (m_state == State::Closed || m_state == State::Connecting)
        return;
    m_state = State::Closed;
    m_handshakeTimer.stop();
    m_buffer.clear();
    emit closed();
}

void DiscordIpc::close()
{
    if (m_state == State::Closed)
        return;
    m_state = State::Closed;
    m_handshakeTimer.stop();
    m_buffer.clear();
    if (m_socket.state() == QLocalSocket::ConnectedState)
        m_socket.disconnectFromServer();
    else
        m_socket.abort();
}

bool DiscordIpc::setActivity(const QJsonObject &activity, qint64 pid)
{
    if (m_state != State::Ready)
        return false;
    QJsonObject args{{QStringLiteral("pid"), pid}};
    if (!activity.isEmpty())
        args.insert(QStringLiteral("activity"), activity);
    write(Frame, {{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
                  {QStringLiteral("args"), args},
                  {QStringLiteral("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces)}});
    return true;
}

void DiscordIpc::flush(int timeoutMs)
{
    if (m_socket.state() == QLocalSocket::ConnectedState && m_socket.bytesToWrite() > 0)
        m_socket.waitForBytesWritten(timeoutMs);
}

void DiscordIpc::write(quint32 opcode, const QJsonObject &payload)
{
    m_socket.write(encode(opcode, payload));
}

}
