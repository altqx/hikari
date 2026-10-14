#include "hikari/application/seek_throttle.h"

#include <utility>

namespace hikari::application {

std::optional<int> SeekThrottle::send(int frame, Clock::time_point now)
{
    m_sent = frame;
    m_sentAt = now;
    m_waiting.reset();
    return frame;
}

std::optional<int> SeekThrottle::move(int frame, Clock::time_point now)
{
    m_last = frame;
    if (m_sent == frame) {
        m_waiting.reset(); // back on the frame sought last: nothing to seek
        return std::nullopt;
    }
    if (!m_sentAt || now - *m_sentAt >= m_interval)
        return send(frame, now);
    m_waiting = frame;
    return std::nullopt;
}

std::optional<SeekThrottle::Clock::time_point> SeekThrottle::due() const
{
    if (!m_waiting || !m_sentAt)
        return std::nullopt;
    return *m_sentAt + m_interval;
}

std::optional<int> SeekThrottle::tick(Clock::time_point now)
{
    const auto at = due();
    if (!at || now < *at)
        return std::nullopt;
    return send(*m_waiting, now);
}

std::optional<int> SeekThrottle::release(Clock::time_point now)
{
    const auto last = std::exchange(m_last, std::nullopt);
    const bool current = last && m_sent == last && m_sentAt && now - *m_sentAt < m_interval;
    m_sent.reset();
    m_sentAt.reset();
    m_waiting.reset();
    if (!last || current)
        return std::nullopt;
    return last;
}

} // namespace hikari::application
