#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <optional>

namespace hikari::app {

// Discord's local RPC: each frame is a little-endian uint32 opcode and payload length, then JSON.
class DiscordIpc : public QObject {
    Q_OBJECT
public:
    enum Opcode : quint32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };
    struct Message {
        quint32 opcode = Frame;
        QJsonObject payload;
    };
    static constexpr qsizetype kMaxPayload = 64 * 1024;

    static QByteArray encode(quint32 opcode, const QJsonObject &payload);
    // Takes one whole frame off the front of `buffer`. Sets `broken` for a frame no client can read.
    static std::optional<Message> decode(QByteArray &buffer, bool *broken);

    // Linux: discord-ipc-0..9 in XDG_RUNTIME_DIR, TMPDIR, TMP, TEMP and /tmp, and in their Flatpak and
    // Snap folders. Windows: \\.\pipe\discord-ipc-0..9.
    static QStringList defaultCandidates(const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());

    DiscordIpc(QString clientId, QStringList candidates, QObject *parent = nullptr);
    ~DiscordIpc() override;

    // Tries each candidate in turn: ready() once Discord answers the handshake, else closed().
    void open();
    void close();
    bool isOpen() const { return m_state != State::Closed; }
    bool isReady() const { return m_state == State::Ready; }
    // An empty activity clears it. False until ready().
    bool setActivity(const QJsonObject &activity, qint64 pid);
    void flush(int timeoutMs);

signals:
    void ready();
    void closed();

private:
    enum class State { Closed, Connecting, Handshaking, Ready };
    void tryNext();
    void connected();
    void failed();
    void read();
    void lost();
    void write(quint32 opcode, const QJsonObject &payload);

    QString m_clientId;
    QStringList m_candidates;
    QLocalSocket m_socket;
    QTimer m_handshakeTimer;
    QByteArray m_buffer;
    qsizetype m_next = 0;
    State m_state = State::Closed;
};

}
