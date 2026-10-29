#pragma once

// The video side comes in as values; what the video has to do is returned, not done.

#include "hikari/application/audio_display.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/legacy_timebase.h"

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace hikari::application {

// The start (or end) time of the frame shown at `tellMs`, its midpoint
// representative.
int legacyFrameTime(const LegacyTimebase &timebase, int tellMs, bool start);

// The frame time plus `offsetMs`, truncated towards zero to a multiple of 10 ms.
int legacyInsertTimeFromVideo(const LegacyTimebase &timebase, int tellMs, bool start, int offsetMs);

// Every selected Line (hidden ones too) gets the time in one step; a start after
// the End moves the End, an end before the Start moves the Start. No change
// records nothing and still succeeds. Times clamp at 0; a MicroDVD Document's
// frames follow its own rate. The pending draft is committed first, as its own
// step. Refused (Invalid) without a selected Line.
std::expected<void, CommandRefusal> setTimesFromVideo(EditSession &session, bool end, int ms);

// The Lines the Grid shows; every Line when empty.
using ShownLine = std::function<bool(core::LineId)>;

// Among shown, uncommented Lines with text (or a translation): the first whose
// [Start, End] holds `tellMs`; else the one starting nearest, the later only when
// strictly nearer, `durationMs` bounding the search forward; else the first
// shown Line. nullopt when no Line is shown.
std::optional<core::LineId> lineAtVideoTime(const core::Document &document, const ShownLine &shown, int tellMs,
                                            int durationMs);

// Dialogue Lines with Start - 5 <= tellMs < End - 5, in Document order; hidden
// Lines count only when `shown` is empty. `active` is the editor's Line, its
// draft applied.
std::vector<core::LineId> linesShownOnVideo(const core::Document &document, const ShownLine &shown,
                                            const std::optional<core::LineRecord> &active, int tellMs);

// The keyframe or, with inactiveLinesMode > 0, the other shown Line's boundary
// nearest to the active Line's start (or end), more than 0 and less than 5000 ms
// away, staying before its end (after its start). Mode 1 reads from the previous
// shown Line to the next, both included; modes 2 and 3 every Line. nullopt when
// none is found.
struct KeyframeSnapContext {
    std::span<const int> keyframesMs;
    std::span<const int> keyframeSnapMs; // each keyframe's start time, before rounding to 10 ms
    std::span<const AudioLineSpan> lines;
    int active = -1;
    int inactiveLinesMode = 1;
};
std::optional<int> legacyKeyframeSnap(const KeyframeSnapContext &context, int startMs, int endMs, bool snapStart);

// MOVE_VIDEO_TO_ACTIVE_LINE's choices, by value.
enum class SeekAfter {
    DoubleClick = 0,
    EveryLineChange,
    ClickOrEditWhenPaused,
    ClickOrEdit,
    EditWhenPaused,
    Edit,
};
// VIDEO_PLAY_AFTER_SELECTION's choices, by value.
enum class PlayAfter {
    Nothing = 0,
    AudioToEnd,
    VideoToEnd,
    VideoToNextStart,
};

enum class VideoState { None, Playing, Paused, Stopped };

// What the video (and the audio box) has to do, in this order.
struct VideoFollow {
    bool playThenPause = false; // a Stopped video: Play() then Pause()
    bool pause = false;
    std::optional<int> seekMs;
    bool seekStart = true;      // false: the frame shown just before the time (an end)
    bool seekWhilePlaying = false; // the video keeps playing from there
    std::optional<std::pair<int, int>> playVideo;
    bool playAudio = false;
    std::optional<bool> audioUpdate; // the audio display's update(moveToEnd) after the Grid's seek
    bool operator==(const VideoFollow &) const = default;
};

// The editor's Line, and the next shown Line's start (the Line's own on the
// last shown one).
struct FollowedLine {
    int startMs = 0, endMs = 0;
    int nextStartMs = 0;
};

// `rowChanged`: the active Line changed; `noChangeLine`: a Grid click (it seeks
// itself); `autoPlay`: a plain Grid click or moving to the next Line (Enter, the
// audio box's commit). `audioBox`: an audio box is open.
VideoFollow followShownLine(SeekAfter seek, PlayAfter play, VideoState state, bool rowChanged, bool noChangeLine,
                            bool autoPlay, bool audioBox, const FollowedLine &line);

// A double click always seeks; a click on another Line (with changeActive or
// without Ctrl) seeks for choices 1-3 unless the play-after choice plays the
// video. The End column seeks to the end (not in TMPlayer); Ctrl+double click a
// second earlier. The audio box then shows the Line's start (or end).
struct GridPress {
    bool doubleClick = false;
    bool ctrl = false;
    bool rowChanged = false;
    bool changeActive = true;
    bool endColumn = false;
    bool tmPlayer = false;
    bool audioBox = false; // an audio box is open
};
VideoFollow followGridPress(SeekAfter seek, PlayAfter play, VideoState state, const GridPress &press,
                            const FollowedLine &line);

// After a recorded change or Undo/Redo: choices 2-5 seek to the active Line's
// start while paused, 3 and 5 also while playing. Nothing while a visual tool
// other than the crosshair is on.
VideoFollow followEdit(SeekAfter seek, VideoState state, bool visualTool, const FollowedLine &line);

} // namespace hikari::application
