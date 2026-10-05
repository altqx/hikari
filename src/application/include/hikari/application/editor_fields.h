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
// the text is not the field's form; the legacy control only lets the form be
// typed (its digit validator and fixed separators).
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

// NumCtrl::GetInt for the Layer (EditBox.cpp:256: -10000000..10000000): the
// value clamped to the range, or nullopt for text NumCtrl keeps the previous
// value for.
std::optional<std::int64_t> layerValue(std::u8string_view text);

// The \an choice (EditBox::SetAlignment, EditBox.cpp:2071-2083): the first
// "\an<digits>" of the translation (the text when it is empty), else the
// Style's alignment. 1..9 for the list; legacy selects nothing otherwise.
int legacyAlignment(const core::LineRecord &line, int styleAlignment);

} // namespace hikari::application
