#pragma once

// The Grid's split family (G4; legacy SubsGrid::Split at 20d647c4), one
// "Splitting lines" step each.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/legacy_timebase.h"

#include <expected>

namespace hikari::application {

// GRID_SPLIT_BY_VIDEO_TIME: with exactly one shown selected Line, it ends at
// the end time of the frame at the video time (EndTimeFor(FrameAt(t)), to
// centiseconds) and a copy starts there, after it. Legacy does not check that
// the time falls inside the Line (characterized).
std::expected<void, CommandRefusal> splitAtVideoTime(EditSession &session, const LegacyTimebase &timebase,
                                                     std::int64_t videoMs, const LineVisible &visible = {});
// GRID_SPLIT_BY_FRAME: each shown selected Line becomes one Line per frame
// from the frame at its Start to the one before the frame at its End, timed
// StartTimeFor/EndTimeFor (to centiseconds); a \move becomes the \pos it has
// at each frame's time. Needs an exact (indexed) timebase.
std::expected<void, CommandRefusal> splitIntoFrames(EditSession &session, const LegacyTimebase &timebase,
                                                    const LineVisible &visible = {});

namespace legacy {
// getfloat(num, "5.3f", true): three decimals, trailing zeros and point dropped.
std::u8string floatText(float value);
// The text with its first \move(...) (or \pos(...)) replaced by \pos(x,y) at
// `ms`, the move's times absolute (zero times meaning the Line's own).
std::u8string moveToPos(std::u8string_view text, std::int64_t lineStartMs, std::int64_t lineEndMs, int ms);
} // namespace legacy

} // namespace hikari::application
