#pragma once

// The audio box's timing (A3; legacy HikariSub/AudioDisplay.cpp and
// AudioBox.cpp at 20d647c4): the selection the display holds and its mouse
// handling (OnMouseEvent without karaoke: dragging the start, end and mark,
// timing from nothing, the ruler's drag and right-click mark), boundary
// snapping (GetBoundarySnap), lead-in and lead-out (AddLead), and the commit
// (CommitChanges with EditBox::Send(AUDIO_CHANGE_TIME) and SubsGrid::NextLine)
// as one history step. Playback (A4) is not part of it: where legacy plays or
// moves the player's end, the result says so and the box leaves it to
// playback. A5: in karaoke mode the mouse also grabs, drags, joins and splits
// syllables (audio_karaoke), in legacy's own order of hit tests.

#include "hikari/application/audio_display.h"
#include "hikari/application/audio_karaoke.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace hikari::application {

// Legacy options the timing reads, with their defaults.
struct AudioTimingOptions {
    bool autoCommit = true;               // AUDIO_AUTO_COMMIT
    bool snapToKeyframes = false;         // AUDIO_SNAP_TO_KEYFRAMES
    bool snapToOtherLines = false;        // AUDIO_SNAP_TO_OTHER_LINES
    int startDragSensitivity = 6;         // AUDIO_START_DRAG_SENSITIVITY
    int leadIn = 200;                     // AUDIO_LEAD_IN_VALUE
    int leadOut = 300;                    // AUDIO_LEAD_OUT_VALUE
    bool dontPlayWhenLineChanges = false; // AUDIO_DONT_PLAY_WHEN_LINE_CHANGES
};

// Legacy ZEROIT: down to 10 ms, truncated towards zero.
constexpr int legacyZeroIt(int ms) { return (ms / 10) * 10; }

// Legacy SubsGrid::GetKeyFromPosition(position, delta, false): the shown Line
// `delta` shown Lines away, or -1 when there is none; position itself for
// delta 0 or (going back) position 0.
int legacyKeyFromPositionOrNone(std::span<const AudioLineSpan> lines, int position, int delta);

// What GetBoundarySnap reads besides the time.
struct AudioSnapContext {
    const AudioView *view = nullptr;
    bool snapToKeyframes = false;  // AUDIO_SNAP_TO_KEYFRAMES
    bool snapToOtherLines = false; // AUDIO_SNAP_TO_OTHER_LINES
    bool drawKeyframes = true;     // AUDIO_DRAW_KEYFRAMES: only drawn keyframes snap
    int inactiveLines = 1;         // AUDIO_INACTIVE_LINES_DISPLAY_MODE (legacy shadeType)
    // Timebase::Keyframes, and each one's snap time: the start time of the
    // frame at it (StartTimeFor(FrameAt)), or 21 ms before it without a video.
    std::span<const int> keyframesMs;
    std::span<const int> keyframeSnapMs;
    std::span<const AudioLineSpan> lines;
    int active = -1; // legacy line_n
};
// Legacy GetBoundarySnap: the keyframe or other Line boundary in view nearest
// to `ms` within `rangeX` columns (Shift inverts both options; `otherLines`
// false leaves the Lines out; `keysnap` skips boundaries under 10 ms away),
// else `ms`; either way down to 10 ms (ZEROIT).
int legacyBoundarySnap(const AudioSnapContext &context, int ms, int rangeX, bool shiftHeld, bool keysnap,
                       bool otherLines);

// Legacy AddLead: the start moves back by the lead-in (not before 0), the end
// on by the lead-out.
std::pair<int, int> legacyAddLead(int startMs, int endMs, bool in, bool out, int leadIn, int leadOut);

// One mouse event as legacy's wxMouseEvent described it.
struct AudioMouse {
    enum class Type { Move, Press, Release, DoubleClick };
    enum class Button { None, Left, Right, Middle };
    Type type = Type::Move;
    Button button = Button::None; // the button pressed or released
    int x = 0, y = 0;
    bool leftHeld = false, rightHeld = false, middleHeld = false; // after the event (LeftIsDown)
    bool shift = false, ctrl = false, alt = false;
};

// The Line the Alt release moves (legacy CopyDialogueWithOffset): the next
// shown Line starts at the end, the previous one ends at the start.
enum class AudioAdjacent { None, Previous, Next };

// What a mouse event asks of the box.
struct AudioMouseResult {
    bool redraw = false;
    bool focus = false;             // any button: SetFocus
    std::optional<bool> sizeCursor; // the cursor becomes wxCURSOR_SIZEWE (true) or the default (false)
    std::optional<int> seekVideoMs; // Ctrl+left or middle click: the video seeks there
    // The button was released over a boundary or after timing (Commit; true
    // for the end: moveToEnd), with the Alt release's Line.
    std::optional<bool> commit;
    AudioAdjacent adjacent = AudioAdjacent::None;
    bool markAdded = false;                // the first mark (the shift panel enables "audio time")
    std::optional<std::int64_t> playEnd;   // player->SetEndPosition, while not playing to the end (A4)
    bool playSelection = false;            // middle double click: Play the selection (A4)
    // A5: the right click's syllable to play (Play(GetSylTimes)); legacy
    // returned before drawing the mouse's cursor (`keepCursor`), or hid it
    // over the syllables' letters (`hideCursor`).
    std::optional<std::pair<int, int>> playMs;
    bool keepCursor = false;
    bool hideCursor = false;
};

