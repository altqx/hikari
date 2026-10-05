#pragma once

// V6: timing and selecting Lines from the video, and how the video follows
// the active Line (legacy at 20d647c4):
// - GLOBAL_SET_START_TIME / GLOBAL_SET_END_TIME (HikariSubFrame.cpp:750-760,
//   VideoBox::GetFrameTime VideoBox.cpp:1688-1693, SubsGrid::SetStartTime /
//   SetEndTime SubsGridBase.cpp:1279-1309);
// - GLOBAL_SELECT_FROM_VIDEO (SubsGrid::SelVideoLine, SubsGridWindow.cpp:1982-2022);
// - GRID_SELECT_VISIBLE_LINES (SubsGrid::SelectVisible, SubsGridBase.cpp:1601-1625);
// - GLOBAL_SNAP_WITH_START / _END (HikariSubFrame::OnAudioSnap, HikariSubFrame.cpp:2528-2607);
// - the video toolbar's "Move video to selected line on:" (MOVE_VIDEO_TO_ACTIVE_LINE)
//   and "On moving to another line play:" (VIDEO_PLAY_AFTER_SELECTION) as
//   EditBox::SetLine (EditBox.cpp:449-492), the Grid's click
//   (SubsGridWindow.cpp:1596-1649 with SetVideoLineTime, 1374-1403) and
//   SubsGrid::ShowEditOnVideo (SubsGridBase.cpp:1165-1189) apply them.
// The video side is described by values (the shown time, the timebase, the
// state); what the video then has to do is returned, not done.

