#pragma once

#include "hikari/backends/helper_protocol.h"

#include <QObject>
#include <QProcess>
#include <QStringList>

#include <expected>
#include <functional>
#include <map>
#include <set>

namespace hikari::backends::helper {

enum class HostError {
    NotReady,   // not started, still starting, refused or lost
    Busy,       // the outstanding-request limits are reached
    HelperLost,
};

struct Event {
    Kind kind = Kind::Reply; // Reply, Progress, Service or Terminal
    Outcome outcome = Outcome::Ok; // for Terminal
    std::vector<std::byte> payload; // for Service, without the call ID
    std::uint64_t call = 0;         // for Service: pass to answer()
};

// Lives on its owner's thread, which needs a Qt event loop.
class HelperHost : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Starting, Ready, Refused, Lost };

    struct Limits {
        std::size_t maxOutstanding = 64;
        std::size_t maxOutstandingBytes = 16u << 20;
        int handshakeMs = 10'000;
        std::size_t maxDiagnosticBytes = 64u << 10; // the newest stderr bytes kept
    };
    // Called for each Reply/Progress/Service, then once with the Terminal or with
    // HelperLost (as an unexpected error).
    using Handler = std::function<void(std::expected<Event, HostError>)>;

    HelperHost(QString program, QStringList arguments, std::uint32_t protocolVersion, QObject *parent = nullptr);
    HelperHost(QString program, QStringList arguments, std::uint32_t protocolVersion, Limits limits,
               QObject *parent = nullptr);
    ~HelperHost() override;

    void start();
    State state() const { return m_state; }
    std::uint64_t session() const { return m_session; } // process-session generation
    QString helperName() const { return m_name; }

    std::expected<std::uint64_t, HostError> request(std::uint64_t run, std::vector<std::byte> payload,
                                                    Handler handler);
    void cancel(std::uint64_t request);
    // False, dropping the answer, when the call is unknown, answered, or its request ended.
    bool answer(std::uint64_t request, std::uint64_t call, Outcome outcome, std::vector<std::byte> payload = {});
    void stop(); // ends the helper; pending requests resolve as HelperLost

    std::size_t rejectedFrames() const { return m_rejected; }
    std::size_t outstanding() const { return m_pending.size(); }
    QByteArray diagnostics() const { return m_diagnostics; }
    std::size_t droppedDiagnosticBytes() const { return m_droppedDiagnostics; }
    qint64 processId() const { return m_process.processId(); } // 0 when not running

signals:
    void ready();
    void refused(quint32 helperVersion);
    void lost();

private:
    void readOutput();
    void readDiagnostics();
    void fail();
    void write(const Frame &frame);
    // One write per turn: QProcess on Windows starts each write() a round trip after the last.
    void flush();

    QString m_program;
    QStringList m_arguments;
    std::uint32_t m_version;
    Limits m_limits;
    QProcess m_process;
    QByteArray m_outgoing;
    bool m_flushQueued = false;
    Decoder m_decoder;
    State m_state = State::Idle;
    std::uint64_t m_session = 0;
    std::uint64_t m_nextRequest = 1;
    QString m_name;
    struct Pending {
        Handler handler;
        std::size_t bytes = 0;
        std::set<std::uint64_t> calls;
    };
    std::map<std::uint64_t, Pending> m_pending;
    std::size_t m_pendingBytes = 0;
    std::size_t m_rejected = 0;
    bool m_stopping = false;
    QByteArray m_diagnostics;
    std::size_t m_droppedDiagnostics = 0;
    static inline std::uint64_t s_nextSession = 1;
};

} // namespace hikari::backends::helper
