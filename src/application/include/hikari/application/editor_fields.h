#pragma once

// E4: the Line editor's time fields and counters as the legacy EditBox shows
// and reads them (EditBox.cpp, TimeCtrl.cpp, SubsTime.cpp and NumCtrl.cpp at
// 20d647c4). Times are whole milliseconds, as legacy SubsTime keeps them.
//
// Each format has its own field text (SubsTime::raw): ASS "H:MM:SS.cc", SRT
// "HH:MM:SS,mmm", TMPlayer "HH:MM:SS", MicroDVD the authored frame, MPL2
// deciseconds rounded up. With EDITBOX_TIMES_TO_FRAMES_SWITCH on and an exact
// timebase (an indexed video) the fields show video frames instead: Start the
// frame at or after the start (Timebase::FrameAt), End the frame before the
// end, Duration the frames between them, both included.

#include "hikari/application/legacy_timebase.h"
#include "hikari/core/document.h"
#include "hikari/core/time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

// TimeCtrl::SetTime/GetTime's `opt`: 1 for Start, 2 for End, 0 for Duration.
enum class TimeFieldRole { Duration = 0, Start = 1, End = 2 };

// What the fields show. `frames` is the timebase when the switch is on and a
// video is open (it shows frames only when exact()).
struct EditorTimeTexts {
    std::u8string start, end, duration;
};
// EditBox::SetLine (EditBox.cpp:414-422): the Duration as the Line is shown.
EditorTimeTexts editorTimeTexts(const core::LineRecord &line, core::SubtitleFormat format,
                                const LegacyTimebase *frames);
// EditBox::OnEdit (EditBox.cpp:1541-1543) after a Start or End edit: the
// Duration shows End - Start (clamped at 0) through SetTime(..., 1), which in
// frame mode is the frame at that many milliseconds, not the frame count.
std::u8string editedDurationText(const core::LineRecord &line, core::SubtitleFormat format,
                                 const LegacyTimebase *frames);

// A typed time (TimeCtrl::GetTime after the field was edited). nullopt when
// the text is not the field's form; the masked field below lets only the form
// be typed, as the legacy control did (its digit validator and fixed
// separators). An empty number field reads as 0 (SubsTime::ParseMS,
// SubsTime.cpp:48).
struct TypedTime {
    std::int64_t ms = 0;                // the time, or the duration
    std::optional<std::int64_t> frame;  // MicroDVD: the authored frame (or frame count)
    bool timeResolved = true;           // false: a MicroDVD frame without the Document's rate
};
std::optional<TypedTime> typedTime(std::u8string_view text, TimeFieldRole role, core::SubtitleFormat format,
                                   const std::optional<core::FrameRate> &microDvdRate, const LegacyTimebase *frames);

// The field precision legacy keeps when a time goes through a field
// (SubsTime::NewTime, raw and SetRaw): ASS centiseconds, TMPlayer seconds,
// MPL2 deciseconds rounded up, SRT milliseconds; never below 0. MicroDVD
// keeps the milliseconds (its frame is microDvdFrame's).
std::int64_t fieldPrecisionTime(core::SubtitleFormat format, std::int64_t ms);
// A MicroDVD field given a time (SubsTime::raw with no frame: rounded up at
// the rate, C01-fps-isolation's per-Document rate in place of legacy's 25)
// and the time that frame starts at; nullopt without a rate.
struct MicroDvdTime {
    std::int64_t frame = 0;
    std::int64_t ms = 0;
};
std::optional<MicroDvdTime> microDvdFieldTime(std::int64_t ms, const std::optional<core::FrameRate> &rate);
// The start of a MicroDVD frame at the Document's rate (in place of legacy's
// frame / 23.976), or nullopt without one (C01-fps-isolation).
std::optional<core::DocumentTime> microDvdFrameTime(std::int64_t frame, const std::optional<core::FrameRate> &rate);

// E4: TimeCtrl's masked editing (HikariSub/TimeCtrl.cpp), so a time field
// never holds text outside its form. The clock forms (TimeCtrl's `form < MDVD
// && !showFrames`) keep their separators and length: ASS "H:MM:SS.cc", SRT
// "HH:MM:SS,mmm", TMPlayer "HH:MM:SS". MPL2, MicroDVD and the frames of the
// Time/Frames switch are numbers: digits only, up to kTimeFieldDigits.
enum class TimeFieldForm { Ass = 0, Srt = 1, TMPlayer = 2, Number = 3 };
TimeFieldForm timeFieldForm(core::SubtitleFormat format, const LegacyTimebase *frames);
// Legacy SetMaxLength(20) let wxAtoi overflow; nine digits stay an int.
inline constexpr std::size_t kTimeFieldDigits = 9;

