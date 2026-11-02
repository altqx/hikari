#pragma once

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

    std::int64_t frameFromMs(std::int64_t ms) const;
    std::int64_t msFromFrame(std::int64_t frame) const;

private:
    explicit AegisubFrameTimes(std::vector<std::int64_t> startsMs);
    std::int64_t frameAt(std::int64_t ms) const;
    std::int64_t timeAt(std::int64_t frame) const;

    std::vector<std::int64_t> m_starts;
    std::int64_t m_numerator = 0;
    std::int64_t m_last = 0;
};

}
