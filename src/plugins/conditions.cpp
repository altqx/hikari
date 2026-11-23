#include "hikari/plugins/conditions.h"

#include <string_view>

namespace hikari::plugins {

namespace {

bool holds(std::string_view condition, const ConditionFacts &facts)
{
    if (condition == "document")
        return facts.document;
    if (condition == "selection")
        return facts.selectedLines > 0;
    if (condition == "multipleLines")
        return facts.selectedLines > 1;
    if (condition == "video")
        return facts.video;
    if (condition == "audio")
        return facts.audio;
    if (condition == "karaoke")
        return facts.karaoke;
    return condition == "translation" && facts.translation;
}

}

std::optional<std::string> failingCondition(const std::vector<std::string> &when, const ConditionFacts &facts)
{
    for (const std::string &condition : when) {
        const bool negated = condition.starts_with('!');
        if (holds(std::string_view(condition).substr(negated ? 1 : 0), facts) == negated)
            return condition;
    }
    return std::nullopt;
}

}
