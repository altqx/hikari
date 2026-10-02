#include "hikari/backends/audio_output_engine.h"

#include <algorithm>
#include <cmath>

namespace hikari::backends {

namespace {

constexpr auto kRelaxed = std::memory_order_relaxed;

std::size_t queueSamples(const application::OutputFormat &format, double seconds)
{
    const double frames = std::max(1.0, std::ceil(seconds * format.sampleRate));
    return static_cast<std::size_t>(frames) * static_cast<std::size_t>(format.channels);
}

} // namespace

OutputEngine::OutputEngine(application::OutputFormat format, double outputLatency, double queueSeconds)
    : m_format(format), m_outputLatency(outputLatency), m_ring(queueSamples(format, queueSeconds))
{
}

bool OutputEngine::render(std::span<float> out, const CallbackTiming &timing) noexcept
{
    if (m_injectLoss.load(kRelaxed)) {
        std::fill(out.begin(), out.end(), 0.0f);
        return false;
    }
    const auto channels = static_cast<std::size_t>(m_format.channels);
    const std::size_t frames = out.size() / channels;
    const std::size_t ready = m_ring.pop(out) / channels;
    const double rate = m_format.sampleRate;
    const double bufferSeconds = static_cast<double>(frames) / rate;

    // A stream clock that moved much further than the previous buffer lasted
    // means callbacks stopped (suspend, a stalled device): the old mapping
    // from queued frames to time no longer holds.
    bool epochBreak = false;
    if (m_hasPrevious) {
        const double expected = static_cast<double>(m_previousFrames) / rate;
        const double gap = timing.streamTime - m_previousStreamTime - expected;
        if (gap > std::max(0.25, 4 * expected)) {
            m_discontinuities.fetch_add(1, kRelaxed);
            epochBreak = true;
        }
    }
    if (timing.outputUnderflow || (m_flowing && ready < frames)) {
        m_underruns.fetch_add(1, kRelaxed);
        epochBreak = true;
    }
    if (epochBreak)
        newEpoch();
    m_flowing = frames > 0 && ready == frames;

    application::ClockEstimate e;
    e.valid = true;
    e.epoch = m_epoch.load(std::memory_order_acquire);
    e.framesConsumed = m_consumed;
    e.readyFrames = static_cast<std::uint32_t>(ready);
    e.bufferFrames = static_cast<std::uint32_t>(frames);
    e.streamTimeSeconds = timing.streamTime;
    e.monotonicSeconds = timing.monotonic;
    if (timing.dacTime > 0) {
        e.dacTimeSeconds = timing.dacTime;
        e.uncertaintySeconds = bufferSeconds;
    } else {
        e.dacTimeSeconds = timing.streamTime + m_outputLatency;
        e.dacTimeDerived = true;
        e.uncertaintySeconds = bufferSeconds + m_outputLatency;
    }
    publish(e);

    m_consumed += ready;
    m_hasPrevious = true;
    m_previousStreamTime = timing.streamTime;
    m_previousFrames = frames;
    if (bufferSeconds > m_longestBuffer) {
        m_longestBuffer = bufferSeconds;
        m_bufferSeconds.store(bufferSeconds, kRelaxed);
    }
    m_framesConsumed.store(m_consumed, kRelaxed);
    m_silenceFrames.fetch_add(frames - ready, kRelaxed);
    m_callbacks.fetch_add(1, std::memory_order_release);
    return true;
}

void OutputEngine::publish(const application::ClockEstimate &e) noexcept
{
    const std::uint64_t seq = m_seq.load(kRelaxed);
    m_seq.store(seq + 1, kRelaxed);
    std::atomic_thread_fence(std::memory_order_release);
    m_snapValid.store(e.valid, kRelaxed);
    m_snapEpoch.store(e.epoch, kRelaxed);
    m_snapFrames.store(e.framesConsumed, kRelaxed);
    m_snapReady.store(e.readyFrames, kRelaxed);
    m_snapBuffer.store(e.bufferFrames, kRelaxed);
    m_snapDac.store(e.dacTimeSeconds, kRelaxed);
    m_snapDerived.store(e.dacTimeDerived, kRelaxed);
    m_snapStream.store(e.streamTimeSeconds, kRelaxed);
    m_snapMonotonic.store(e.monotonicSeconds, kRelaxed);
    m_snapUncertainty.store(e.uncertaintySeconds, kRelaxed);
    m_seq.store(seq + 2, std::memory_order_release);
}

void OutputEngine::streamFinished() noexcept
{
    if (!m_stopRequested.load(std::memory_order_acquire))
        m_lost.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    newEpoch();
}

std::size_t OutputEngine::write(std::span<const float> interleaved)
{
    const auto channels = static_cast<std::size_t>(m_format.channels);
    const std::size_t n = std::min(m_ring.writable(), interleaved.size()) / channels * channels;
    return m_ring.push(interleaved.first(n));
}

void OutputEngine::started()
{
    m_flowing = false;
    m_hasPrevious = false;
    m_injectLoss.store(false, kRelaxed);
    m_stopRequested.store(false, std::memory_order_release);
    newEpoch();
    m_running.store(true, std::memory_order_release);
}

void OutputEngine::stopRequested()
{
    m_stopRequested.store(true, std::memory_order_release);
}

void OutputEngine::stopped()
{
    m_running.store(false, std::memory_order_release);
    newEpoch();
    m_ring.clear();
}

void OutputEngine::injectLoss()
{
    m_injectLoss.store(true, kRelaxed);
}

double OutputEngine::stallLimit() const
{
    return std::max(0.25, 8 * m_bufferSeconds.load(kRelaxed));
}

application::OutputStatus OutputEngine::status() const
{
    application::OutputStatus s;
    s.open = true;
    s.callbacks = m_callbacks.load(std::memory_order_acquire);
    s.running = m_running.load(std::memory_order_acquire);
    s.deviceLost = m_lost.load(std::memory_order_acquire);
    s.framesConsumed = m_framesConsumed.load(kRelaxed);
    s.silenceFrames = m_silenceFrames.load(kRelaxed);
    s.underruns = m_underruns.load(kRelaxed);
    s.discontinuities = m_discontinuities.load(kRelaxed);
    return s;
}

application::ClockEstimate OutputEngine::clock(double now) const
{
    application::ClockEstimate e;
    // Bounded: a reader that keeps losing to the callback reports no estimate.
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::uint64_t before = m_seq.load(std::memory_order_acquire);
        if (before & 1)
            continue;
        e.valid = m_snapValid.load(kRelaxed);
        e.epoch = m_snapEpoch.load(kRelaxed);
        e.framesConsumed = m_snapFrames.load(kRelaxed);
        e.readyFrames = m_snapReady.load(kRelaxed);
        e.bufferFrames = m_snapBuffer.load(kRelaxed);
        e.dacTimeSeconds = m_snapDac.load(kRelaxed);
        e.dacTimeDerived = m_snapDerived.load(kRelaxed);
        e.streamTimeSeconds = m_snapStream.load(kRelaxed);
        e.monotonicSeconds = m_snapMonotonic.load(kRelaxed);
        e.uncertaintySeconds = m_snapUncertainty.load(kRelaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (m_seq.load(kRelaxed) != before)
            continue;
        e.valid = e.valid && m_running.load(std::memory_order_acquire) && !m_lost.load(std::memory_order_acquire)
                  && e.epoch == m_epoch.load(std::memory_order_acquire) && now - e.monotonicSeconds <= stallLimit();
        return e;
    }
    return {};
}

} // namespace hikari::backends
