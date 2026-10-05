#include "hikari/application/video_fullscreen.h"

#include <cstdio>

namespace hikari::application {

int fullscreenMonitor(int monitor, const std::vector<MonitorRect> &monitors, const MonitorRect &program)
{
    if (monitors.empty())
        return -1;
    if (monitor == -1 || monitors.size() == 1)
        return 0;
    if (monitor == 0) {
        const int x = (program.width / 2) + program.x;
        const int y = (program.height / 2) + program.y;
        for (std::size_t i = 0; i < monitors.size(); ++i)
            if (monitors[i].contains(x, y))
                return static_cast<int>(i);
        return 0;
    }
    // The Linux branch checks the index (UtilsWindows.cpp:165-168); the
    // Windows one read past the list, which its menu never asked for.
    if (monitor > 0 && monitor < static_cast<int>(monitors.size()))
        return monitor;
    return 0;
}

int fullscreenMonitorEntries(int monitors)
{
    return monitors > 1 ? monitors - 1 : 0;
}

FullscreenPanelChange fullscreenPanelOnPointer(bool pinned, bool shown, int y, int clientHeight, int panelHeight)
{
    const bool onFullVideo = y < clientHeight - panelHeight;
    if (!onFullVideo && !shown)
        return FullscreenPanelChange::Show;
    if (onFullVideo && shown && !pinned)
        return FullscreenPanelChange::Hide;
    return FullscreenPanelChange::None;
}

namespace {

std::string tmpTime(std::int64_t ms)
{
    // SubsTime::raw(TMP): int arithmetic on mstime.
    const int mstime = static_cast<int>(ms);
    const int sec = mstime / 1000;
    const int min = mstime / 60000;
    const int hours = mstime / 3600000;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02i:%02i:%02i", hours, min % 60, sec % 60);
    return buffer;
}

} // namespace

std::string fullscreenProgressText(std::int64_t positionMs, std::int64_t durationMs)
{
    return tmpTime(positionMs) + " / " + tmpTime(durationMs);
}

FullscreenProgressBar fullscreenProgressBar(int clientWidth, int textWidth, int textHeight, std::int64_t timeMs,
                                            std::int64_t durationMs)
{
    const int w = clientWidth;
    const int fw = textWidth;
    const int fh = textHeight;
    const int progresbarHeight = static_cast<int>(fh * 0.6f);
    const int margin = static_cast<int>(fh * 0.25f);
    FullscreenProgressBar bar;
    // m_ProgressBarRect: LONG fields take the float sum truncated.
    bar.textTop = static_cast<int>(progresbarHeight + (margin * 1.5f));
    bar.textBottom = fh + bar.textTop;
    bar.textLeft = w - (fw + margin);
    bar.textRight = w - margin;
    // The black frame (vectors[0..4]).
    bar.frameLeft = w - (fw + margin);
    bar.frameTop = margin;
    bar.frameRight = w - margin;
    bar.frameBottom = progresbarHeight + margin;
    // The white frame inside it (vectors[5..9]).
    bar.innerLeft = w - (fw - 1 + margin);
    bar.innerTop = margin + 1;
    bar.innerRight = w - (margin + 1);
    bar.innerBottom = progresbarHeight - 1 + margin;
    // The bar: two pixels in from both sides.
    const int rw = w - (fw - 2 + margin);
    const int progBarLen = fw - 4;
    bar.barWidth = progresbarHeight - 2;
    bar.barLeft = rw;
    bar.barY = (margin + 1) + (bar.barWidth / 2);
    // GetDuration() and m_Time are ints there; the ratio is float.
    const int duration = static_cast<int>(durationMs);
    bar.barRight = duration > 0
                       ? static_cast<int>(((static_cast<float>(timeMs) / static_cast<float>(duration)) * progBarLen) + rw)
                       : progBarLen + rw;
    return bar;
}

} // namespace hikari::application
