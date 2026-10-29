#pragma once

// Single-producer, single-consumer; the consumer side never allocates, locks or blocks.

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

    // Producer thread only; copies as many samples as fit and returns how many.
    std::size_t push(std::span<const float> samples);
    // Consumer thread only; returns the samples that were ready, the rest of `out` is silence.
    std::size_t pop(std::span<float> out);

    std::size_t readable() const;
    std::size_t writable() const { return capacity() - readable(); }
    std::size_t capacity() const { return m_mask + 1; }
    // Only while no callback runs.
    void clear();

private:
    std::vector<float> m_buffer;
    std::size_t m_mask;
    alignas(64) std::atomic<std::uint64_t> m_head{0};
    alignas(64) std::atomic<std::uint64_t> m_tail{0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
};

} // namespace hikari::backends
