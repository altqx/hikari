#pragma once

// Grid structural commands (G3; legacy SubsGrid::OnInsertBefore/After,
// InsertWithVideoTime, OnDuplicate and DeleteRows at 20d647c4). Each is one
// undo step under the legacy history name, and leaves the selection where
// legacy does. New copies get new LineIds; retained Lines keep theirs.

#include "hikari/application/edit_session.h"

#include <expected>
#include <functional>
#include <optional>
#include <string_view>

namespace hikari::application {

enum class InsertWhere { Before, After };

// The shown video frame: its start and the next frame's start, in ms
// (legacy VideoBox::GetFrameTime(true / false)).
struct FrameTimes {
    std::int64_t startMs = 0, endMs = 0;
};

// Which Lines the Grid shows (filtering); every Line when empty.
using LineVisible = std::function<bool(core::LineId)>;

// GRID_INSERT_BEFORE / _AFTER: a copy of the active Line without text, timed
// against its neighbours (or 4 s); with `videoMs` (GRID_INSERT_*_VIDEO) it
// starts at the video time and lasts 4 s. The new Line becomes active and selected.
std::expected<void, CommandRefusal> insertLine(EditSession &session, InsertWhere where,
                                               std::optional<std::int64_t> videoMs = std::nullopt,
                                               const LineVisible &visible = {});
// GRID_INSERT_*_WITH_VIDEO_FRAME: copies of the selected Lines timed to the
// shown frame, before the first or after the last selected; the copies are selected.
std::expected<void, CommandRefusal> insertWithFrameTimes(EditSession &session, InsertWhere where, FrameTimes frame);
// GRID_DUPLICATE_LINES: copies of the first contiguous run of selected Lines
// (hidden Lines between them do not end it) after that run; the copies are
// selected unless GRID_DUPLICATION_DONT_CHANGE_SELECTION.
std::expected<void, CommandRefusal> duplicateLines(EditSession &session, const LineVisible &visible = {},
                                                   bool keepSelection = false);
// GLOBAL_REMOVE_LINES: every selected Line, hidden ones included; an empty
// Document gets the default Line. The Line now at the first deleted position becomes active.
std::expected<void, CommandRefusal> deleteLines(EditSession &session);

enum class JoinKind {
    Join,         // GRID_JOIN_LINES (2 to 20 selected): earliest Start, latest End, texts joined with \N
    WithPrevious, // GLOBAL_JOIN_WITH_PREVIOUS (F4): the active Line and the shown one before it
    WithNext,     // GLOBAL_JOIN_WITH_NEXT (F5): the active Line and the shown one after it
    KeepFirst,    // GRID_JOIN_TO_FIRST_LINE (2 to 500): the first Line, ending where the last ends
    KeepLast,     // GRID_JOIN_TO_LAST_LINE: the same with the last Line's text
};
// The first participating Line survives with its LineId; under the approved
// J56-selected-only-join only the other participating Lines are deleted,
// so unselected Lines between them survive (legacy deleted the whole span).
std::expected<void, CommandRefusal> joinLines(EditSession &session, JoinKind kind, const LineVisible &visible = {});
// GRID_SWAP_LINES: exactly two selected Lines trade places; the active row
// keeps its position, so the active Line becomes the other one (legacy).
std::expected<void, CommandRefusal> swapLines(EditSession &session);
// GRID_MAKE_CONTINOUS_PREVIOUS_LINE / _NEXT_LINE: each selected Line starts
// where the Line before it ends, or ends where the Line after it starts.
std::expected<void, CommandRefusal> makeContinuous(EditSession &session, bool withPrevious, const LineVisible &visible = {});

// GRID_SET_FPS_FROM_VIDEO ("Setting FPS from video"): with exactly two shown
// selected Lines, every Line is retimed so the second one starts at
// `videoMs`, scaling the distance from the first one's Start (legacy
// OnSetFPSFromVideo). Refused when both start at the same time (legacy
// divides by zero there).
std::expected<void, CommandRefusal> setFpsFromVideo(EditSession &session, std::int64_t videoMs,
                                                    const LineVisible &visible = {});
// GRID_SET_NEW_FPS ("Setting custom FPS"): every time scaled by
// oldFps / newFps. A MicroDVD Document recomputes its frames from its own
// rate (C01-fps-isolation) and is refused while that rate is unknown.
std::expected<void, CommandRefusal> setNewFps(EditSession &session, double oldFps, double newFps);

// GLOBAL_SORT_ALL_BY_* / GLOBAL_SORT_SELECTED_BY_* ("Sorting subtitles"):
// a stable sort of every Line or of the selected Lines (hidden ones too)
// among their own rows. Ties go by End for Start, by Start otherwise. The
// selection stays on the same rows, as legacy keeps row numbers. An order
// that does not change adds no step.
enum class SortKey { Start, End, Style, Actor, Effect, Layer };
// Locale collation for Style, Actor and Effect (<0, 0, >0); bytes when empty.
using TextCompare = std::function<int(std::u8string_view, std::u8string_view)>;
std::expected<void, CommandRefusal> sortLines(EditSession &session, SortKey key, bool selectedOnly,
                                              const TextCompare &compare = {});

} // namespace hikari::application