#include "hikari/application/audio_display.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/legacy_timebase.h"

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace hikari::application {

// Legacy VideoBox::GetFrameTime: the start (or end) time legacy gives the
// frame shown at `tellMs` (Timebase::FrameShownAt, then StartTimeFor /
// EndTimeFor, the midpoint representatives).
int legacyFrameTime(const LegacyTimebase &timebase, int tellMs, bool start);

// GLOBAL_SET_START_TIME / _END_TIME: the time inserted, ZEROIT(GetFrameTime +
// GRID_INSERT_START_OFFSET / GRID_INSERT_END_OFFSET), truncated towards zero.
int legacyInsertTimeFromVideo(const LegacyTimebase &timebase, int tellMs, bool start, int offsetMs);

// SubsGrid::SetStartTime / SetEndTime: every selected Line (hidden ones too)
// gets the time ("Setting start time" / "Setting end time", one step, also
// when nothing changes, as legacy's CopyDialogue records every selected
// Line); a start after the End moves the End there, an end before the Start
// moves the Start. SubsTime::NewTime clamps at 0; a MicroDVD Document's
// frames follow from its own rate (C01-fps-isolation). The pending draft is
// committed first, as its own step (legacy Send(EDITBOX_LINE_EDITION)).
// Refused (Invalid) without a selected Line.
std::expected<void, CommandRefusal> setTimesFromVideo(EditSession &session, bool end, int ms);

// The Lines the Grid shows (legacy isVisible); every Line when empty.
using ShownLine = std::function<bool(core::LineId)>;

// SubsGrid::SelVideoLine(-1): among shown, uncommented Lines with text (or a
// translation), the first whose [Start, End] holds `tellMs` (both ends
// included); else the one starting nearest to it, the later one only when it
// is strictly nearer, with `durationMs` (the video's) bounding the search
// forward. Legacy's fallbacks are Document row 0 (ip, idr start at 0).
std::optional<core::LineId> lineAtVideoTime(const core::Document &document, const ShownLine &shown, int tellMs,
                                            int durationMs);

// SubsGrid::SelectVisible: the Dialogue Lines (no Comment, no unparsed record)
// with Start - 5 <= tellMs < End - 5, in Document order; hidden Lines count
// only with "Ignore filtering in some actions" (`shown` empty). The active
// Line is read as the editor holds it (`active`, its draft applied). Legacy
// clears the selection first; nothing found leaves it empty and the active
// Line where it was.
std::vector<core::LineId> linesShownOnVideo(const core::Document &document, const ShownLine &shown,
                                            const std::optional<core::LineRecord> &active, int tellMs);

// HikariSubFrame::OnAudioSnap's search: the keyframe (each one's snap time
// is ZEROIT(StartTimeFor(FrameAt(keyframe)))) or, with
// AUDIO_INACTIVE_LINES_DISPLAY_MODE > 0, the other shown Line's boundary
// nearest to the active Line's start (or end), more than 0 and less than
// 5000 ms away, that stays before its end (after its start). Mode 1 reads the
// Lines from the previous shown Line up to, not including, the next one
// (legacy's loop stops before shadeTo, so the next Line never counts); modes
// 2 and 3 every Line. Nothing (no change) when none is found.
struct KeyframeSnapContext {
    std::span<const int> keyframesMs;    // Timebase::Keyframes
    std::span<const int> keyframeSnapMs; // StartTimeFor(FrameAt(keyframe)), before ZEROIT
    std::span<const AudioLineSpan> lines;
    int active = -1; // legacy currentLine
    int inactiveLinesMode = 1;
};
std::optional<int> legacyKeyframeSnap(const KeyframeSnapContext &context, int startMs, int endMs, bool snapStart);

// --- how the video follows the active Line --------------------------------

// MOVE_VIDEO_TO_ACTIVE_LINE's six choices (VideoToolbar.cpp:130-136).
enum class SeekAfter {
    DoubleClick = 0,        // "Double-clicking a line (always on)"
    EveryLineChange,        // "Every line change"
    ClickOrEditWhenPaused,  // "Clicking a line or editing when paused"
    ClickOrEdit,            // "Clicking a line or editing"
    EditWhenPaused,         // "Editing line when paused"
    Edit,                   // "Editing"
};
// VIDEO_PLAY_AFTER_SELECTION's four choices (VideoToolbar.cpp:132-133).
enum class PlayAfter {
    Nothing = 0,            // "Nothing"
    AudioToEnd,             // "Audio to the line end time"
    VideoToEnd,             // "Video and audio to the line end time"
    VideoToNextStart,       // "Video and audio to the next line start time"
};

// Legacy PlaybackState as these paths read it.
enum class VideoState { None, Playing, Paused, Stopped };

// What the video (and the audio box) has to do, in this order.
struct VideoFollow {
    bool playThenPause = false; // a Stopped video: Play(); Pause() (SetVideoLineTime)
    bool pause = false;
    std::optional<int> seekMs;  // VideoBox::Seek(time, seekStart)
    bool seekStart = true;      // false: the frame shown just before the time (an end)
    bool seekWhilePlaying = false; // the video keeps playing from there (ShowEditOnVideo)
    std::optional<std::pair<int, int>> playVideo; // PlayLine(start, end); PlayEndBefore applied by the video
    bool playAudio = false;     // AudioBox::OnPlaySelection
    std::optional<bool> audioUpdate; // AudioDisplay::Update(moveToEnd) after the Grid's seek
    bool operator==(const VideoFollow &) const = default;
};

// The active Line as the follow reads it (the editor's Line), and the next
// shown Line's start (GetKeyFromPosition(currentLine, 1): the Line itself on
// the last shown one).
struct FollowedLine {
    int startMs = 0, endMs = 0;
    int nextStartMs = 0;
};

// EditBox::SetLine's tail ("done:", EditBox.cpp:449-492). `rowChanged`: the
// active Line changed; `noChangeLine`: a Grid click (it seeks itself);
// `autoPlay`: a plain Grid click or SubsGrid::NextLine (Enter, the audio
// box's commit to the next Line). `audioBox`: an audio box is open.
VideoFollow followShownLine(SeekAfter seek, PlayAfter play, VideoState state, bool rowChanged, bool noChangeLine,
                            bool autoPlay, bool audioBox, const FollowedLine &line);

// The Grid's left press or double click on a Line (SubsGridWindow.cpp:1643-1649,
// SetVideoLineTime): a double click always seeks; a click on another Line
// (with GRID_CHANGE_ACTIVE_ON_SELECTION or without Ctrl) seeks for choices
// 1-3, unless the play-after choice plays the video. The End column seeks to
// the end (not in TMPlayer); Ctrl+double click a second earlier. The audio
// box then shows the Line's start (or end).
struct GridPress {
    bool doubleClick = false;
    bool ctrl = false;
    bool rowChanged = false; // legacy lastActiveLine != row
    bool changeActive = true; // GRID_CHANGE_ACTIVE_ON_SELECTION
    bool endColumn = false;
    bool tmPlayer = false;
    bool audioBox = false;    // an audio box is open (legacy edit->ABox)
};
VideoFollow followGridPress(SeekAfter seek, PlayAfter play, VideoState state, const GridPress &press,
                            const FollowedLine &line);

// SubsGrid::ShowEditOnVideo after a recorded change or Undo/Redo: choices 2-5
// seek to the active Line's start while paused, 3 and 5 also while playing.
// Nothing while a visual tool other than the crosshair is on (it refreshes itself).
VideoFollow followEdit(SeekAfter seek, VideoState state, bool visualTool, const FollowedLine &line);

} // namespace hikari::application
