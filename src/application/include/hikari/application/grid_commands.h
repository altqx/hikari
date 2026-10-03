#pragma once

// Grid structural commands (G3; legacy SubsGrid::OnInsertBefore/After,
// InsertWithVideoTime, OnDuplicate and DeleteRows at 20d647c4). Each is one
// undo step under the legacy history name, and leaves the selection where
// legacy does. New copies get new LineIds; retained Lines keep theirs.

#include "hikari/application/edit_session.h"

#include <expected>
#include <functional>
#include <optional>

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

} // namespace hikari::application
