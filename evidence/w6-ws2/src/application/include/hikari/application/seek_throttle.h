#pragma once

// V2 seek-bar drag (user decision 2026-10-14, compatibility-decisions.md
// V2-seek-bar-drag-throttle): while the seek bar is held and dragged, every
// player seeks at most once per interval (50 ms). The first move seeks at
// once; a move within the interval waits, and only the newest waiting frame
// is sought once the interval is up. The release seeks the frame it lands on
// unless that frame was sought within the last interval. Legacy's
// DirectShow player sought only on the release, its other players on every
// move (HikariSub/VideoSlider.cpp:143-206).
//
// Times are passed in, so the policy is tested without a clock; the Video
// panel's controller runs it on a timer.

#include <chrono>
#include <optional>

namespace hikari::application {

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
    // A frame is waiting for its seek.
    bool waiting() const { return m_waiting.has_value(); }

private:
    std::optional<int> send(int frame, Clock::time_point now);

    std::chrono::milliseconds m_interval;
    std::optional<Clock::time_point> m_sentAt; // the last seek of this drag
    std::optional<int> m_sent;                 // its frame
    std::optional<int> m_waiting;              // a newer frame not yet sought
    std::optional<int> m_last;                 // the last move's frame
};

} // namespace hikari::application
