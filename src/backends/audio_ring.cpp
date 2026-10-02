#include "hikari/backends/audio_ring.h"

#include <algorithm>
#include <bit>

namespace hikari::backends {

AudioRing::AudioRing(std::size_t capacitySamples)
    : m_buffer(std::bit_ceil(std::max<std::size_t>(capacitySamples, 2))), m_mask(m_buffer.size() - 1)
{
}

std::size_t AudioRing::readable() const
{
    return static_cast<std::size_t>(m_head.load(std::memory_order_acquire) - m_tail.load(std::memory_order_acquire));
}

std::size_t AudioRing::push(std::span<const float> samples)
{
    const std::uint64_t head = m_head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
    const std::size_t space = capacity() - static_cast<std::size_t>(head - tail);
    const std::size_t n = std::min(space, samples.size());
    for (std::size_t i = 0; i < n; ++i)
        m_buffer[(head + i) & m_mask] = samples[i];
    m_head.store(head + n, std::memory_order_release);
    return n;
}

std::size_t AudioRing::pop(std::span<float> out)
{
    const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    const std::size_t n = std::min(static_cast<std::size_t>(head - tail), out.size());
    for (std::size_t i = 0; i < n; ++i)
        out[i] = m_buffer[(tail + i) & m_mask];
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(n), out.end(), 0.0f);
    m_tail.store(tail + n, std::memory_order_release);
    return n;
}

void AudioRing::clear()
{
    m_tail.store(m_head.load(std::memory_order_acquire), std::memory_order_release);
}

} // namespace hikari::backends
