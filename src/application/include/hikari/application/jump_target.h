#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace hikari::application {

// What Jump to's field and Quick Open's @ accept (docs/ux/video-controls.md#jump-to): a time ("0:03:40.50",
// "3:40", "220.5"), a frame ("5200"), a step in seconds ("+5s", "-1.5s", "+1:00") or in frames ("+12f").
// A bare number is a frame; one with a decimal point or a colon is a time.
struct JumpTarget {
    enum class Kind { Time, Frame, TimeStep, FrameStep };
    Kind kind = Kind::Time;
    std::int64_t value = 0; // milliseconds for Time and TimeStep, frames for Frame and FrameStep; steps are signed
};

// nullopt: not a time or frame. Minutes and seconds after a colon stay under 60.
std::optional<JumpTarget> parseJumpTarget(std::string_view text);

struct JumpClock {
    std::int64_t frameCount = 0;
    std::int64_t shownFrame = 0;
    std::function<std::int64_t(std::int64_t ms)> frameAt;
    std::function<std::int64_t(std::int64_t frame)> timeOf; // milliseconds
};

// The frame to seek to, clamped to the video's frames.
std::int64_t jumpFrame(const JumpTarget &target, const JumpClock &clock);

}
