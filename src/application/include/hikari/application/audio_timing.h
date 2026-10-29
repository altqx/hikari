#pragma once

#include "hikari/application/audio_display.h"
#include "hikari/application/audio_karaoke.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>

namespace hikari::application {

struct AudioTimingOptions {
    bool autoCommit = true;
    bool snapToKeyframes = false;
    bool snapToOtherLines = false;
    int startDragSensitivity = 6;
    int leadIn = 200;
    int leadOut = 300;
    bool dontPlayWhenLineChanges = false;
};

constexpr int legacyZeroIt(int ms) { return (ms / 10) * 10; }

// The shown Line `delta` shown Lines away, or -1 when none; `position` itself for delta 0
// or going back from position 0.
int legacyKeyFromPositionOrNone(std::span<const AudioLineSpan> lines, int position, int delta);

struct AudioSnapContext {
    const AudioView *view = nullptr;
    bool snapToKeyframes = false;
    bool snapToOtherLines = false;
    bool drawKeyframes = true; // only drawn keyframes snap
    int inactiveLines = 1;
    // keyframeSnapMs: each keyframe's frame start, or 21 ms before it without a video.
    std::span<const int> keyframesMs;
    std::span<const int> keyframeSnapMs;
    std::span<const AudioLineSpan> lines;
    int active = -1;
};
// The keyframe or other Line boundary in view nearest `ms` within `rangeX` columns, else `ms`;
// rounded down to 10 ms. Shift inverts both options; `keysnap` skips boundaries under 10 ms away.
int legacyBoundarySnap(const AudioSnapContext &context, int ms, int rangeX, bool shiftHeld, bool keysnap,
                       bool otherLines);

// The start moves back by `leadIn` (not below 0), the end on by `leadOut`.
std::pair<int, int> legacyAddLead(int startMs, int endMs, bool in, bool out, int leadIn, int leadOut);

struct AudioMouse {
    enum class Type { Move, Press, Release, DoubleClick };
    enum class Button { None, Left, Right, Middle };
    Type type = Type::Move;
    Button button = Button::None;
    int x = 0, y = 0;
    bool leftHeld = false, rightHeld = false, middleHeld = false; // after the event
    bool shift = false, ctrl = false, alt = false;
};

// The Line an Alt release moves: Next starts at the end, Previous ends at the start.
enum class AudioAdjacent { None, Previous, Next };

struct AudioMouseResult {
    bool redraw = false;
    bool focus = false;
    std::optional<bool> sizeCursor; // true: horizontal resize cursor, false: the default
    std::optional<int> seekVideoMs;
    // Set by a release that commits; true: move to the end.
    std::optional<bool> commit;
    AudioAdjacent adjacent = AudioAdjacent::None;
    bool markAdded = false;              // the first mark
    std::optional<std::int64_t> playEnd; // the player's new end, unless it plays to the end
    bool playSelection = false;
    std::optional<std::pair<int, int>> playMs;
    bool keepCursor = false;
    bool hideCursor = false;
    // The commit does not scroll the current syllable into view.
    bool keepView = false;
};

struct AudioKaraokeMouse {
    AudioKaraoke *karaoke = nullptr;
    bool moveOnClick = false;
    KaraokeMeasure measure;
    // A start as its field commits it; a dragged start moves the syllables by this.
    std::function<int(int)> fieldTime;
    // Ascending; empty, nothing snaps. Only a dragged boundary snaps, within kOnsetSnapWindowMs
    // and 16 columns, between its neighbours.
    std::span<const int> onsetsMs;
};

class AudioTiming {
public:
    struct Selection {
        int startMs = 0, endMs = 0;
        bool modified = false;
    };

    // Recomputes the boundaries' and mark's columns from the times.
    void drawn(const AudioView &view, const Selection &selection);
    AudioMouseResult mouse(const AudioMouse &event, AudioView &view, Selection &selection, const AudioSnapContext &snap,
                           const AudioTimingOptions &options, int scrollbarThickness,
                           const AudioKaraokeMouse &karaoke = {});
    // True when it is the first mark.
    bool setMark(int ms);
    // A start the left button was dragging leaves the syllables at their times. True for a start
    // the right button carried the syllables with: the caller commits them.
    bool lostCapture(int startMs, const AudioKaraokeMouse &karaoke = {});
    void reset() { *this = AudioTiming{}; }

    bool hasMark() const { return m_hasMark; }
    int markMs() const { return m_markMs; }
    // MUST KILL: hold() is a raw int: 0 none, 1 start, 2 end, 3 from nothing, 4 mark, 5 syllable
    int hold() const { return m_hold; }
    bool holding() const { return m_holding; }
    // The syllable boundary last under the mouse, -1 for none; never cleared on release.
    // While set, a release keeps the selection as it is.
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
    int m_grabbed = -1;
    int m_startOrigin = 0;
    bool m_startCarries = false;
    std::int64_t m_pressX = 0;
    bool m_dragged = false;
};

// Puts the times in the editor's fields; with `save` commits the draft to the active Line (or
// every selected Line) as one step; `nextLine` then moves on, appending a 5 s copy after the last.
struct AudioCommitRequest {
    int startMs = 0, endMs = 0;
    bool save = true;
    bool nextLine = false;
    bool holding = false; // true: nextLine does nothing
    AudioAdjacent adjacent = AudioAdjacent::None;
    // Goes into the text field (the translation in translation mode), always marked modified.
    std::optional<std::u16string> karaokeText;
    // The Duration field takes the times too.
    bool duration = true;
    // nullptr: "Changing time on audio spectrum".
    const char *stepName = nullptr;
    // Only the active Line takes this request's fields; other pending edits still go to every
    // selected Line.
    bool activeOnly = false;
};
struct AudioCommitOutcome {
    bool stepped = false;
    std::optional<core::LineId> next; // made active and selected alone
};
// Rounded to the format's field precision (ASS cs, TMPlayer s, SRT ms), never below 0.
int legacyFieldTime(core::SubtitleFormat format, int ms);
// legacyFieldTime, or a MicroDVD frame's start at the Document's rate.
int committedAudioTime(const core::Document &document, int ms);
// Refused with InvalidDraft when the draft is invalid (the draft stays).
std::expected<AudioCommitOutcome, CommandRefusal> commitAudioTimes(EditSession &session,
                                                                   const AudioCommitRequest &request,
                                                                   const LineVisible &shown);

}