// The field's text and selection (the caret when start == end).
struct TimeFieldState {
    std::u16string text;
    std::size_t selectionStart = 0, selectionEnd = 0;
};
// One edit's outcome: the new state (the old one when refused), whether the
// text changed, and legacy's wxBell for a refused key or paste.
struct TimeFieldEdit {
    TimeFieldState state;
    bool changed = false;
    bool bell = false;
};
// A typed character. Not a digit: refused with the bell (the digits-only
// HikariTextValidator, TimeCtrl.cpp:34-47). In a clock form a digit typed
// over a selection first zeroes the selected digits and goes to its start
// (OnKeyEvent, :212-223); at the end it is refused with the bell (:224-226);
// otherwise it replaces the next digit, skipping a ':' '.' or ',' before it
// (OnTimeWrite, :166-201), and a digit straight after ':' is at most 5
// (:190-191). A number takes it as typed text, the bell past
// kTimeFieldDigits (HikariTextCtrl::OnCharPress).
TimeFieldEdit typeIntoTimeField(TimeFieldForm form, const TimeFieldState &field, char16_t c);
// Backspace in a clock form (TimeCtrl.cpp:113-137): the selected digits, or
// the one before the caret, become 0 and the caret goes to the start of
// them, before a separator in front of it; nothing at the start.
TimeFieldEdit backspaceTimeField(const TimeFieldState &field);
// Paste (TimeCtrl.cpp:68-107): a clock form takes only a whole time of its
// form (ASS ^[0-9]:[0-5][0-9]:[0-5][0-9]\.[0-9][0-9]$, SRT with two-digit
// hours and ",mmm", TMPlayer HH:MM:SS), all of it selected; a number takes
// digits only (legacy's IsNumber also let a sign through, which no frame or
// decisecond count holds), in place of the selection and cut at
// kTimeFieldDigits (HikariTextCtrl::Paste). Anything else is refused with
// the bell.
TimeFieldEdit pasteIntoTimeField(TimeFieldForm form, const TimeFieldState &field, std::u16string_view pasted);

// E4: TimeCtrl's mouse (TimeCtrl.cpp:286-387): the right button dragged
// over a time field, and the wheel, step its time. In a clock form the step
// is that of the column (TimeCtrl's `grad`, from HitTest's column): hours
// before column 2 (ASS) or 3 (SRT, TMPlayer), minutes in the next three,
// seconds in the three after, 10 ms past them.
std::int64_t timeFieldStep(TimeFieldForm form, std::size_t column);
// TimeCtrl's top time, 9:59:59.999 (TimeCtrl.cpp:322-323, 380).
inline constexpr std::int64_t kTimeFieldMaxMs = 35999999;

// The mouse reads TimeCtrl's own `form`, the Document's format as SubsTime
// numbers it (ASS, SRT, TMP, MDVD, MPL2), and its showFrames, not the
// field's mask: with the Time/Frames switch a frame number still steps by the
// format's column units, and MPL2's deciseconds by TimeCtrl's 1 ms.
enum class TimeCtrlForm { Ass = 0, Srt = 1, TMPlayer = 2, MicroDvd = 3, Mpl2 = 4 };
TimeCtrlForm timeCtrlForm(core::SubtitleFormat format);
struct TimeFieldMouse {
    TimeCtrlForm form = TimeCtrlForm::Ass;
    bool frames = false; // the field shows frames (the switch on, an exact timebase)
};
// TimeCtrl's `grad` for the column under the pointer: 1 for MicroDVD and
// MPL2, else the clock column's (startPos 2 for ASS, 3 otherwise), the frame
// number's columns included.
std::int64_t timeFieldStep(TimeFieldMouse mouse, std::size_t column);

// A stepped field: its text and the time behind it (TimeCtrl's
// mTime.mstime, which a frame number does not show and the next drag of a
// frame number starts from).
struct TimeFieldStepped {
    std::u16string text;
    std::int64_t ms = 0;
};
// The wheel: `notches` whole wheel steps (GetWheelRotation() /
// GetWheelDelta(), so a smaller rotation is none) over `column` of `text`,
// with `ms` the time behind it. As TimeCtrl.cpp:363-385 does, the text is
// read in its form (SetRaw), MicroDVD's frame moves by the notches and every
// other form's milliseconds by notches times the step, and the move is
// refused (nullopt) when the time would leave 0..kTimeFieldMaxMs or the frame
// go below 0. What it shows, SubsTime::raw:
// - a clock form: the stepped time;
// - MPL2: the milliseconds in deciseconds rounded up, so a notch up adds a
//   decisecond and a notch down shows the same number;
// - MicroDVD: the stepped frame, except that frame 0 reached from another
//   frame shows the frame it started from (raw recomputes a 0 frame from the
//   time, which is that frame's);
// - frames (the switch): the frame, which moves only on MicroDVD; on the
//   other formats the step goes to the time behind the unchanged number.
// A number past kTimeFieldDigits is refused too.
std::optional<TimeFieldStepped> wheelTimeField(TimeFieldMouse mouse, std::u16string_view text, std::int64_t ms,
                                               std::size_t column, int notches);
