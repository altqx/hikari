#include "hikari/backends/line_audition.h"

#include <chrono>

namespace hikari::backends {

namespace {

double monotonicSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr double kAheadSeconds = 0.25; // queued beyond what the device has taken
constexpr double kChunkSeconds = 0.05;

} // namespace

LineAudition::LineAudition(application::IndexedSourcePort &source, application::AudioOutputPort &output,
                           QObject *parent)
    : QObject(parent), m_source(source), m_output(output)
{
    m_timer.setInterval(5);
    connect(&m_timer, &QTimer::timeout, this, &LineAudition::pump);
}

bool LineAudition::play(std::int64_t start, std::int64_t count)
{
    if (!m_output.status().open || count <= 0)
        return false;
    if (m_active)
        stop();
    const auto format = m_output.format();
    m_output.stop(); // nothing queued from before
    const auto status = m_output.status();
    m_report = {};
    m_report.sourceStart = start;
    m_report.outputRate = format.sampleRate;
    m_report.outputChannels = format.channels;
    m_baseConsumed = status.framesConsumed;
    m_baseUnderruns = status.underruns;
    m_written = 0;
    m_pending.clear();
    m_requestInFlight = true;
    m_streamEnded = false;
    m_started = false;
    m_active = true;
    const std::uint64_t generation = ++m_generation;
    m_source.beginPcm(start, count, format.sampleRate, format.channels, [this, generation](auto stream) {
        if (generation != m_generation)
            return;
        m_requestInFlight = false;
        if (!stream) {
            m_active = false;
            m_timer.stop();
            emit failed(QStringLiteral("the source cannot stream this range"));
            return;
        }
        m_report.sourceCount = stream->count;
        m_report.outputFrames = stream->totalFrames;
        m_streamEnded = stream->totalFrames == 0;
        pump();
    });
    m_timer.start();
    return true;
}

void LineAudition::stop()
{
    if (!m_active)
        return;
    m_output.stop();
    if (m_report.logicalStopAt == 0)
        m_report.logicalStopAt = monotonicSeconds();
    finish(false);
}

void LineAudition::pump()
{
    if (!m_active)
        return;
    const auto status = m_output.status();
    if (status.deviceLost) {
        m_active = false;
        m_timer.stop();
        emit failed(QStringLiteral("the output device was lost"));
        return;
    }
    const auto channels = static_cast<std::size_t>(m_report.outputChannels);
    if (!m_pending.empty()) {
        const std::size_t accepted = m_output.write(m_pending);
        m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(accepted));
        m_written += static_cast<std::int64_t>(accepted / channels);
    }
    if (m_written > 0 && !m_started) {
        if (!m_output.start()) {
            m_active = false;
            m_timer.stop();
            emit failed(QStringLiteral("the output did not start"));
            return;
        }
        m_started = true;
        m_report.startedAt = monotonicSeconds();
    }
    const auto consumed = static_cast<std::int64_t>(status.framesConsumed - m_baseConsumed);
    const auto ahead = m_written - consumed;
    if (m_pending.empty() && !m_streamEnded && !m_requestInFlight &&
        ahead < static_cast<std::int64_t>(kAheadSeconds * m_report.outputRate)) {
        m_requestInFlight = true;
        const std::uint64_t generation = m_generation;
        m_source.nextPcm(static_cast<std::int64_t>(kChunkSeconds * m_report.outputRate),
                         [this, generation](auto chunk) {
                             if (generation != m_generation)
                                 return;
                             m_requestInFlight = false;
                             if (!chunk) {
                                 m_output.stop();
                                 m_active = false;
                                 m_timer.stop();
                                 emit failed(QStringLiteral("the source stopped streaming"));
                                 return;
                             }
                             m_pending.insert(m_pending.end(), chunk->samples.begin(), chunk->samples.end());
                             m_streamEnded = chunk->end;
                             pump();
                         });
    }
    if (m_streamEnded && m_pending.empty() && !m_requestInFlight && m_report.logicalStopAt == 0)
        m_report.logicalStopAt = monotonicSeconds();
    // Drained: the device took the last queued frame. The clock keeps where
    // that audio ends even though callbacks continue on silence.
    if (m_report.logicalStopAt != 0 && consumed >= m_written) {
        const auto clock = m_output.clock();
        if (clock.lastReadyFramesConsumed == m_baseConsumed + static_cast<std::uint64_t>(m_written)) {
            m_report.lastFrameConsumedAt = clock.lastReadyMonotonicSeconds;
            m_report.audibleEndAt =
                clock.lastReadyMonotonicSeconds + (clock.lastReadyEndDacSeconds - clock.lastReadyStreamSeconds);
            m_report.outputLatency = m_report.audibleEndAt - m_report.lastFrameConsumedAt;
        }
        finish(true);
    }
}

void LineAudition::finish(bool drained)
{
    m_timer.stop();
    m_active = false;
    ++m_generation; // late answers belong to nobody
    const auto status = m_output.status();
    m_report.underruns = status.underruns - m_baseUnderruns;
    m_report.drained = drained;
    if (drained)
        m_report.outputFrames = m_written;
    emit finished(m_report);
}

} // namespace hikari::backends
