// V5 (#184): fullscreen's legacy arithmetic at 20d647c4 — the monitor
// GetMonitorRect1 picks, the context menu's monitor entries and the
// progress bar RendererVideo::DrawProgressBar places in the window's corner;
// FS1 (#275): the bar's modes, when it hides, and the current Line's label.
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

// V5-wayland-parent: legacy's Fullscreen frame was the main window's child
// on every platform; on Wayland the rewrite's window has no parent.
TEST(VideoFullscreenWindow, OwnedByTheMainWindowExceptOnWayland)
{
    for (const char *platform : {"windows", "xcb", "offscreen", "minimal", "cocoa", "eglfs"})
        EXPECT_TRUE(fullscreenOwnedByMainWindow(platform)) << platform;
    for (const char *platform : {"wayland", "wayland-egl", "wayland-brcm"})
        EXPECT_FALSE(fullscreenOwnedByMainWindow(platform)) << platform;
}

// FS1-bar-mode: the setting's values, and the key's order.
TEST(VideoFullscreenBar, ModesFromTheSettingAndTheirCycle)
{
    EXPECT_EQ(fullscreenBarMode(0), FullscreenBarMode::Auto);
    EXPECT_EQ(fullscreenBarMode(1), FullscreenBarMode::Always);
    EXPECT_EQ(fullscreenBarMode(2), FullscreenBarMode::Never);
    EXPECT_EQ(fullscreenBarMode(-1), FullscreenBarMode::Auto);
    EXPECT_EQ(fullscreenBarMode(3), FullscreenBarMode::Auto);
    EXPECT_EQ(nextFullscreenBarMode(FullscreenBarMode::Auto), FullscreenBarMode::Always);
    EXPECT_EQ(nextFullscreenBarMode(FullscreenBarMode::Always), FullscreenBarMode::Never);
    EXPECT_EQ(nextFullscreenBarMode(FullscreenBarMode::Never), FullscreenBarMode::Auto);
    EXPECT_EQ(kFullscreenBarIdleMs, 1500);
}

// FS1-bar-auto-hide and FS1-cursor-hide: Auto hides the bar, and every mode
// the pointer, only while the video plays with nothing holding them.
TEST(VideoFullscreenBar, AutoHidesOnlyWhenNothingHoldsIt)
{
    const FullscreenBarState idle{false, true, false, false};
    EXPECT_TRUE(fullscreenIdle(idle));
    EXPECT_FALSE(fullscreenBarShown(FullscreenBarMode::Auto, idle));
    EXPECT_TRUE(fullscreenBarShown(FullscreenBarMode::Always, idle));
    EXPECT_FALSE(fullscreenBarShown(FullscreenBarMode::Never, idle));
    // A movement, a pause (or stop), an open menu, the pointer over the bar.
    for (const FullscreenBarState held : {FullscreenBarState{true, true, false, false},
                                          FullscreenBarState{false, false, false, false},
                                          FullscreenBarState{false, true, true, false},
                                          FullscreenBarState{false, true, false, true}}) {
        EXPECT_FALSE(fullscreenIdle(held));
        EXPECT_TRUE(fullscreenBarShown(FullscreenBarMode::Auto, held));
        EXPECT_TRUE(fullscreenBarShown(FullscreenBarMode::Always, held));
        EXPECT_FALSE(fullscreenBarShown(FullscreenBarMode::Never, held));
    }
}

// FS1-current-line: the label shows what the Line says, on one line.
TEST(VideoFullscreenBar, TheCurrentLinesTextOnOneLine)
{
    EXPECT_EQ(fullscreenLineText("We'll meet again"), "We'll meet again");
    EXPECT_EQ(fullscreenLineText("{\\an8\\pos(10,20)}Station{\\i1}, now{\\i0}"), "Station, now");
    EXPECT_EQ(fullscreenLineText("at the station,\\NI promise."), "at the station, ↵ I promise.");
    EXPECT_EQ(fullscreenLineText("one\\ntwo\\N\\Nthree"), "one ↵ two ↵ three");
    EXPECT_EQ(fullscreenLineText("a\\hb   c\td"), "a b c d");
    EXPECT_EQ(fullscreenLineText("  {\\b1}  trimmed  \\N"), "trimmed");
    EXPECT_EQ(fullscreenLineText("\\Nstarts"), "starts");
    // A drawing is not text; \p0 ends it.
    EXPECT_EQ(fullscreenLineText("{\\p1}m 0 0 l 10 0 10 10{\\p0}Sign"), "Sign");
    EXPECT_EQ(fullscreenLineText("{\\p2\\p0}Text"), "Text");
    EXPECT_EQ(fullscreenLineText("{\\pos(1,2)}Kept{\\p1}m 0 0"), "Kept");
    // Only a top-level \\pN draws: one inside \\t(...) is not read (core::project).
    EXPECT_EQ(fullscreenLineText("{\\t(0,1000,\\p1)}Visible text"), "Visible text");
    EXPECT_EQ(fullscreenLineText("{\\t(\\p1)\\p1}m 0 0{\\p0}After"), "After");
    // An unclosed block hides the rest; a block inside a block is not text.
    EXPECT_EQ(fullscreenLineText("Shown{\\i1 hidden"), "Shown");
    EXPECT_EQ(fullscreenLineText("a{b{\\i1}c"), "ac");
    EXPECT_EQ(fullscreenLineText("caf\u00e9 \u65e5\u672c"), "caf\u00e9 \u65e5\u672c");
    EXPECT_EQ(fullscreenLineText(""), "");
}

TEST(VideoFullscreenBar, UnicodeSeparatorsAreSpaces)
{
    // One row: a line or paragraph separator (or NEL, VT, FF) pasted into a
    // Line is a space, not a break that would hide the rest.
    EXPECT_EQ(fullscreenLineText("one\xE2\x80\xA8two\xE2\x80\xA9three\xC2\x85" "four\vfive\fsix"), "one two three four five six");
    EXPECT_EQ(fullscreenLineText("\xE2\x80\xA8  edge \xE2\x80\xA9"), "edge");
}

TEST(VideoFullscreenBar, TheLineLabelIsBounded)
{
    // A Line of megabytes: the label's length, not the Line's.
    EXPECT_EQ(fullscreenLineText(std::string(10'000'000, 'a')), std::string(kFullscreenLineLabelUnits, 'a'));
    // Only the first kFullscreenLineInputBytes are read: a block left open
    // there hides what follows, though it closes after them.
    EXPECT_EQ(fullscreenLineText("Before{" + std::string(kFullscreenLineInputBytes, 'x') + "}After"), "Before");
    // The cut is never inside a character, nor the label inside a pair.
    const std::string emoji = "\xF0\x9F\x98\x80"; // U+1F600, a surrogate pair
    std::string many = "a";
    for (int i = 0; i < 20'000; ++i)
        many += emoji;
    std::string expected = "a";
    for (std::size_t i = 0; i < (kFullscreenLineLabelUnits + 1) / 2; ++i)
        expected += emoji;
    EXPECT_EQ(fullscreenLineText(many), expected);
    // A hidden block, then é (two bytes each) with the cut 1001 bytes in:
    // 500 of them, no U+FFFD for the half one.
    std::string twoByte = "{" + std::string(kFullscreenLineInputBytes - 1003, 'x') + "}";
    std::string shown;
    for (int i = 0; i < 2000; ++i)
        twoByte += "\xC3\xA9";
    for (int i = 0; i < 500; ++i)
        shown += "\xC3\xA9";
    EXPECT_EQ(fullscreenLineText(twoByte), shown);
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
