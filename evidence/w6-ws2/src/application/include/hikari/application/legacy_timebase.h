#pragma once

// Legacy Timebase (Timebase.cpp at 20d647c4) over an indexed video's frame
// starts in whole milliseconds, for commands whose results legacy computes
// from it: MsAt, FrameAt (the frame at or after a time), FrameShownAt, and
// the midpoint StartTimeFor/EndTimeFor that legacy gives Lines timed to frames.

#include <cstdint>
#include <vector>

namespace hikari::application {

class LegacyTimebase {
public:
    LegacyTimebase() = default;
    LegacyTimebase(std::vector<int> timecodesMs, double fps) : m_timecodes(std::move(timecodesMs)), m_fps(static_cast<float>(fps)) {}

    bool empty() const { return m_timecodes.empty() && m_fps <= 0.f; }
    bool exact() const { return !m_timecodes.empty(); }
    int frameCount() const { return static_cast<int>(m_timecodes.size()); }
    float frameDuration() const { return m_fps > 0.f ? 1000.f / m_fps : 0.f; }
    int msAt(int frame) const;
    int frameAt(int ms) const;
    int frameShownAt(int ms) const;
    int clampFrame(int frame) const;
    int startTimeFor(int frame) const;
    int endTimeFor(int frame) const;

private:
    std::vector<int> m_timecodes;
    float m_fps = 0.f;
};

} // namespace hikari::application
