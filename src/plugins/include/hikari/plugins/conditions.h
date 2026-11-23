#pragma once

#include <optional>
#include <string>
#include <vector>

namespace hikari::plugins {

// What a command's `when` conditions are checked against, for one window.
struct ConditionFacts {
    bool document = false;
    int selectedLines = 0;
    bool video = false;
    bool audio = false;
    bool karaoke = false;
    bool translation = false;
};

// The first of `when` that doesn't hold, as written ("selection", "!video"), or nothing when all hold.
std::optional<std::string> failingCondition(const std::vector<std::string> &when, const ConditionFacts &facts);

}
