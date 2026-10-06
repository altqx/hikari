// V5 (#184): fullscreen's legacy arithmetic at 20d647c4 — the monitor
// GetMonitorRect1 picks, the context menu's monitor entries, the panel's
// showing and hiding under the pointer (VideoBox::OnMouseEvent) and the
// progress bar RendererVideo::DrawProgressBar places in the window's corner.
#include "hikari/application/video_fullscreen.h"

#include <gtest/gtest.h>

using namespace hikari::application;

namespace {

// Two outputs side by side, the primary first (MonitorEnumProc1), and a
// third above the first.
const std::vector<MonitorRect> kMonitors{{0, 0, 1920, 1080}, {1920, 0, 1280, 1024}, {0, -900, 1600, 900}};

} // namespace

TEST(VideoFullscreenMonitor, ZeroTakesTheMonitorHoldingTheProgramsCentre)
{
    // F, a double click and VIDEO_FULL_SCREEN call SetFullscreen() with 0.
    EXPECT_EQ(fullscreenMonitor(0, kMonitors, {100, 100, 800, 600}), 0);
    EXPECT_EQ(fullscreenMonitor(0, kMonitors, {2000, 100, 800, 600}), 1);
    EXPECT_EQ(fullscreenMonitor(0, kMonitors, {100, -800, 800, 600}), 2);
    // The centre decides, not the corner: (1700 + 400, 100 + 300) is on the second.
    EXPECT_EQ(fullscreenMonitor(0, kMonitors, {1700, 100, 800, 600}), 1);
    // Right and bottom are exclusive: x 1920 is the second monitor's.
    EXPECT_EQ(fullscreenMonitor(0, kMonitors, {1919, 0, 2, 2}), 1);
    EXPECT_EQ(fullscreenMonitor(0, kMonitors, {1918, 0, 2, 2}), 0);
    // A centre off every monitor: the first.
    EXPECT_EQ(fullscreenMonitor(0, kMonitors, {5000, 5000, 100, 100}), 0);
}

TEST(VideoFullscreenMonitor, AnIndexTakesThatMonitor)
{
    // The context menu's MENU_MONITORS + i (VideoBox.cpp:1053-1056).
    EXPECT_EQ(fullscreenMonitor(1, kMonitors, {100, 100, 800, 600}), 1);
    EXPECT_EQ(fullscreenMonitor(2, kMonitors, {2000, 100, 800, 600}), 2);
    // -1, an index past the list, or one monitor: the first.
    EXPECT_EQ(fullscreenMonitor(-1, kMonitors, {2000, 100, 800, 600}), 0);
    EXPECT_EQ(fullscreenMonitor(3, kMonitors, {2000, 100, 800, 600}), 0);
    EXPECT_EQ(fullscreenMonitor(1, {{0, 0, 800, 600}}, {2000, 100, 800, 600}), 0);
    EXPECT_EQ(fullscreenMonitor(0, {}, {0, 0, 800, 600}), -1);
}

TEST(VideoFullscreenMonitor, OneMenuEntryPerMonitorAfterTheFirst)
{
    EXPECT_EQ(fullscreenMonitorEntries(0), 0);
    EXPECT_EQ(fullscreenMonitorEntries(1), 0);
    EXPECT_EQ(fullscreenMonitorEntries(2), 1);
    EXPECT_EQ(fullscreenMonitorEntries(3), 2);
}

TEST(VideoFullscreenPanel, ShowsBelowTheVideoAndHidesOverItUnlessPinned)
{
    // A 1080-high window with a 60-pixel panel: the video ends at 1020.
    using C = FullscreenPanelChange;
    EXPECT_EQ(fullscreenPanelOnPointer(false, false, 1019, 1080, 60), C::None);
    EXPECT_EQ(fullscreenPanelOnPointer(false, false, 1020, 1080, 60), C::Show);
    EXPECT_EQ(fullscreenPanelOnPointer(false, true, 1050, 1080, 60), C::None);
    EXPECT_EQ(fullscreenPanelOnPointer(false, true, 500, 1080, 60), C::Hide);
    // Pinned ("Show toolbar"): never hidden by the pointer.
    EXPECT_EQ(fullscreenPanelOnPointer(true, true, 500, 1080, 60), C::None);
    EXPECT_EQ(fullscreenPanelOnPointer(true, false, 1050, 1080, 60), C::Show);
}

TEST(VideoFullscreenProgress, TheTimesInTmpFormat)
{
    EXPECT_EQ(fullscreenProgressText(0, 2002), "00:00:00 / 00:00:02");
    EXPECT_EQ(fullscreenProgressText(61'999, 3'723'000), "00:01:01 / 01:02:03");
    // Hours are not wrapped (SubsTime::raw(TMP)).
    EXPECT_EQ(fullscreenProgressText(25 * 3'600'000, 100 * 3'600'000), "25:00:00 / 100:00:00");
}

TEST(VideoFullscreenProgress, TheBarInTheTopRightCorner)
{
    // A 1920-wide client, a 160x20 text: bar 12 high, margin 5.
    const auto bar = fullscreenProgressBar(1920, 160, 20, 500, 2000);
    EXPECT_EQ(bar.frameLeft, 1920 - 165);
    EXPECT_EQ(bar.frameTop, 5);
    EXPECT_EQ(bar.frameRight, 1915);
    EXPECT_EQ(bar.frameBottom, 17);
    EXPECT_EQ(bar.innerLeft, 1920 - 164);
    EXPECT_EQ(bar.innerTop, 6);
    EXPECT_EQ(bar.innerRight, 1914);
    EXPECT_EQ(bar.innerBottom, 16);
    EXPECT_EQ(bar.barWidth, 10);
    EXPECT_EQ(bar.barLeft, 1920 - 163);
    EXPECT_EQ(bar.barY, 6 + 5);
    // A quarter of the 156-pixel length.
    EXPECT_EQ(bar.barRight, 1757 + 39);
    // The text box below: 12 + 5 * 1.5 = 19.5, truncated.
    EXPECT_EQ(bar.textTop, 19);
    EXPECT_EQ(bar.textBottom, 39);
    EXPECT_EQ(bar.textLeft, 1755);
    EXPECT_EQ(bar.textRight, 1915);
    // No duration: the bar is full.
    EXPECT_EQ(fullscreenProgressBar(1920, 160, 20, 500, 0).barRight, 1757 + 156);
    // At the end: full.
    EXPECT_EQ(fullscreenProgressBar(1920, 160, 20, 2000, 2000).barRight, 1757 + 156);
}
