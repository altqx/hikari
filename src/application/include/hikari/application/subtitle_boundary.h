#pragma once

#include <cstdint>
#include <optional>

namespace hikari::application {

// The active Line's first and last shown frames.
struct LineFrames {
    std::int64_t first = 0;
    std::int64_t last = 0;
};

// Next and previous subtitle boundary (docs/ux/video-controls.md), in Aegisub 3.5's order: the
// active Line's boundary past the shown frame, or nullopt when there is none and the command
// moves to the next Line's first frame (the previous Line's last frame going back).
std::optional<std::int64_t> nextBoundaryInLine(LineFrames line, std::int64_t shown);
std::optional<std::int64_t> previousBoundaryInLine(LineFrames line, std::int64_t shown);

}
