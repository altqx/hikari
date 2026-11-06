#pragma once

#include "hikari/application/timecodes_file.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace hikari::application {

// A video's frame times as Aegisub's Lua API rounds them: frameFromMs and msFromFrame are
// aegisub.frame_from_ms and aegisub.ms_from_frame, extrapolating past the last frame.
class AegisubFrameTimes {
public:
    // nullopt for fewer than two frames, which Aegisub cannot time.
    static std::optional<AegisubFrameTimes> fromFrameStarts(std::vector<std::int64_t> startsMs);
    static AegisubFrameTimes fromTimecodes(const Timecodes &timecodes);

    std::int64_t frameFromMs(std::int64_t ms) const;
    std::int64_t msFromFrame(std::int64_t frame) const;
    // The frame's own start, extrapolated past the held frames.
    std::int64_t startOf(std::int64_t frame) const;
    std::int64_t heldFrames() const { return static_cast<std::int64_t>(m_starts.size()); }
    double fps() const;

private:
    AegisubFrameTimes(std::vector<std::int64_t> startsMs, std::int64_t numerator, std::int64_t last);
    std::int64_t frameAt(std::int64_t ms) const;

    std::vector<std::int64_t> m_starts;
    std::int64_t m_numerator = 0;
    std::int64_t m_last = 0;
};

}
