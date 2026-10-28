#pragma once

// The real-time half of the editor audio output (N6), free of any audio API so
// its callback constraints are testable everywhere. render() runs on the
// device callback: it takes ready samples from the SPSC ring, fills silence,
// and publishes counters and a clock snapshot through atomics and a seqlock.
// It never allocates, locks, blocks, logs or calls Qt.

#include "hikari/application/audio_output.h"
#include "hikari/backends/audio_ring.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hikari::backends {

struct CallbackTiming {
    double dacTime = 0;        // 0: the host gave none
    double streamTime = 0;
    double monotonic = 0;      // steady_clock seconds when the callback began
    bool outputUnderflow = false;
};

class OutputEngine {
public:
    // outputLatency is the stream's reported latency, used when a callback has
    // no DAC time. queueSeconds sizes the ready queue.
    OutputEngine(application::OutputFormat format, double outputLatency, double queueSeconds);

    // Real-time side. Returns false when the stream should abort (fault
    // injection for device-loss evidence).
    bool render(std::span<float> out, const CallbackTiming &timing) noexcept;
    // Called when the stream ends, on whatever thread the host uses.
    void streamFinished() noexcept;

    // Owner side.
    std::size_t write(std::span<const float> interleaved);
    void started();          // a new epoch; callback state is reset
    void stopRequested();    // the next streamFinished() is expected
    void stopped();          // a new epoch; queued audio is discarded
    void injectLoss();       // the next callback aborts the stream

    application::OutputStatus status() const;
    // `now` is steady_clock seconds; a snapshot older than the stall limit is
    // invalid.
    application::ClockEstimate clock(double now) const;
    double stallLimit() const;

    const application::OutputFormat &format() const { return m_format; }

private:
    void publish(const application::ClockEstimate &estimate) noexcept;
    void newEpoch() noexcept { m_epoch.fetch_add(1, std::memory_order_acq_rel); }

    application::OutputFormat m_format;
    double m_outputLatency;
    AudioRing m_ring;

    // Callback-only state; reset by started() while no callback runs.
    bool m_flowing = false;
    bool m_hasPrevious = false;
    double m_previousStreamTime = 0;
    std::size_t m_previousFrames = 0;
    std::uint64_t m_consumed = 0;
    double m_longestBuffer = 0; // seconds, for the stall limit
    double m_readyEndDac = 0, m_readyStream = 0, m_readyMonotonic = 0;
    std::uint64_t m_readyConsumed = 0;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_lost{false};
    std::atomic<bool> m_injectLoss{false};
    std::atomic<std::uint64_t> m_epoch{0};
    std::atomic<std::uint64_t> m_callbacks{0};
    std::atomic<std::uint64_t> m_framesConsumed{0};
    std::atomic<std::uint64_t> m_silenceFrames{0};
    std::atomic<std::uint64_t> m_underruns{0};
    std::atomic<std::uint64_t> m_discontinuities{0};
    std::atomic<double> m_bufferSeconds{0};

    // Clock snapshot, written by the callback under a sequence counter.
    std::atomic<std::uint64_t> m_seq{0};
    std::atomic<bool> m_snapValid{false};
    std::atomic<std::uint64_t> m_snapEpoch{0};
    std::atomic<std::uint64_t> m_snapFrames{0};
    std::atomic<std::uint32_t> m_snapReady{0};
    std::atomic<std::uint32_t> m_snapBuffer{0};
    std::atomic<double> m_snapDac{0};
    std::atomic<bool> m_snapDerived{false};
    std::atomic<double> m_snapStream{0};
    std::atomic<double> m_snapMonotonic{0};
    std::atomic<double> m_snapUncertainty{0};
    std::atomic<double> m_snapReadyEndDac{0};
    std::atomic<double> m_snapReadyStream{0};
    std::atomic<double> m_snapReadyMonotonic{0};
    std::atomic<std::uint64_t> m_snapReadyConsumed{0};

    static_assert(std::atomic<double>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
};

} // namespace hikari::backends