// A5: karaoke mode's part of the mouse (legacy hasKara with its Karaoke).
struct AudioKaraokeMouse {
    AudioKaraoke *karaoke = nullptr;
    bool moveOnClick = false; // AUDIO_KARAOKE_MOVE_ON_CLICK
    KaraokeMeasure measure;   // the label font (GetTextExtentPixel)
};

// Legacy AudioDisplay's timing state (hold, holding, selStart/selEnd, the
// mark, draggingScale, lastX/lastDragX, inside, defCursor).
class AudioTiming {
public:
    // The selection the display holds (legacy curStartMS, curEndMS) and
    // NeedCommit, which the box's caller owns.
    struct Selection {
        int startMs = 0, endMs = 0;
        bool modified = false;
    };

    // Legacy DoUpdateImage: the boundaries' and mark's columns from the times.
    void drawn(const AudioView &view, const Selection &selection);
    // Legacy OnMouseEvent, over audio that is drawn; with a karaoke (A5) its
    // syllables too.
    AudioMouseResult mouse(const AudioMouse &event, AudioView &view, Selection &selection, const AudioSnapContext &snap,
                           const AudioTimingOptions &options, int scrollbarThickness,
                           const AudioKaraokeMouse &karaoke = {});
    // Legacy SetMark; true when it is the first.
    bool setMark(int ms);
    // Legacy OnLostCapture.
    void lostCapture();
    // A new box (legacy Reset keeps the mark: AudioDisplay is made again with the box).
    void reset() { *this = AudioTiming{}; }

    bool hasMark() const { return m_hasMark; }
    int markMs() const { return m_markMs; }
    int hold() const { return m_hold; } // 0 none, 1 start, 2 end, 3 from nothing, 4 mark, 5 a syllable (A5)
    bool holding() const { return m_holding; }
    // A5: legacy Grabbed, the syllable boundary last under the mouse in the
    // karaoke rows (-1: none). Legacy never clears it on release, nor when
    // karaoke is switched off; while it is not -1 a release keeps the
    // selection as it is (no ZEROIT, no Alt neighbour).
    int grabbed() const { return m_grabbed; }

private:
    int m_hold = 0;
    bool m_holding = false;
    bool m_draggingScale = false;
    bool m_inside = false;
    bool m_defCursor = true;
    bool m_hasMark = false;
    int m_markMs = 0;
    std::int64_t m_selStart = 0, m_selEnd = 0, m_selMark = 0;
    std::int64_t m_lastX = 0, m_lastDragX = 0;
    int m_grabbed = -1; // A5
};

// Legacy AudioDisplay::CommitChanges: the selection goes into the editor's
// time fields (the active Line's draft; a field that already shows the time
// is not marked modified), then with `save` EditBox::Send(AUDIO_CHANGE_TIME,
// nextLine): the draft's changed fields go to the active Line, or with
// several Lines selected to every selected Line (SubsGrid::ChangeLine), as one
// "Changing time on audio spectrum" step; with `nextLine` SubsGrid::NextLine
// takes the next shown Line, or on the last one appends a copy starting at
// its end, five seconds long, without text, and the step is then named
// "Adding a new line" (legacy recorded the append first and the commit had
// nothing left to record).
struct AudioCommitRequest {
    int startMs = 0, endMs = 0;
    bool save = true;
    bool nextLine = false;
    bool holding = false; // the display holds a boundary: NextLine does nothing
    AudioAdjacent adjacent = AudioAdjacent::None;
    // A5: legacy Commit in karaoke mode first puts the syllables' text
    // (GetText) into the editor's text field (TextEdit: the translation's
    // in a TLMode file), marked modified even when it is the same text, so
    // the Send writes it (to every selected Line with several selected).
    std::optional<std::u16string> karaokeText;
};
struct AudioCommitOutcome {
    bool stepped = false;              // a history step was recorded
    std::optional<core::LineId> next;  // the Line NextLine makes active (selected alone)
};
// The times as the editor's fields keep them: ASS in centiseconds, TMPlayer
// in seconds, SRT in milliseconds; never below 0 (SubsTime::NewTime, raw and
// SetRaw).
int legacyFieldTime(core::SubtitleFormat format, int ms);
// `shown`: the Grid's shown Lines (legacy isVisible). Refused with
// InvalidDraft when E63-invalid-commit blocks the draft (it stays).
std::expected<AudioCommitOutcome, CommandRefusal> commitAudioTimes(EditSession &session,
                                                                   const AudioCommitRequest &request,
                                                                   const LineVisible &shown);

} // namespace hikari::application
