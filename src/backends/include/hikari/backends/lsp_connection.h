#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QTimer>

#include <expected>
#include <functional>
#include <map>
#include <memory>

namespace hikari::backends {

class ProgramProcess;

// JSON-RPC 2.0 with LSP's Content-Length framing on a program's stdio.
class LspConnection : public QObject {
    Q_OBJECT
public:
    struct Limits {
        qsizetype maxFrame = qsizetype(64) << 20;
        int shutdownMs = 2'000;
    };
    struct Failure {
        int code = 0;
        QString message;
    };
    using Answer = std::function<void(std::expected<QJsonValue, Failure>)>;
    // The result of a request the server makes, or a Failure for an error response.
    using Handler = std::function<std::expected<QJsonValue, Failure>(const QString &method, const QJsonValue &params)>;

    static constexpr int kMethodNotFound = -32601;
    static constexpr int kGone = -32099;

    LspConnection(std::unique_ptr<ProgramProcess> process, Limits limits, QObject *parent = nullptr);
    ~LspConnection() override;

    // False with the sandbox's refusal when the program can't start.
    std::expected<void, QString> start();
    bool running() const;
    // `answer` comes once: with the response, or a kGone Failure when the program ends first.
    qint64 request(const QString &method, const QJsonValue &params, Answer answer);
    void cancel(qint64 id);
    void notify(const QString &method, const QJsonValue &params);
    void setHandler(Handler handler) { m_handler = std::move(handler); }
    // shutdown, then exit; the program is killed when it hasn't ended within Limits::shutdownMs.
    void shutdown();
    void kill();

signals:
    void notification(const QString &method, const QJsonValue &params);
    void errorLine(const QString &line);
    // crashed is false only after shutdown() or kill().
    void ended(bool crashed, const QString &why);

private:
    void send(QJsonObject message);
    void onOutput(const QByteArray &bytes);
    void onErrorOutput(const QByteArray &bytes);
    void onMessage(const QJsonObject &message);
    void fail(const QString &why);
    void finish(bool crashed, const QString &why);

    std::unique_ptr<ProgramProcess> m_process;
    Limits m_limits;
    Handler m_handler;
    QByteArray m_buffer;
    QByteArray m_errorLine;
    std::map<qint64, Answer> m_pending;
    qint64 m_next = 1;
    bool m_stopping = false;
    bool m_done = false;
    QTimer m_shutdownTimer;
};

}
