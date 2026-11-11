#pragma once

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

// line0's Cascade Transforms: the Line's \t run back to back over `durationMs`.
std::string cascadeTransforms(std::string_view text, double durationMs);

}
