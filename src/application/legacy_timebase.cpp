#include "hikari/application/legacy_timebase.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace hikari::application {

namespace {

constexpr int kCentisecondMargin = 5;

int saturate(double v)
{
    if (v >= INT_MAX)
        return INT_MAX;
    if (v <= INT_MIN)
        return INT_MIN;
    return static_cast<int>(v);
}

} // namespace

int LegacyTimebase::msAt(int frame) const
{
    if (frame < 0 || empty())
        return 0;
    if (m_timecodes.empty())
        return saturate(frame * static_cast<double>(frameDuration()));
    const int last = static_cast<int>(m_timecodes.size()) - 1;
    if (frame <= last)
        return m_timecodes[static_cast<std::size_t>(frame)];
    return saturate(m_timecodes[static_cast<std::size_t>(last)] + (frame - last) * static_cast<double>(frameDuration()));
}

int LegacyTimebase::frameAt(int ms) const
{
    if (ms <= 0 || empty())
        return 0;
    double estimate;
    if (!m_timecodes.empty()) {
        const auto it = std::lower_bound(m_timecodes.begin(), m_timecodes.end(), ms);
        if (it != m_timecodes.end())
            return static_cast<int>(it - m_timecodes.begin());
        const int last = static_cast<int>(m_timecodes.size()) - 1;
        estimate = last + std::ceil((ms - m_timecodes[static_cast<std::size_t>(last)]) / frameDuration());
    } else {
        estimate = std::ceil(ms / frameDuration());
    }
    int frame = saturate(estimate);
    while (frame < INT_MAX && msAt(frame) < ms)
        ++frame;
    while (frame > 0 && msAt(frame - 1) >= ms)
        --frame;
    return frame;
}

int LegacyTimebase::frameShownAt(int ms) const
{
    if (ms < 0)
        return 0;
    return std::max(frameAt(ms < INT_MAX ? ms + 1 : ms) - 1, 0);
}

int LegacyTimebase::clampFrame(int frame) const
{
    if (frame < 0)
        return 0;
    if (frameCount() > 0 && frame >= frameCount())
        return frameCount() - 1;
    return frame;
}

int LegacyTimebase::startTimeFor(int frame) const
{
    if (frame <= 0)
        return 0;
    const int frameTime = msAt(frame), prevTime = msAt(frame - 1);
    return std::min(prevTime + (frameTime - prevTime) / 2 + kCentisecondMargin, frameTime);
}

int LegacyTimebase::endTimeFor(int frame) const
{
    if (frame < 0)
        return 0;
    const int frameTime = msAt(frame), nextTime = msAt(frame + 1);
    return std::min(frameTime + (nextTime - frameTime) / 2 + kCentisecondMargin, nextTime);
}

} // namespace hikari::application
