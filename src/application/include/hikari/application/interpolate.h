#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/style.h"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::interpolate {

struct LineIn {
    std::string text;
    core::StyleValues style;
    double durationMs = 0;
    // Where the Line sits without \pos or \move.
    double defaultX = 0, defaultY = 0;
};

struct Options {
    double accel = 1;
    bool shortestRotation = false;
    bool cascade = false;
    // Change ids (Change::id) left out.
    std::vector<std::string> excluded;
};

enum class Skip { None, DifferentShapes, RectAndDrawing, OneSidedClip, DifferentTransforms };

// A tag whose value differs between the first and the last Line, at one place in the text.
struct Change {
    std::string id; // "pos", "1c@0", "t@3": key and the character offset of its block
    std::string key; // tag_lerp's key, or "t"
    std::size_t at = 0; // characters into the text without tags
    std::string from, to; // as written: "\1c&H0000FF&"
    Skip skip = Skip::None;
    std::string name; // as the Lines spell it: "c" for key "1c"
    std::string before; // up to 8 characters of the text the block comes before; empty for the start block
};

enum class Problem { TooFewLines, NothingChanges };

struct Plan {
    std::vector<Change> changes;
    // The two ends' text without tags differ: only the start block counts.
    bool startBlockOnly = false;
    // One per Line of the run, in order.
    std::vector<std::string> texts;
};

std::expected<Plan, Problem> plan(std::span<const LineIn> lines, const Options &options);

// The shown selected Lines in Grid order, with their Style, length and default position.
struct Run {
    std::vector<core::LineId> ids;
    std::vector<LineIn> lines;
};
Run selectedRun(const EditSession &session, const LineVisible &visible = {});

// The plan's texts written to the run's Lines as one "Interpolate across Lines" step.
std::expected<void, CommandRefusal> applyPlan(EditSession &session, const Run &run, const Plan &plan);

// line0's Cascade Transforms: the Line's \t run back to back over `durationMs`.
std::string cascadeTransforms(std::string_view text, double durationMs);

}
