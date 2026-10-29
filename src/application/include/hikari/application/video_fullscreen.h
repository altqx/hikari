#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Desktop coordinates.
struct MonitorRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool contains(int px, int py) const { return x <= px && px < x + width && y <= py && py < y + height; }
    friend bool operator==(const MonitorRect &, const MonitorRect &) = default;
};

// `monitors` primary first. `monitor` -1 (or one monitor): the first; 0: the one holding the centre
// of `program`, else the first; n: that one, the first past the list. Returns -1 without monitors.
int fullscreenMonitor(int monitor, const std::vector<MonitorRect> &monitors, const MonitorRect &program);

// Not on Wayland: a parent makes it an xdg_toplevel child and the compositor keeps the keyboard on
// the main window's output.
bool fullscreenOwnedByMainWindow(std::string_view platformName);

// One context menu entry per monitor after the first.
int fullscreenMonitorEntries(int monitors);

// video.fullscreenBar. Auto: shown on pointer movement, hidden after kFullscreenBarIdleMs without;
// Always: the picture ends above it.
enum class FullscreenBarMode { Auto = 0, Always = 1, Never = 2 };
inline constexpr int kFullscreenBarIdleMs = 1500;
// Out of range: Auto.
FullscreenBarMode fullscreenBarMode(std::int64_t setting);
FullscreenBarMode nextFullscreenBarMode(FullscreenBarMode mode);

// `awake`: the pointer moved less than kFullscreenBarIdleMs ago.
struct FullscreenBarState {
    bool awake = false;
    bool playing = false;
    bool menuOpen = false;
    bool pointerOverBar = false;
};
// Nothing keeps the bar up: Auto hides it and every mode hides the pointer.
bool fullscreenIdle(const FullscreenBarState &state);
bool fullscreenBarShown(FullscreenBarMode mode, const FullscreenBarState &state);

// One row without override blocks, drawings or malformed blocks; \N and \n as " ↵ ", \h and
// Unicode separators as a space, white space collapsed and trimmed. Reads at most
// kFullscreenLineInputBytes, gives at most kFullscreenLineLabelUnits UTF-16 units (pairs whole).
inline constexpr std::size_t kFullscreenLineInputBytes = 65536;
inline constexpr std::size_t kFullscreenLineLabelUnits = 1024;
std::string fullscreenLineText(std::string_view text);
// The first kFullscreenLineInputBytes, cut back to a whole UTF-8 character.
std::string_view fullscreenLineInput(std::string_view text);
// \r, \n, VT, FF, NEL, U+2028 and U+2029 as spaces.
std::string fullscreenOneRow(std::string_view text);

// "HH:MM:SS / HH:MM:SS".
std::string fullscreenProgressText(std::int64_t positionMs, std::int64_t durationMs);

// Client coordinates: a black frame, a white frame inside it, the bar along it and the times' text
// box below, sized from the text's extent in the bar's font.
struct FullscreenProgressBar {
    int frameLeft = 0, frameTop = 0, frameRight = 0, frameBottom = 0;
    int innerLeft = 0, innerTop = 0, innerRight = 0, innerBottom = 0;
    int barLeft = 0, barRight = 0, barY = 0, barWidth = 0;
    int textLeft = 0, textTop = 0, textRight = 0, textBottom = 0;
};
FullscreenProgressBar fullscreenProgressBar(int clientWidth, int textWidth, int textHeight, std::int64_t timeMs,
                                            std::int64_t durationMs);

}
