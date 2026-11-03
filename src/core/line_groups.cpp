#include "hikari/core/line_groups.h"

#include <algorithm>
#include <span>

namespace hikari::core {

namespace {

bool isMember(GroupMarker g)
{
    return g == GroupMarker::Opened || g == GroupMarker::Closed;
}

using Enclosing = std::span<const std::optional<LineId>>;

template <class Visit>
void walkGroups(const Document &document, Visit visit)
{
    std::vector<std::optional<LineId>> open;
    for (const auto *line : document.lines()) {
        open.resize(groupDepth(*line));
        visit(*line, Enclosing(open));
        if (line->group == GroupMarker::Description)
            open.push_back(line->id);
    }
}

}

std::vector<GroupMarker> groupMemberships(const LineRecord &line)
{
    std::vector<GroupMarker> out = line.outerGroups;
    if (isMember(line.group))
        out.push_back(line.group);
    return out;
}

std::size_t groupDepth(const LineRecord &line)
{
    return line.outerGroups.size() + (isMember(line.group) ? 1 : 0);
}

void setGroupMarkers(LineRecord &line, std::vector<GroupMarker> memberships, bool description)
{
    const bool wasGrouped = line.group != GroupMarker::None;
    const bool closed = std::ranges::contains(memberships, GroupMarker::Closed);
    if (description) {
        line.group = GroupMarker::Description;
    } else if (memberships.empty()) {
        line.group = GroupMarker::None;
    } else {
        line.group = memberships.back();
        memberships.pop_back();
    }
    line.outerGroups = std::move(memberships);
    if (line.group != GroupMarker::None)
        line.visibility = closed ? LineVisibility::Hidden : LineVisibility::Visible;
    else if (wasGrouped && line.visibility == LineVisibility::Hidden)
        line.visibility = LineVisibility::Visible;
}

std::unordered_map<std::uint64_t, std::optional<LineId>> groupOwners(const Document &document)
{
    std::unordered_map<std::uint64_t, std::optional<LineId>> owners;
    walkGroups(document, [&](const LineRecord &line, Enclosing enclosing) {
        if (!enclosing.empty())
            owners.emplace(line.id.value, enclosing.back());
    });
    return owners;
}

std::vector<LineId> groupBreaks(const Document &before, const Document &after)
{
    const auto was = groupOwners(before);
    const auto now = groupOwners(after);
    std::unordered_map<std::uint64_t, const LineRecord *> previous;
    for (const auto *line : before.lines())
        previous.emplace(line->id.value, line);
    auto regrouped = [&](const LineRecord &line) {
        const auto old = previous.find(line.id.value);
        return old != previous.end() &&
               (old->second->group != line.group || old->second->outerGroups != line.outerGroups);
    };
    std::vector<LineId> broken;
    for (const auto *line : after.lines()) {
        const auto current = now.find(line->id.value);
        if (current == now.end())
            continue;
        const auto old = was.find(line->id.value);
        const bool wasOrphan = old != was.end() && !old->second;
        if (!current->second) {
            if (!wasOrphan)
                broken.push_back(line->id);
        } else if (old != was.end() && old->second && *old->second != *current->second && !regrouped(*line)) {
            broken.push_back(line->id);
        }
    }
    return broken;
}

std::vector<LineId> groupMembers(const Document &document, LineId description)
{
    std::vector<LineId> out;
    walkGroups(document, [&](const LineRecord &line, Enclosing enclosing) {
        if (!enclosing.empty() && enclosing.back() == description)
            out.push_back(line.id);
    });
    return out;
}

std::vector<LineId> groupContents(const Document &document, LineId description)
{
    std::vector<LineId> out;
    walkGroups(document, [&](const LineRecord &line, Enclosing enclosing) {
        if (std::ranges::contains(enclosing, std::optional(description)))
            out.push_back(line.id);
    });
    return out;
}

}
