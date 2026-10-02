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
    m_process.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size()));
}

void HelperHost::readOutput()
{
    const QByteArray data = m_process.readAllStandardOutput();
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
                            frame->kind == Kind::Terminal);
        if (!known) {
            ++m_rejected; // late, duplicated, foreign-session or unknown
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

void HelperHost::stop()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
    fail();
}

void HelperHost::fail()
{
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
