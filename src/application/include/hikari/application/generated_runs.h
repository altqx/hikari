#pragma once

// KP1 (#286): the generated Lines the Grid folds away (docs/qt/ux/workspaces.md,
// "Karaoke"). A karaoke effect writes them: the templater with Effect fx,
// Kara Effector with Effect "Effector [fx]". Folding is the Grid's view only;
// the Lines stay in the Document as they are.

#include "hikari/core/document.h"

#include <cstddef>
#include <vector>

namespace hikari::application {

// A Line a karaoke effect generated: its Effect is exactly "fx" or
// "Effector [fx]", Comment or not.
bool isFoldableGeneratedLine(const core::LineRecord &line);

// A run of consecutive generated Lines, by Document row.
struct GeneratedRun {
    std::size_t first = 0;
    std::size_t count = 0;
    friend bool operator==(const GeneratedRun &, const GeneratedRun &) = default;
};
// The runs in Document order.
std::vector<GeneratedRun> generatedRuns(const std::vector<const core::LineRecord *> &lines);

} // namespace hikari::application
