#pragma once

// V5 (#184): video fullscreen as legacy VideoBox and its Fullscreen frame did
// at 20d647c4 (HikariSub/VideoBox.cpp, VideoFullscreen.cpp, UtilsWindows.cpp,
// RendererVideo.cpp).
//
// The arithmetic and decisions that do not need a window: which monitor
// fullscreen takes, the context menu's monitor entries, the fullscreen
// panel's showing and hiding under the pointer, and the progress bar drawn
// over the video in the window's top right corner.

#include <cstdint>
#include <string>
#include <vector>

namespace hikari::application {

// A monitor's rectangle in the desktop's coordinates (legacy RECT: right and
// bottom exclusive).
struct MonitorRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool contains(int px, int py) const { return x <= px && px < x + width && y <= py && py < y + height; }
    friend bool operator==(const MonitorRect &, const MonitorRect &) = default;
};

// GetMonitorRect1 (UtilsWindows.cpp:138-200): `monitors` with the primary
// first (MonitorEnumProc1 puts it there, UtilsWindows.cpp:66-71). -1 or a
// single monitor: the first; 0: the one holding the centre of the program's
// rectangle (the main window), else the first; n: that monitor, the first
// beyond the list. -1 when there is no monitor (legacy's 1920x1080 guess).
int fullscreenMonitor(int monitor, const std::vector<MonitorRect> &monitors, const MonitorRect &program);

// VideoBox::ContextMenu (VideoBox.cpp:941-948): one entry per monitor after
// the first, MENU_MONITORS + i, numbered i + 1 in the text: "Open in full
// screen on monitor %i" out of fullscreen, "Switch full screen to monitor
// %i" in it.
int fullscreenMonitorEntries(int monitors);

// VideoBox::OnMouseEvent in fullscreen (VideoBox.cpp:571-603): the panel
// (seek bar, buttons, times, toolbar) shows when the pointer goes below the
// video, `y` at or past `clientHeight - panelHeight`, and hides when it comes
// back over the video unless it is pinned ("Show toolbar",
// m_PanelOnFullscreen); hiding gives the video the keyboard (SetFocus).
enum class FullscreenPanelChange { None, Show, Hide };
FullscreenPanelChange fullscreenPanelOnPointer(bool pinned, bool shown, int y, int clientHeight, int panelHeight);

// VideoBox::RefreshTime (VideoBox.cpp:1366-1385): "<time> / <duration>",
// each SubsTime::raw(TMP) ("%02i:%02i:%02i", SubsTime.cpp:96-100).
std::string fullscreenProgressText(std::int64_t positionMs, std::int64_t durationMs);

// RendererVideo::DrawProgressBar (RendererVideo.cpp:1039-1095): the
// progress bar's rectangles in the fullscreen window's client, from the
// text's extent (fw, fh) in the bar's font: a black frame, a white frame
// inside it, the white bar along it (m_ProgressBarLineWidth thick, from `barLeft`
// to `barRight` at `barY`), and the times' text box below them.
struct FullscreenProgressBar {
    int frameLeft = 0, frameTop = 0, frameRight = 0, frameBottom = 0;  // vectors[0..4]
    int innerLeft = 0, innerTop = 0, innerRight = 0, innerBottom = 0;  // vectors[5..9]
    int barLeft = 0, barRight = 0, barY = 0, barWidth = 0;             // vectors[10..11]
    int textLeft = 0, textTop = 0, textRight = 0, textBottom = 0;      // m_ProgressBarRect
};
FullscreenProgressBar fullscreenProgressBar(int clientWidth, int textWidth, int textHeight, std::int64_t timeMs,
                                            std::int64_t durationMs);

} // namespace hikari::application
