#pragma once

// A program plugin: the plugin protocol as JSON-RPC 2.0 on the program's stdio, one message per line (docs/plugins.md,
// Program plugins). It turns PluginProcess's requests into initialize, command and event calls, and the program's calls
// into Service payloads.

#include "hikari/backends/plugin_link.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace hikari::backends {

class ProgramProcess;

class ProgramPluginLink : public PluginLink {
    Q_OBJECT
public:
    struct Limits {
        int initializeMs = 30'000;
        int shutdownMs = 5'000;
        qsizetype maxLine = qsizetype(96) << 20;
        qsizetype maxDiagnostics = qsizetype(64) << 10;
    };
    // background: the program may call HikariSub while no request from HikariSub is open.
    ProgramPluginLink(std::unique_ptr<ProgramProcess> process, bool background, QObject *parent = nullptr);
    ProgramPluginLink(std::unique_ptr<ProgramProcess> process, bool background, Limits limits, QObject *parent = nullptr);
    ~ProgramPluginLink() override;

    QString name() const override { return m_name; }
    void setName(QString name) { m_name = std::move(name); }
    void start() override;
    bool ready() const override { return m_ready; }
    std::expected<std::uint64_t, helper::HostError> request(std::uint64_t run, std::vector<std::byte> payload,
                                                            helper::HelperHost::Handler handler) override;
    std::expected<std::uint64_t, helper::HostError> push(std::uint64_t run, std::vector<std::byte> payload,
                                                         helper::HelperHost::Handler handler) override;
    bool answer(std::uint64_t request, std::uint64_t call, helper::Outcome outcome, std::vector<std::byte> payload) override;
    void stop(bool now) override;
    QByteArray diagnostics() const override { return m_diagnostics; }
    qint64 processId() const override;

signals:
    void logLine(const QString &line);

private:
    enum class Stage { Single, EditLock, EditGroupBegin, EditGroupEnd, EditUnlock, Subscribe };
    struct Pending {
        std::uint64_t request = 0;
        helper::HelperHost::Handler handler;
        bool load = false;
    };
    struct Call {
        QJsonValue id; // undefined for a notification
        std::uint64_t request = 0;
        std::int32_t service = 0;
        Stage stage = Stage::Single;
        QJsonObject params;
        int waiting = 0;
        QString refusal;
    };

    std::expected<std::uint64_t, helper::HostError> send(std::vector<std::byte> payload, helper::HelperHost::Handler handler,
                                                         bool isEvent);
    void send(const QJsonObject &message, ProgramProcess *to = nullptr);
    void respond(const QJsonValue &id, const QJsonValue &result);
    void respondError(const QJsonValue &id, int code, const QString &message, const QJsonValue &data = QJsonValue::Undefined);
    void respondEnvelope(const QJsonValue &id, const QJsonObject &envelope);
    void onOutput(const QByteArray &bytes);
    void onLine(const QByteArray &line);
    void onAnswer(std::int64_t id, const QJsonObject &message);
    void onCall(const QJsonObject &message);
    void forward(std::uint64_t call, Call &record, std::vector<std::byte> payload);
    void finishCall(std::uint64_t call, const std::string &text, bool failed);
    void endCalls(std::uint64_t request);
    void deliver(const helper::HelperHost::Handler &handler, std::expected<helper::Event, helper::HostError> event);
    void onErrorOutput(const QByteArray &bytes);
    void appendDiagnostics(const QByteArray &bytes);
    void lose();

    std::unique_ptr<ProgramProcess> m_process;
    bool m_background = false;
    Limits m_limits;
    QString m_name = QStringLiteral("The program");
    bool m_ready = false;
    bool m_gone = false;
    QByteArray m_buffer;
    QByteArray m_errorLine;
    QByteArray m_diagnostics;
    QJsonObject m_settings;
    std::vector<std::string> m_commands;
    std::set<QString> m_muted;
    std::map<std::int64_t, Pending> m_pending;
    std::uint64_t m_open = 0;
    std::uint64_t m_nextRequest = 1;
    std::int64_t m_nextId = 1;
    std::uint64_t m_nextCall = 1;
    std::map<std::uint64_t, Call> m_calls;
    QTimer m_initializeTimer;
};

}
