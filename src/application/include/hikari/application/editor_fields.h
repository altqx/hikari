#pragma once

#include "hikari/application/legacy_timebase.h"
#include "hikari/core/document.h"
#include "hikari/core/time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

enum class TimeFieldRole { Duration = 0, Start = 1, End = 2 };

struct EditorTimeTexts {
    std::u8string start, end, duration;
};
// Times are whole ms. ASS "H:MM:SS.cc", SRT "HH:MM:SS,mmm", TMPlayer
// "HH:MM:SS", MicroDVD the authored frame, MPL2 deciseconds rounded up.
// `frames`: the timebase when the frames switch is on; only an exact one shows
// frames: Start the frame at or after the start, End the frame before the end,
// Duration the frames between them, both included.
EditorTimeTexts editorTimeTexts(const core::LineRecord &line, core::SubtitleFormat format,
                                const LegacyTimebase *frames);
// End - Start clamped at 0; in frame mode the frame at that many ms, not a frame count.
std::u8string editedDurationText(const core::LineRecord &line, core::SubtitleFormat format,
                                 const LegacyTimebase *frames);

// nullopt when the text is not the field's form; an empty number field reads as 0.
struct TypedTime {
    std::int64_t ms = 0;                // the time, or the duration
    std::optional<std::int64_t> frame;  // MicroDVD: the authored frame (or frame count)
    bool timeResolved = true;           // false: a MicroDVD frame without the Document's rate
};
std::optional<TypedTime> typedTime(std::u8string_view text, TimeFieldRole role, core::SubtitleFormat format,
                                   const std::optional<core::FrameRate> &microDvdRate, const LegacyTimebase *frames);

// ASS centiseconds, TMPlayer seconds, MPL2 deciseconds rounded up, SRT
// milliseconds; never below 0. MicroDVD keeps the milliseconds.
std::int64_t fieldPrecisionTime(core::SubtitleFormat format, std::int64_t ms);
// The frame (the time rounded up at the rate) and the time it starts at;
// nullopt without a rate.
struct MicroDvdTime {
    std::int64_t frame = 0;
    std::int64_t ms = 0;
};
std::optional<MicroDvdTime> microDvdFieldTime(std::int64_t ms, const std::optional<core::FrameRate> &rate);
// nullopt without a rate.
std::optional<core::DocumentTime> microDvdFrameTime(std::int64_t frame, const std::optional<core::FrameRate> &rate);

// Masked editing: the clock forms keep their separators and length; Number
// (MPL2, MicroDVD, frames) is digits only, up to kTimeFieldDigits.
enum class TimeFieldForm { Ass = 0, Srt = 1, TMPlayer = 2, Number = 3 };
TimeFieldForm timeFieldForm(core::SubtitleFormat format, const LegacyTimebase *frames);
// Nine digits stay an int.
inline constexpr std::size_t kTimeFieldDigits = 9;

// The field's text and selection (the caret when start == end).
struct TimeFieldState {
    std::u16string text;
    std::size_t selectionStart = 0, selectionEnd = 0;
};
// The old state when refused; `bell` for a refused key or paste.
struct TimeFieldEdit {
    TimeFieldState state;
    bool changed = false;
    bool bell = false;
};
// A non-digit is refused with the bell. In a clock form a digit over a
// selection zeroes it and goes to its start; at the end it is refused;
// otherwise it replaces the next digit, skipping a separator, and is at most 5
// right after ':'. A number takes it as typed, the bell past kTimeFieldDigits.
TimeFieldEdit typeIntoTimeField(TimeFieldForm form, const TimeFieldState &field, char16_t c);
// Clock form: the selected digits, or the one before the caret, become 0 and
// the caret goes before them and any separator in front; nothing at the start.
TimeFieldEdit backspaceTimeField(const TimeFieldState &field);
// A clock form takes only a whole time of its form, all of it selected; a
// number takes digits only, in place of the selection, cut at
// kTimeFieldDigits. Anything else is refused with the bell.
TimeFieldEdit pasteIntoTimeField(TimeFieldForm form, const TimeFieldState &field, std::u16string_view pasted);

// In ms: hours before column 2 (ASS) or 3 (SRT, TMPlayer), minutes in the
// next three columns, seconds in the three after, 10 ms past them.
std::int64_t timeFieldStep(TimeFieldForm form, std::size_t column);
// 9:59:59.999
inline constexpr std::int64_t kTimeFieldMaxMs = 35999999;

enum class TimeCtrlForm { Ass = 0, Srt = 1, TMPlayer = 2, MicroDvd = 3, Mpl2 = 4 };
TimeCtrlForm timeCtrlForm(core::SubtitleFormat format);
struct TimeFieldMouse {
    TimeCtrlForm form = TimeCtrlForm::Ass;
    bool frames = false; // the switch on and an exact timebase
};
// A number form steps by 1.
std::int64_t timeFieldStep(TimeFieldMouse mouse, std::size_t column);

// `notches` whole wheel steps. A clock form moves by notches times the
// column's step, nullopt when it would leave 0..kTimeFieldMaxMs. A number
// moves by the notches, nullopt below 0 or past its top (MPL2's deciseconds
// of kTimeFieldMaxMs, a frame's kTimeFieldDigits).
std::optional<std::u16string> wheelTimeField(TimeFieldMouse mouse, std::u16string_view text, std::size_t column,
                                             int notches);
// `value`: ms in a clock form, the number in a number form; (x, y): where it last stepped.
struct TimeFieldDrag {
    std::int64_t value = 0, step = 0;
    int x = 0, y = 0;
};
// Starts from the value the text reads; a clock form's empty text is 0 ms.
TimeFieldDrag beginTimeFieldDrag(TimeFieldMouse mouse, std::u16string_view text, std::size_t column, int x, int y);
// 8 pixels up or down (where the vertical move is not the smaller) step by
// one step, up adding; otherwise 10 pixels right or left by ten, right adding.
// Each step restarts both distances; the value stays within the wheel's
// limits. nullopt when nothing stepped; a clock form shows its time even at a
// limit, a number at its limit is no step.
std::optional<std::u16string> dragTimeField(TimeFieldMouse mouse, TimeFieldDrag &drag, int x, int y);

// A field's `value` may lie outside its range: a typed number is kept as is.
struct NumberFieldRange {
    std::int64_t from = 0, to = 0;
};
inline constexpr NumberFieldRange kLayerRange{-10000000, 10000000};
inline constexpr NumberFieldRange kMarginRange{0, 9999};
// nullopt where the field keeps its previous value (empty, "-", or not a number).
std::optional<std::int64_t> numberFieldValue(std::u16string_view text);
// Clamped to the range; nullopt when the value did not change.
std::optional<std::u16string> wheelNumberField(std::int64_t &value, int notches, NumberFieldRange range);
// (x, y): where it last stepped.
struct NumberFieldDrag {
    std::int64_t value = 0;
    int x = 0, y = 0;
};
// 8 pixels up or down step by one, 10 pixels right or left by ten, clamped to
// the range. A step that changes nothing keeps the position; nullopt then.
std::optional<std::u16string> dragNumberField(NumberFieldDrag &drag, int x, int y, NumberFieldRange range);

// Clamped to kLayerRange; nullopt for text that keeps the previous value.
std::optional<std::int64_t> layerValue(std::u8string_view text);

// The first "\an<digits>" of the translation (the text when it is empty),
// else the Style's alignment; outside 1..9 selects nothing.
int legacyAlignment(const core::LineRecord &line, int styleAlignment);

}
