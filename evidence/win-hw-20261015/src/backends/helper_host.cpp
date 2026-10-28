#include "hikari/backends/helper_host.h"

#include <QTimer>

namespace hikari::backends::helper {

HelperHost::HelperHost(QString program, QStringList arguments, std::uint32_t protocolVersion, QObject *parent)
    : HelperHost(std::move(program), std::move(arguments), protocolVersion, Limits{}, parent)
{
}

HelperHost::HelperHost(QString program, QStringList arguments, std::uint32_t protocolVersion, Limits limits,
                       QObject *parent)
    : QObject(parent), m_program(std::move(program)), m_arguments(std::move(arguments)), m_version(protocolVersion),
      m_limits(limits)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.setReadChannel(QProcess::StandardOutput);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &HelperHost::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &HelperHost::readDiagnostics);
    connect(&m_process, &QProcess::finished, this, [this] { fail(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart || e == QProcess::Crashed)
            fail();
    });
}

HelperHost::~HelperHost()
{
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        flush();
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
}

void HelperHost::start()
{
    if (m_state != State::Idle)
        return;
    m_state = State::Starting;
    m_process.start(m_program, m_arguments);
    QTimer::singleShot(m_limits.handshakeMs, this, [this] {
        if (m_state == State::Starting)
            stop();
    });
}

void HelperHost::write(const Frame &frame)
{
    const auto bytes = encode(frame);
    m_outgoing.append(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    if (!m_flushQueued) {
        m_flushQueued = true;
        QMetaObject::invokeMethod(this, &HelperHost::flush, Qt::QueuedConnection);
    }
}

void HelperHost::flush()
{
    m_flushQueued = false;
    if (m_outgoing.isEmpty())
        return;
    if (m_process.state() != QProcess::NotRunning)
        m_process.write(m_outgoing);
    m_outgoing.clear();
}

void HelperHost::readOutput()
{
    const QByteArray data = m_process.readAllStandardOutput();
    if (m_stopping)
        return; // a deliberate stop: replies racing it are dropped
    m_decoder.feed(reinterpret_cast<const std::byte *>(data.constData()), static_cast<std::size_t>(data.size()));
    while (auto frame = m_decoder.next()) {
        if (m_state == State::Starting) {
            if (frame->kind != Kind::Hello) {
                ++m_rejected;
                continue;
            }
            m_name = QString::fromStdString(textOf(frame->payload));
            if (frame->code != m_version) {
                // Refuse before any work: the helper exits on Refuse.
                write(Frame{Kind::Refuse, static_cast<std::uint16_t>(m_version), 0, 0, 0, {}});
                m_state = State::Refused;
                flush();
                m_process.closeWriteChannel();
                emit refused(frame->code);
                return;
            }
            m_session = s_nextSession++;
            write(Frame{Kind::Welcome, 0, m_session, 0, 0, {}});
            m_state = State::Ready;
            emit ready();
            continue;
        }
        if (m_state != State::Ready)
            return;
        const auto it = m_pending.find(frame->request);
        const bool known = frame->session == m_session && it != m_pending.end() &&
                           (frame->kind == Kind::Reply || frame->kind == Kind::Progress ||
                            frame->kind == Kind::Service || frame->kind == Kind::Terminal);
        if (!known || (frame->kind == Kind::Service && frame->payload.size() < 8)) {
            ++m_rejected; // late, duplicated, foreign-session, unknown or malformed
            continue;
        }
        if (frame->kind == Kind::Service) {
            Reader r(frame->payload);
            const auto call = static_cast<std::uint64_t>(r.i64());
            if (!it->second.calls.insert(call).second) {
                ++m_rejected; // a call ID reused while unanswered
                continue;
            }
            frame->payload.erase(frame->payload.begin(), frame->payload.begin() + 8);
            it->second.handler(Event{Kind::Service, Outcome::Ok, std::move(frame->payload), call});
            continue;
        }
        if (frame->kind == Kind::Terminal) {
            Pending pending = std::move(it->second);
            m_pendingBytes -= pending.bytes;
            m_pending.erase(it);
            pending.handler(Event{Kind::Terminal, static_cast<Outcome>(frame->code), std::move(frame->payload)});
        } else {
            it->second.handler(Event{frame->kind, Outcome::Ok, std::move(frame->payload)});
        }
    }
    if (m_decoder.failed())
        stop(); // a broken stream ends the session
}

std::expected<std::uint64_t, HostError> HelperHost::request(std::uint64_t run, std::vector<std::byte> payload,
                                                            Handler handler)
{
    if (m_state != State::Ready)
        return std::unexpected(HostError::NotReady);
    if (m_pending.size() >= m_limits.maxOutstanding ||
        m_pendingBytes + payload.size() > m_limits.maxOutstandingBytes)
        return std::unexpected(HostError::Busy);
    const std::uint64_t id = m_nextRequest++;
    const std::size_t bytes = payload.size();
    m_pending.emplace(id, Pending{std::move(handler), bytes});
    m_pendingBytes += bytes;
    write(Frame{Kind::Request, 0, m_session, run, id, std::move(payload)});
    return id;
}

void HelperHost::cancel(std::uint64_t request)
{
    if (m_state == State::Ready && m_pending.contains(request))
        write(Frame{Kind::Cancel, 0, m_session, 0, request, {}});
}

bool HelperHost::answer(std::uint64_t request, std::uint64_t call, Outcome outcome, std::vector<std::byte> payload)
{
    const auto it = m_pending.find(request);
    if (m_state != State::Ready || it == m_pending.end() || it->second.calls.erase(call) == 0)
        return false;
    Writer w;
    w.i64(static_cast<std::int64_t>(call));
    auto body = w.take();
    body.insert(body.end(), payload.begin(), payload.end());
    write(Frame{Kind::ServiceReply, static_cast<std::uint16_t>(outcome), m_session, 0, request, std::move(body)});
    return true;
}

void HelperHost::readDiagnostics()
{
    m_diagnostics += m_process.readAllStandardError();
    const auto limit = static_cast<qsizetype>(m_limits.maxDiagnosticBytes);
    if (m_diagnostics.size() > limit) {
        m_droppedDiagnostics += static_cast<std::size_t>(m_diagnostics.size() - limit);
        m_diagnostics.remove(0, m_diagnostics.size() - limit);
    }
}

void HelperHost::stop()
{
    // Requests pending now resolve HelperLost, even if their replies are
    // already in the pipe (waitForFinished reads output while it waits).
    m_stopping = true;
    m_outgoing.clear(); // the helper is killed; nothing more reaches it
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    fail();
    m_stopping = false;
}

void HelperHost::fail()
{
    readDiagnostics(); // keep the last words of a helper that died
    const bool wasLive = m_state == State::Ready || m_state == State::Starting;
    if (m_state != State::Refused)
        m_state = State::Lost;
    // Resolve every pending request once, then forget them.
    auto pending = std::move(m_pending);
    m_pending.clear();
    m_pendingBytes = 0;
    for (auto &[id, p] : pending)
        p.handler(std::unexpected(HostError::HelperLost));
    if (wasLive)
        emit lost();
}

} // namespace hikari::backends::helper
