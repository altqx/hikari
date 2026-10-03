#pragma once

// F5: shift times (legacy GLOBAL_SHIFT_TIMES / the ShiftTimes panel and
// SubsGrid::ChangeTimes at 20d647c4), without the postprocessor (F6).

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/legacy_timebase.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hikari::application {

struct ShiftTimesSettings {
    bool forward = true;
    bool byFrames = false;          // the panel shows frames (needs an exact timebase)
    int timeMs = 0;                 // legacy SHIFT_TIMES_TIME
    int frames = 0;                 // legacy SHIFT_TIMES_DISPLAY_FRAMES
    // Move to video/audio time: the active Line's start (true) or end time
    // goes to the video frame's time or the audio mark.
    bool fromStartTime = false;
    bool moveToVideoTime = false;
    bool moveToAudioTime = false;
    bool tagTimes = false;          // keep \move, \t and \fad times on the frames
    // 0 all Lines, 1 selected, 2 from the selection on, 3 times at or after
    // the first selected Line's start, 4 at or before it, 5 by styles.
    int whichLines = 0;
    // 0 both times, 1 start only, 2 end only (the UI confirms 1 and 2).
    int whichTimes = 0;
    // 0 none, 1 end times overlapping the next shifted Line end at its start,
    // 2 also ends set from the text length (timePerCharacter, at least 1 s).
    int correctEndTimes = 0;
    int timePerCharacter = 0;
    std::u8string styles;           // comma-separated, for whichLines 5
    // F6, the postprocessor (legacy POSTPROCESSOR_*): 1 lead-in, 2 lead-out,
    // 4 continuous times, 8 snap to keyframes, 16 the postprocessor is the
    // panel shown. With 16 and any feature the run does no shift: it applies
    // the features to the chosen Lines (on an exact timebase only).
    int postprocessor = 0;
    int leadIn = 0, leadOut = 0;
    int thresholdStart = 0, thresholdEnd = 0;
    int keyframeBeforeStart = 0, keyframeAfterStart = 0, keyframeBeforeEnd = 0, keyframeAfterEnd = 0;
};

// What the video and audio offer the shift.
struct ShiftContext {
    const LegacyTimebase *timebase = nullptr; // the open video's, or null
    std::optional<int> videoFrame;            // the shown frame
    std::optional<int> videoFrameStartMs, videoFrameEndMs;
    std::optional<int> audioMarkMs;
    std::vector<int> keyframes;               // keyframe frame numbers of the video
};

enum class ShiftProblem {
    NoStylesChosen,     // "No styles selected for time shifting"
    NoLinesSelected,    // "No lines selected for shifting"
    NoExactTimebase,    // "Video was not loaded using FFMS2" (frames)
};

struct ShiftOutcome {
    // End correction needs an exact timebase; legacy logs this and skips it.
    bool endCorrectionSkipped = false;
};

// One "Shifting times" step. Hidden Lines are skipped unless `visible` is empty.
std::expected<ShiftOutcome, std::variant<ShiftProblem, CommandRefusal>>
shiftTimes(EditSession &session, const ShiftTimesSettings &settings, const ShiftContext &context,
           const LineVisible &visible = {});

// Shift profiles (legacy SHIFT_TIMES_PROFILES): one line per profile,
// "<name>: Time: <ms> Forward: <0|1> Frames: <0|1> MoveTagTimes: ... EndTimeCorrection: <n>".
// Reading takes the values by position and skips the labels, so a style list
// with spaces in it shifts the later values (kept from legacy).
std::string shiftProfileText(const std::string &name, const ShiftTimesSettings &settings);
std::string shiftProfileName(const std::string &profileText);
ShiftTimesSettings applyShiftProfile(const std::string &profileText, ShiftTimesSettings base);

namespace legacy {
// Dialogue::ChangeTimes: the first two times of \move (after its four
// coordinates), \t and \fad gain `start` and `end`, floored at 0.
std::u8string changeTagTimes(std::u8string_view text, int start, int end);
} // namespace legacy

} // namespace hikari::application
