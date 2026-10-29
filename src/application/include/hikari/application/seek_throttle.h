#pragma once

#include <chrono>
#include <optional>

namespace hikari::application {

// Seeks at most once per interval while dragging; only the newest waiting frame is sought.
class SeekThrottle {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kInterval{50};

    explicit SeekThrottle(std::chrono::milliseconds interval = kInterval) : m_interval(interval) {}

    // The held bar moved to `frame` at `now`: the frame to seek now, if any.
    std::optional<int> move(int frame, Clock::time_point now);
    // When the waiting frame is due; nullopt when none waits.
    std::optional<Clock::time_point> due() const;
    // The waiting frame once due at `now`: the frame to seek now, if any.
    std::optional<int> tick(Clock::time_point now);
    // The bar was let go at `now`: the frame to seek now (the last move's),
    // if any. The drag is over; the next move starts a new one.
    std::optional<int> release(Clock::time_point now);
    bool waiting() const { return m_waiting.has_value(); }

private:
    std::optional<int> send(int frame, Clock::time_point now);

    std::chrono::milliseconds m_interval;
    std::optional<Clock::time_point> m_sentAt;
    std::optional<int> m_sent;
    std::optional<int> m_waiting;
    std::optional<int> m_last;
};

} // namespace hikari::application
