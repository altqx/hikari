#pragma once

// V5 (#184): video fullscreen as legacy VideoBox and its Fullscreen frame did
// at 20d647c4 (HikariSub/VideoBox.cpp, VideoFullscreen.cpp, UtilsWindows.cpp,
// RendererVideo.cpp).
//
// The arithmetic and decisions that do not need a window: which monitor
// fullscreen takes, the context menu's monitor entries, the fullscreen
// bar's modes and when it shows (FS1), the current Line's label, and the
// progress bar drawn over the video in the window's top right corner.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

// Whether the fullscreen window is owned by the main window on the Qt
// platform `platformName` (V5-wayland-parent): legacy's Fullscreen frame
// always was (wxFrame(parent, ...)); on Wayland ("wayland", "wayland-egl",
// ...) a parent makes the window the main window's dialog (xdg_toplevel
// set_parent) and the compositor keeps the keyboard on the main window's
// output, so it stands alone there.
bool fullscreenOwnedByMainWindow(std::string_view platformName);

// VideoBox::ContextMenu (VideoBox.cpp:941-948): one entry per monitor after
// the first, MENU_MONITORS + i, numbered i + 1 in the text: "Open in full
// screen on monitor %i" out of fullscreen, "Switch full screen to monitor
// %i" in it.
int fullscreenMonitorEntries(int monitors);

// FS1 (#275): the fullscreen bar's mode, the setting video.fullscreenBar
// (FS1-bar-mode; legacy had "Show toolbar", m_PanelOnFullscreen, and showed
// its panel when the pointer went below the video, VideoBox.cpp:571-603).
// Auto shows the bar on pointer movement and hides it after
// kFullscreenBarIdleMs without (FS1-bar-auto-hide); Always keeps it and the
// picture ends above it (legacy's "Show toolbar" on); Never shows the
// picture only. The Video hotkey VIDEO_FULLSCREEN_BAR cycles them in order.
enum class FullscreenBarMode { Auto = 0, Always = 1, Never = 2 };
inline constexpr int kFullscreenBarIdleMs = 1500;
// The setting's value as a mode: one out of range is Auto (the default).
FullscreenBarMode fullscreenBarMode(std::int64_t setting);
FullscreenBarMode nextFullscreenBarMode(FullscreenBarMode mode);

// What keeps the bar up: a pointer movement less than kFullscreenBarIdleMs
// ago (`awake`), the video not playing (paused or stopped), a menu of the
// fullscreen window open, and the pointer over the bar.
struct FullscreenBarState {
    bool awake = false;
    bool playing = false;
    bool menuOpen = false;
    bool pointerOverBar = false;
};
// Nothing keeps the bar up: in Auto it hides, and in every mode the pointer
// hides over the picture (FS1-cursor-hide; legacy hid it after a second
// without moving while the panel was hidden, VideoBox::OnIdle).
bool fullscreenIdle(const FullscreenBarState &state);
bool fullscreenBarShown(FullscreenBarMode mode, const FullscreenBarState &state);

// FS1-current-line: the bar's read-only label of the current Line's text,
// on one line, read as core::project reads it: override blocks ({...}),
// drawings (a top-level \p1 and up, to \p0) and malformed blocks left out,
// \N and \n shown as " ↵ ", \h and Unicode line and paragraph separators as
// a space, runs of white space as one, trimmed. A label of one elided row: it reads the first
// kFullscreenLineInputBytes of the text at most (to a whole character) and
// gives kFullscreenLineLabelUnits UTF-16 units at most (a pair kept whole).
inline constexpr std::size_t kFullscreenLineInputBytes = 65536;
inline constexpr std::size_t kFullscreenLineLabelUnits = 1024;
std::string fullscreenLineText(std::string_view text);
// The part of a Line's text the label reads: its first
// kFullscreenLineInputBytes, cut back to a whole UTF-8 character (to take
// before any copy of a Line of megabytes).
std::string_view fullscreenLineInput(std::string_view text);
// FS1-file-name-strip: a text on one row, its line breaks and line and
// paragraph separators (\r, \n, VT, FF, NEL, U+2028, U+2029) as spaces.
std::string fullscreenOneRow(std::string_view text);

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
