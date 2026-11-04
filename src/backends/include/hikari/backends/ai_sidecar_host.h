#pragma once

#include "hikari/api/api.h"
#include "hikari/api/json.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace hikari::backends::ai {

/// The link's major version; the sidecar's `initialize` answer must carry the same.
inline constexpr int kSidecarProtocolVersion = 1;

namespace rpc {
inline constexpr int kHikariApiError = -32000;
inline constexpr int kProviderFailed = -32010;
inline constexpr int kBusy = -32003;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;
inline constexpr int kSignInFailed = -32011;
inline constexpr int kRequestCancelled = -32800;
}

enum class SidecarError {
    SidecarLost, // the sidecar crashed, broke the protocol or was stopped while the request was out
    Stopped,     // three crashes in a minute; nothing restarts until reset()
    Refused,     // the sidecar's versions differ from the app's; download it again
    Busy,        // the outstanding-request limits are reached
    Cancelled,
    Remote,      // the sidecar answered with a JSON-RPC error
};

struct SidecarFailure {
    SidecarError error = SidecarError::SidecarLost;
    int code = 0;
    std::string message;
    api::Json data;
};

using SidecarResult = std::expected<api::Json, SidecarFailure>;
using SidecarReply = std::function<void(SidecarResult)>;

/// The environment the sidecar starts with: only what a process needs to run, never a provider's key, base URL
/// or telemetry switch.
QProcessEnvironment sidecarEnvironment(const QProcessEnvironment &from);

/// Starts the AI sidecar on the first request and talks JSON-RPC 2.0 to it, one message per line on stdio.
/// Lives on its owner's thread, which needs a Qt event loop.
class AiSidecarHost final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Starting, Ready, Stopping, Stopped, Refused };

    struct Options {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        std::string buildId;
        int hikariApiVersion = api::kApiVersion;
        int handshakeMs = 10'000;
        int shutdownMs = 3'000;
        std::size_t maxOutstanding = 64;
        std::size_t maxOutstandingBytes = 16u << 20;
        std::size_t maxMessageBytes = 64u << 20;
        std::size_t maxDiagnosticBytes = 64u << 10; // the newest stderr bytes kept
        int crashLimit = 3;
        int crashWindowMs = 60'000;
        int firstBackoffMs = 500; // doubles with each crash in the window
    };

    using NotificationHandler = std::function<void(const api::Json &params)>;
    using Respond = std::function<void(SidecarResult)>;
    /// Answers one of the sidecar's requests, once, through `respond`; a SidecarFailure's code and message become
    /// the JSON-RPC error.
    using RequestHandler = std::function<void(const api::Json &params, Respond respond)>;

    /// `hikari` answers `_hikari/api.call`; without it the sidecar's tool calls fail as unavailable.
    AiSidecarHost(Options options, api::Api *hikari, api::Caller caller, QObject *parent = nullptr);
    ~AiSidecarHost() override;

    State state() const { return m_state; }
    /// Starts the sidecar when it isn't running; the request waits for `initialize`.
    std::uint64_t request(const std::string &method, api::Json params, SidecarReply reply);
    /// The reply still comes once, with Cancelled.
    void cancel(std::uint64_t request);
    void notify(const std::string &method, api::Json params);
    /// Replaces the method's handler.
    void onNotification(std::string method, NotificationHandler handler);
    /// Replaces the method's handler; `_hikari/api.call` always goes to Hikari's API.
    void onRequest(std::string method, RequestHandler handler);
    /// The Documents the user attached with @; the sidecar's tools see them besides the editing target.
    void setAttachedDocuments(std::vector<application::DocumentId> documents) { m_caller.attached = std::move(documents); }

    /// Closes stdin, waits, then kills the sidecar's process tree. Pending requests fail with SidecarLost.
    void shutdown();
    /// Lets a Stopped or Refused host start again on the next request.
    void reset();

    std::size_t outstanding() const { return m_pending.size(); }
    QByteArray diagnostics() const { return m_diagnostics; }
    qint64 processId() const { return m_process ? m_process->processId() : 0; }

signals:
    void ready();
    void refused(QString reason);
    void lost(QString reason);
    void stopped(QByteArray diagnostics);
    void finished();

private:
    struct Pending {
        std::string method;
        SidecarReply reply;
        std::size_t bytes = 0;
        bool sent = false;
    };

    void start();
    void startProcess();
    void handshake();
    void readOutput();
    void readDiagnostics();
    void receive(const QByteArray &line);
    void receiveRequest(const api::Json &id, const std::string &method, const api::Json *params);
    void receiveOwnRequest(const api::Json &id, const std::string &method, const api::Json *params);
    void receiveResponse(std::uint64_t id, const api::Json &message);
    void send(const api::Json &message);
    void sendQueued();
    void respond(const api::Json &id, api::Json body);
    void lose(const QString &reason);
    void killTree();
    void processGone();
    void failAll(const SidecarFailure &failure);
    void finish(std::uint64_t id, SidecarResult result);

    Options m_options;
    api::Api *m_hikari;
    api::Caller m_caller;
    std::unique_ptr<QProcess> m_process;
    State m_state = State::Idle;
    QByteArray m_buffer;
    QByteArray m_diagnostics;
    QTimer m_handshakeTimer;
    QTimer m_startTimer;
    QTimer m_killTimer;
    std::uint64_t m_nextId = 1;
    std::uint64_t m_initializeId = 0;
    std::map<std::uint64_t, Pending> m_pending;
    std::deque<std::uint64_t> m_queue;
    std::map<std::uint64_t, api::Json> m_queuedMessages;
    std::size_t m_pendingBytes = 0;
    std::uint64_t m_generation = 0;
    qint64 m_pid = 0;
    std::size_t m_incoming = 0;
    std::deque<qint64> m_crashes;
    QElapsedTimer m_clock;
    std::map<std::string, NotificationHandler, std::less<>> m_notificationHandlers;
    std::map<std::string, RequestHandler, std::less<>> m_requestHandlers;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
#ifdef Q_OS_WIN
    void *m_job = nullptr;
#endif
};

}