// The right button held down (TimeCtrl's `holding`): the time it started
// from and accumulates (TimeCtrl::mstime), the column's step (grad), the
// pointer position it last stepped at (oldposx, oldposy) and MicroDVD's
// frame (mTime.orgframe).
struct TimeFieldDrag {
    std::int64_t ms = 0, step = 0;
    int x = 0, y = 0;
    std::int64_t frame = 0;
};
// The right button pressed at `column` and (x, y), `ms` the time behind the
// text: the drag starts from the time the text reads in its form (a frame
// number: the time behind it).
TimeFieldDrag beginTimeFieldDrag(TimeFieldMouse mouse, std::u16string_view text, std::int64_t ms, std::size_t column,
                                 int x, int y);
// The pointer moved to (x, y) with the button held: 8 pixels up or down
// (where the vertical move is not the smaller) step the time by the step,
// up adding; otherwise 10 pixels right or left step it by ten steps, right
// adding. Each step restarts both distances. The time stays within
// 0..kTimeFieldMaxMs. The text shown after a step (the same text when the
// form does not show the change), or nullopt when nothing stepped:
// - a clock form: the time;
// - MPL2: the time in deciseconds rounded up (100 steps to a decisecond);
// - MicroDVD: its frame, which never moves, except that frame 0 becomes 1
//   at the first step above 0 ms (raw's frame for a time at any rate up to
//   100 frames a second), and stays 1;
// - frames (the switch): the stepped milliseconds as the frame number
//   (TimeCtrl.cpp:325-327 sets orgframe to mstime).
std::optional<std::u16string> dragTimeField(TimeFieldMouse mouse, TimeFieldDrag &drag, int x, int y);

// E4: NumCtrl's mouse (NumCtrl.cpp:243-319) on the Layer and the margins,
// whose range NumCtrl clamps to (EditBox.cpp:271, 288-290). `value` is
// NumCtrl's own `value`: what SetInt set, the last number typed (OnNumWrite
// reads it whatever the range), and what the wheel and the drag left.
struct NumberFieldRange {
    std::int64_t from = 0, to = 0;
};
inline constexpr NumberFieldRange kLayerRange{-10000000, 10000000};
inline constexpr NumberFieldRange kMarginRange{0, 9999};
// OnNumWrite's value of typed text, or nullopt where NumCtrl keeps the one
// it had (empty, "-", or not a number).
std::optional<std::int64_t> numberFieldValue(std::u16string_view text);
// The wheel: `value` moves by `notches` (whole notches), and the new value's
// text when it is in the range. Outside it nothing is shown and `value`
// keeps the move (NumCtrl adds the step before its range test), so the wheel
// comes back into the range only as many notches later.
std::optional<std::u16string> wheelNumberField(std::int64_t &value, int notches, NumberFieldRange range);
// The right button held down: the value and the pointer position it last
// stepped at.
struct NumberFieldDrag {
    std::int64_t value = 0;
    int x = 0, y = 0;
};
// The pointer moved to (x, y): 8 pixels up or down step the value by one,
// refused outside the range; 10 pixels right or left by ten, clamped to the
// range, refused only from its end. A refused step leaves the position where
// it was. The new text, or nullopt when nothing stepped.
std::optional<std::u16string> dragNumberField(NumberFieldDrag &drag, int x, int y, NumberFieldRange range);

// NumCtrl::GetInt for the Layer (EditBox.cpp:256: -10000000..10000000): the
// value clamped to the range, or nullopt for text NumCtrl keeps the previous
// value for.
std::optional<std::int64_t> layerValue(std::u8string_view text);

// The \an choice (EditBox::SetAlignment, EditBox.cpp:2071-2083): the first
// "\an<digits>" of the translation (the text when it is empty), else the
// Style's alignment. 1..9 for the list; legacy selects nothing otherwise.
int legacyAlignment(const core::LineRecord &line, int styleAlignment);

} // namespace hikari::application
