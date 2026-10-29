#pragma once

// Each command is one undo step. New copies get new LineIds; retained Lines keep theirs.

#include "hikari/application/edit_session.h"

#include <expected>
#include <functional>
#include <optional>
#include <string_view>

namespace hikari::application {

enum class InsertWhere { Before, After };

// The shown video frame's start and the next frame's start, in ms.
struct FrameTimes {
    std::int64_t startMs = 0, endMs = 0;
};

// Which Lines the Grid shows (filtering); every Line when empty.
using LineVisible = std::function<bool(core::LineId)>;

// A copy of the active Line without text, timed against its neighbours (or 4 s); with
// `videoMs` it starts there and lasts 4 s. The new Line becomes active and selected.
std::expected<void, CommandRefusal> insertLine(EditSession &session, InsertWhere where,
                                               std::optional<std::int64_t> videoMs = std::nullopt,
                                               const LineVisible &visible = {});
// Copies of the selected Lines timed to the frame, before the first or after the last selected;
// the copies are selected.
std::expected<void, CommandRefusal> insertWithFrameTimes(EditSession &session, InsertWhere where, FrameTimes frame);
// Copies of the first contiguous run of selected Lines (hidden Lines do not end it) after that
// run; the copies are selected unless `keepSelection`.
std::expected<void, CommandRefusal> duplicateLines(EditSession &session, const LineVisible &visible = {},
                                                   bool keepSelection = false);
// Every selected Line, hidden ones included; an empty Document gets the default Line. The Line
// now at the first deleted position becomes active.
std::expected<void, CommandRefusal> deleteLines(EditSession &session);
// Empties the text of each shown selected Line (the translation stays), one step even when it
// already was; refused with none shown and selected.
std::expected<void, CommandRefusal> deleteText(EditSession &session, const LineVisible &visible = {});

enum class JoinKind {
    Join,         // 2 to 20 selected: earliest Start, latest End, texts joined with \N
    WithPrevious, // the active Line and the shown one before it
    WithNext,     // the active Line and the shown one after it
    KeepFirst,    // 2 to 500 selected: the first Line, ending where the last ends
    KeepLast,     // the same with the last Line's text
};
// The first participating Line survives with its LineId; only the other participating Lines
// are deleted, so unselected Lines between them survive.
std::expected<void, CommandRefusal> joinLines(EditSession &session, JoinKind kind, const LineVisible &visible = {});
// Exactly two selected Lines trade places; the active row keeps its position, so the active
// Line becomes the other one.
std::expected<void, CommandRefusal> swapLines(EditSession &session);
// Each selected Line starts where the Line before it ends, or ends where the Line after it starts.
std::expected<void, CommandRefusal> makeContinuous(EditSession &session, bool withPrevious, const LineVisible &visible = {});

// With exactly two shown selected Lines, every Line is retimed so the second starts at
// `videoMs`, scaling the distance from the first one's Start. Refused when both start together.
std::expected<void, CommandRefusal> setFpsFromVideo(EditSession &session, std::int64_t videoMs,
                                                    const LineVisible &visible = {});
// Every time scaled by oldFps / newFps. A MicroDVD Document recomputes its frames from its own
// rate and is refused while that rate is unknown.
std::expected<void, CommandRefusal> setNewFps(EditSession &session, double oldFps, double newFps);

// A stable sort of every Line or of the selected Lines (hidden ones too) among their own rows.
// Ties go by End for Start, by Start otherwise. The selection stays on the same rows. An order
// that does not change adds no step.
enum class SortKey { Start, End, Style, Actor, Effect, Layer };
// Locale collation for Style, Actor and Effect (<0, 0, >0); bytes when empty.
using TextCompare = std::function<int(std::u8string_view, std::u8string_view)>;
std::expected<void, CommandRefusal> sortLines(EditSession &session, SortKey key, bool selectedOnly,
                                              const TextCompare &compare = {});

} // namespace hikari::application
