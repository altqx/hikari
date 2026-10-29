#pragma once

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
    bool byFrames = false;          // needs an exact timebase
    int timeMs = 0;
    int frames = 0;
    // Whether the active Line's start (true) or end moves to the video frame or audio mark.
    bool fromStartTime = false;
    bool moveToVideoTime = false;
    bool moveToAudioTime = false;
    bool tagTimes = false;          // keep \move, \t and \fad times on the frames
    // 0 all Lines, 1 selected, 2 from the selection on, 3 times at or after
    // the first selected Line's start, 4 at or before it, 5 by styles.
    int whichLines = 0;
    // 0 both times, 1 start only, 2 end only.
    int whichTimes = 0;
    // 0 none, 1 end times overlapping the next shifted Line end at its start,
    // 2 also ends set from the text length (timePerCharacter, at least 1 s).
    int correctEndTimes = 0;
    int timePerCharacter = 0;
    std::u8string styles;           // comma-separated, for whichLines 5
    // Bits: 1 lead-in, 2 lead-out, 4 continuous times, 8 snap to keyframes, 16
    // postprocessor panel shown. With 16 and any feature the run applies the
    // features instead of shifting (exact timebase only).
    int postprocessor = 0;
    int leadIn = 0, leadOut = 0;
    int thresholdStart = 0, thresholdEnd = 0;
    int keyframeBeforeStart = 0, keyframeAfterStart = 0, keyframeBeforeEnd = 0, keyframeAfterEnd = 0;
};

struct ShiftContext {
    const LegacyTimebase *timebase = nullptr; // null without video
    std::optional<int> videoFrame;
    std::optional<int> videoFrameStartMs, videoFrameEndMs;
    std::optional<int> audioMarkMs;
    std::vector<int> keyframes;               // frame numbers
};

enum class ShiftProblem {
    NoStylesChosen,
    NoLinesSelected,
    NoExactTimebase,
};

struct ShiftOutcome {
    // End correction was skipped for lack of an exact timebase.
    bool endCorrectionSkipped = false;
};

// One "Shifting times" step. Hidden Lines are skipped unless `visible` is empty.
std::expected<ShiftOutcome, std::variant<ShiftProblem, CommandRefusal>>
shiftTimes(EditSession &session, const ShiftTimesSettings &settings, const ShiftContext &context,
           const LineVisible &visible = {});

// One line per profile: "<name>: Time: <ms> Forward: <0|1> Frames: <0|1> MoveTagTimes: ... EndTimeCorrection: <n>".
// Values are read by position, so a style list with spaces shifts the later values.
std::string shiftProfileText(const std::string &name, const ShiftTimesSettings &settings);
std::string shiftProfileName(const std::string &profileText);
ShiftTimesSettings applyShiftProfile(const std::string &profileText, ShiftTimesSettings base);

namespace legacy {
// The first two times of \move (after its four coordinates), \t and \fad gain
// `start` and `end`, floored at 0.
std::u8string changeTagTimes(std::u8string_view text, int start, int end);
}

}
