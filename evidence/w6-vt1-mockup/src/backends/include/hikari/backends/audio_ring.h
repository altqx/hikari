#pragma once

// Single-producer, single-consumer sample ring for the real-time audio
// callback (N6). The consumer side never allocates, locks or blocks: it takes
// what is ready and fills the rest with silence.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hikari::backends {

class AudioRing {
public:
    // Capacity in samples (all channels); rounded up to a power of two.
    explicit AudioRing(std::size_t capacitySamples);

    // Producer (control thread): copies as many samples as fit.
    std::size_t push(std::span<const float> samples);
    // Consumer (callback): fills `out`; returns the samples that were ready.
    // The remainder of `out` is set to silence.
    std::size_t pop(std::span<float> out);

    std::size_t readable() const;
    std::size_t writable() const { return capacity() - readable(); }
    std::size_t capacity() const { return m_mask + 1; }
    void clear(); // only while no callback runs

private:
    std::vector<float> m_buffer;
    std::size_t m_mask;
    alignas(64) std::atomic<std::uint64_t> m_head{0}; // written by the producer
    alignas(64) std::atomic<std::uint64_t> m_tail{0}; // written by the consumer
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
};

} // namespace hikari::backends
