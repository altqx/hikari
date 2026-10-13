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
// The wheel: `notches` whole wheel steps (GetWheelRotation() /
// GetWheelDelta(), so a smaller rotation is none) over `column`. A clock
// form moves by notches times the column's step, refused (nullopt) when the
// time would leave 0..kTimeFieldMaxMs. A MicroDVD number (`microDvdNumber`,
// with or without the frames switch: legacy's `form == MDVD` steps orgframe)
// moves by `notches` frames, refused below 0 or past kTimeFieldDigits.
// nullopt for any other number (MPL2's deciseconds, the frames switch's
// frames of another format): legacy stepped those wrongly (its 1 ms step on
// MPL2's deciseconds goes up only; with the frames switch it stepped the
// milliseconds behind a frame number that never changed), so they are not
// ported until the user decides how they step.
std::optional<std::u16string> wheelTimeField(TimeFieldForm form, bool microDvdNumber, std::u16string_view text,
                                             std::size_t column, int notches);
// The right button held down (TimeCtrl's `holding`): the time it started
// from and accumulates (mstime), the column's step (grad) and the pointer
// position it last stepped at (oldposx, oldposy).
struct TimeFieldDrag {
    std::int64_t ms = 0, step = 0;
    int x = 0, y = 0;
};
// The right button pressed at `column` and (x, y) of a clock form; nullopt
// in a number form, where legacy's drag never stepped the number shown
// (MicroDVD, MPL2) or set the frame to a time in milliseconds (the frames
// switch): not ported, as for the wheel.
std::optional<TimeFieldDrag> beginTimeFieldDrag(TimeFieldForm form, std::u16string_view text, std::size_t column,
                                                int x, int y);
// The pointer moved to (x, y) with the button held: 8 pixels up or down
// (where the vertical move is not the smaller) step the time by the step,
// up adding; otherwise 10 pixels right or left step it by ten steps, right
// adding. Each step restarts both distances. The time stays within
// 0..kTimeFieldMaxMs. The new text, or nullopt when nothing stepped.
std::optional<std::u16string> dragTimeField(TimeFieldForm form, TimeFieldDrag &drag, int x, int y);

// NumCtrl::GetInt for the Layer (EditBox.cpp:256: -10000000..10000000): the
// value clamped to the range, or nullopt for text NumCtrl keeps the previous
// value for.
std::optional<std::int64_t> layerValue(std::u8string_view text);

// The \an choice (EditBox::SetAlignment, EditBox.cpp:2071-2083): the first
// "\an<digits>" of the translation (the text when it is empty), else the
// Style's alignment. 1..9 for the list; legacy selects nothing otherwise.
int legacyAlignment(const core::LineRecord &line, int styleAlignment);

} // namespace hikari::application
