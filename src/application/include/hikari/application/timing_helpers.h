#pragma once

// Every time these write is the start of a frame on `timebase` (startTimeFor,
// truncated to 10 ms), the same rounding as frame shifts. Keyframes are frame
// numbers. A command that would change nothing is refused (Invalid) and records
// no step; the pending draft is committed first, as its own step.

#include "hikari/application/edit_session.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/application/shift_times.h"

#include <expected>
#include <optional>
#include <span>

namespace hikari::application {

enum class TimingEdge { Start, End };
enum class TimingToward { Previous, Next };

// Each selected Line's start (or end) moves to the keyframe strictly before or
// after the frame it starts (ends) on. A start stays before the end frame and an
// end after the start frame; a Line with no such keyframe keeps its times.
std::expected<void, CommandRefusal> snapToAdjacentKeyframe(EditSession &session, const LegacyTimebase &timebase,
                                                           std::span<const int> keyframes, TimingEdge edge,
                                                           TimingToward toward);

// Adding moves each selected Line's start earlier (end later) by `amountMs`,
// stopping at the previous (next) uncommented Line's end (start) and at 0.
// Removing does the reverse and keeps at least one frame.
std::expected<void, CommandRefusal> changeLead(EditSession &session, const LegacyTimebase &timebase, TimingEdge edge,
                                               bool add, int amountMs);

// Each selected Line's start (or end) moves by `frames` whole frames; at least
// one frame stays and a start stays at frame 0 or later.
std::expected<void, CommandRefusal> nudgeEdgeByFrames(EditSession &session, const LegacyTimebase &timebase,
                                                      TimingEdge edge, int frames);

// The previous uncommented Line's end becomes the active Line's start (or the
// next one's start becomes its end); the active Line keeps its times.
std::expected<void, CommandRefusal> extendNeighbour(EditSession &session, TimingToward toward);

enum class TimingReason {
    Kept,
    OnKeyframe,
    KeyframeBefore,
    KeyframeAfter,
    Linked,
    LeadIn,
    LeadOut,
};

struct TimingSuggestion {
    core::LineId line;
    int startBefore = 0, endBefore = 0;
    int startMs = 0, endMs = 0;
    TimingReason startReason = TimingReason::Kept, endReason = TimingReason::Kept;
    int startFrames = 0, endFrames = 0; // how far the snap moved, in frames
    std::optional<core::LineId> linked;  // the previous Line, whose end becomes startMs
    int linkedEndBefore = 0;
};

// The Timing assistant's suggestion for `line`, from the Timing postprocessor's
// rules that are on (`settings.postprocessor` bits 1, 2, 4, 8). Start: kept on a
// keyframe; else the keyframe within keyframeBeforeStart before it or
// keyframeAfterStart after it; else, when the previous uncommented Line ends
// within thresholdStart of it and not on a keyframe, the lead-in start with that
// Line linked to it; else the lead-in. End: kept on a keyframe; else the keyframe
// within keyframeBeforeEnd before or keyframeAfterEnd after it; else the
// lead-out. nullopt for a missing or commented Line.
std::optional<TimingSuggestion> suggestTiming(const core::Document &document, core::LineId line,
                                              const LegacyTimebase &timebase, std::span<const int> keyframes,
                                              const ShiftTimesSettings &settings);

// The suggestion, with `startMs`/`endMs` as given, in one step. Refused (Invalid)
// when the end is before the start.
std::expected<void, CommandRefusal> acceptTiming(EditSession &session, const TimingSuggestion &suggestion);

}
