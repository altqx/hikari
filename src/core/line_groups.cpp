#include "hikari/core/line_groups.h"

namespace hikari::core {

namespace {

bool isMember(GroupMarker g)
{
    return g == GroupMarker::Opened || g == GroupMarker::Closed;
}

} // namespace

std::unordered_map<std::uint64_t, std::optional<LineId>> groupOwners(const Document &document)
{
    std::unordered_map<std::uint64_t, std::optional<LineId>> owners;
    std::optional<LineId> current;
    for (const auto *line : document.lines()) {
        if (line->group == GroupMarker::Description) {
            current = line->id;
        } else if (isMember(line->group)) {
            owners.emplace(line->id.value, current);
        } else {
            current.reset(); // an ordinary Line ends the run
        }
    }
    return owners;
}

std::vector<LineId> groupBreaks(const Document &before, const Document &after)
{
    const auto was = groupOwners(before);
    const auto now = groupOwners(after);
    std::vector<LineId> broken;
    for (const auto *line : after.lines()) {
        const auto current = now.find(line->id.value);
        if (current == now.end())
            continue; // not a member
        const auto old = was.find(line->id.value);
        const bool wasOrphan = old != was.end() && !old->second;
        if (!current->second) {
            // A new orphan, made or moved here by this command (an imported
            // one that stays where it was is not repaired or refused).
            if (!wasOrphan)
                broken.push_back(line->id);
        } else if (old != was.end() && old->second && *old->second != *current->second) {
            broken.push_back(line->id); // moved into another group
        }
    }
    return broken;
}

std::vector<LineId> groupMembers(const Document &document, LineId description)
{
    std::vector<LineId> out;
    for (const auto &[id, owner] : groupOwners(document))
        if (owner && *owner == description)
            out.push_back(LineId{id});
    // Document order.
    std::vector<LineId> ordered;
    for (const auto *line : document.lines())
        for (const auto id : out)
            if (id == line->id)
                ordered.push_back(id);
    return ordered;
}

} // namespace hikari::core
