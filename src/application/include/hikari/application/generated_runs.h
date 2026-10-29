#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <vector>

namespace hikari::application {

// Written by the karaoke templater (Effect "fx") or Kara Effector ("Effector [fx]"); the templater skips Comments.
bool isFoldableGeneratedLine(const core::LineRecord &line);

struct GeneratedRun {
    std::size_t first = 0;
    std::size_t count = 0;
    friend bool operator==(const GeneratedRun &, const GeneratedRun &) = default;
};
std::vector<GeneratedRun> generatedRuns(const std::vector<const core::LineRecord *> &lines);

} // namespace hikari::application
