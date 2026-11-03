#include "hikari/application/grid_groups.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/clipboard_rows.h"
#include "hikari/core/line_groups.h"

#include <algorithm>
#include <map>
#include <set>

namespace hikari::application {

namespace {

using core::GroupMarker;
using core::LineVisibility;
using Markers = std::vector<GroupMarker>;

std::vector<const core::LineRecord *> linesOf(const EditSession &s)
{
    const auto lines = s.document().lines();
    return {lines.begin(), lines.end()};
}

const core::LineRecord *find(const EditSession &s, core::LineId id)
{
    for (const auto *l : s.document().lines())
        if (l->id == id)
            return l;
    return nullptr;
}

bool isDescription(const EditSession &s, core::LineId id)
{
    const auto *l = find(s, id);
    return l && l->group == GroupMarker::Description;
}

std::set<core::LineId> allIds(const EditSession &s)
{
    std::set<core::LineId> out;
    for (const auto *l : s.document().lines())
        out.insert(l->id);
    return out;
}

bool regroup(core::Document &d, core::LineId id, const std::function<void(Markers &)> &edit,
             core::ChangeMark mark = core::ChangeMark::Changed)
{
    return d.editLine(id, [&](core::LineRecord &l) {
        auto markers = core::groupMemberships(l);
        edit(markers);
        core::setGroupMarkers(l, std::move(markers), l.group == GroupMarker::Description);
    }, mark);
}

bool anyClosedAt(const EditSession &s, const std::vector<core::LineId> &contents, std::size_t level)
{
    return std::ranges::any_of(contents, [&](core::LineId id) {
        return core::groupMemberships(*find(s, id))[level] == GroupMarker::Closed;
    });
}

void keepActiveShown(EditSession &session, core::LineId description)
{
    Selection selection = session.selection();
    if (const auto *active = selection.active ? find(session, *selection.active) : nullptr;
        active && active->visibility == LineVisibility::Hidden) {
        selection.active = description;
        session.setSelection(std::move(selection));
    }
}

struct NewGroup {
    std::vector<core::LineId> lines;
    std::size_t level = 0;
    Markers context;
    std::u8string style;
};

}

std::expected<void, CommandRefusal> makeGroups(EditSession &session, const LineVisible &shown)
{
    const auto lines = linesOf(session);
    const auto &selected = session.selection().selected;
    auto isShown = [&](const core::LineRecord &l) {
        return l.visibility != LineVisibility::Hidden && (!shown || shown(l.id));
    };
    // Hidden Lines are skipped, not run ends.
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    bool start = true;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!isShown(*lines[i]))
            continue;
        if (selected.contains(lines[i]->id)) {
            if (start)
                runs.emplace_back(i, i);
            runs.back().second = i;
            start = false;
        } else {
            start = true;
        }
    }
    if (runs.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<NewGroup> groups;
    for (auto [first, last] : runs) {
        const core::LineRecord *shallowest = nullptr;
        for (std::size_t i = first; i <= last; ++i)
            if (isShown(*lines[i]) && (!shallowest || core::groupDepth(*lines[i]) < core::groupDepth(*shallowest)))
                shallowest = lines[i];
        NewGroup group{{}, core::groupDepth(*shallowest), core::groupMemberships(*shallowest), lines[first]->style};
        while (last + 1 < lines.size() && !isShown(*lines[last + 1]) && core::groupDepth(*lines[last + 1]) > group.level)
            ++last;
        for (std::size_t i = first; i <= last; ++i) {
            const auto depth = core::groupDepth(*lines[i]);
            if (depth > group.level || (depth == group.level && isShown(*lines[i])))
                group.lines.push_back(lines[i]->id);
        }
        groups.push_back(std::move(group));
    }
    std::optional<core::LineId> firstDescription;
    const auto ran = session.run(Command{"Adding tree", session.revision(), allIds(session), [&](core::Document &d) {
                                             for (const auto &group : groups) {
                                                 core::LineRecord description;
                                                 description.comment = true;
                                                 description.style = group.style;
                                                 core::setGroupMarkers(description, group.context, true);
                                                 const auto id = d.insertLineBefore(group.lines.front(), description,
                                                                                    core::ChangeMark::Kept);
                                                 if (!id)
                                                     return false;
                                                 if (!firstDescription)
                                                     firstDescription = id;
                                                 for (const auto member : group.lines)
                                                     if (!regroup(d, member, [&](Markers &m) {
                                                             m.insert(m.begin() + static_cast<std::ptrdiff_t>(group.level),
                                                                      GroupMarker::Closed);
                                                         }, core::ChangeMark::Kept))
                                                         return false;
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    keepActiveShown(session, *firstDescription);
    return {};
}

std::expected<void, CommandRefusal> toggleGroup(EditSession &session, core::LineId description)
{
    if (!isDescription(session, description))
        return std::unexpected(CommandRefusal::Invalid);
    const auto contents = core::groupContents(session.document(), description);
    if (contents.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto level = core::groupDepth(*find(session, description));
    const auto ran = session.run(Command{"Opening or closing tree", session.revision(), {contents.begin(), contents.end()},
                                         [&](core::Document &d) {
                                             for (const auto id : contents)
                                                 if (!regroup(d, id, [&](Markers &m) {
                                                         m[level] = m[level] == GroupMarker::Closed ? GroupMarker::Opened
                                                                                                    : GroupMarker::Closed;
                                                     }, core::ChangeMark::Kept))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    keepActiveShown(session, description);
    return {};
}

std::expected<void, CommandRefusal> renameGroup(EditSession &session, core::LineId description, std::u8string text)
{
    if (!isDescription(session, description) || text.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto ran = session.run(Command{"Setting tree description", session.revision(), {description},
                                         [&](core::Document &d) { return d.setLineText(description, text); }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

std::expected<void, CommandRefusal> removeGroup(EditSession &session, core::LineId description)
{
    if (!isDescription(session, description))
        return std::unexpected(CommandRefusal::Invalid);
    const auto contents = core::groupContents(session.document(), description);
    const auto level = static_cast<std::ptrdiff_t>(core::groupDepth(*find(session, description)));
    std::set<core::LineId> touched(contents.begin(), contents.end());
    touched.insert(description);
    const auto ran = session.run(Command{"Removing tree", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto id : contents)
                                                 if (!regroup(d, id, [&](Markers &m) { m.erase(m.begin() + level); }))
                                                     return false;
                                             return d.removeLine(description);
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    Selection selection = session.selection();
    selection.selected.erase(description);
    if (selection.active == description)
        selection.active = contents.empty() ? std::nullopt : std::optional(contents.front());
    if (selection.anchor == description)
        selection.anchor = selection.active;
    session.setSelection(std::move(selection));
    return {};
}

std::expected<void, CommandRefusal> selectGroup(EditSession &session, core::LineId description)
{
    if (!isDescription(session, description))
        return std::unexpected(CommandRefusal::Invalid);
    const auto contents = core::groupContents(session.document(), description);
    if (contents.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto level = core::groupDepth(*find(session, description));
    if (anyClosedAt(session, contents, level)) {
        const auto ran = session.run(Command{"Opening or closing tree", session.revision(),
                                             {contents.begin(), contents.end()}, [&](core::Document &d) {
                                                 for (const auto id : contents)
                                                     if (!regroup(d, id, [&](Markers &m) { m[level] = GroupMarker::Opened; },
                                                                  core::ChangeMark::Kept))
                                                         return false;
                                                 return true;
                                             }});
        if (!ran)
            return std::unexpected(ran.error());
    }
    Selection selection;
    selection.selected = {contents.begin(), contents.end()};
    selection.active = selection.anchor = session.selection().active ? session.selection().active : contents.front();
    session.setSelection(std::move(selection));
    return {};
}

std::expected<void, CommandRefusal> addLinesToGroup(EditSession &session, core::LineId description)
{
    if (!isDescription(session, description))
        return std::unexpected(CommandRefusal::Invalid);
    const auto &document = session.document();
    const auto *target = find(session, description);
    const auto level = core::groupDepth(*target);
    const auto contents = core::groupContents(document, description);
    const std::set<core::LineId> inGroup(contents.begin(), contents.end());
    Markers context = core::groupMemberships(*target);
    context.push_back(anyClosedAt(session, contents, level) ? GroupMarker::Closed : GroupMarker::Opened);
    const auto owners = core::groupOwners(document);
    auto ownerOf = [&](core::LineId id) {
        const auto it = owners.find(id.value);
        return it == owners.end() ? std::nullopt : it->second;
    };
    const auto targetOwner = ownerOf(description);
    std::vector<core::LineId> before, after;
    std::set<core::LineId> taken;
    std::map<core::LineId, Markers> rebased;
    bool passed = false;
    for (const auto *l : linesOf(session)) {
        if (l->id == description) {
            passed = true;
            continue;
        }
        if (!session.selection().selected.contains(l->id) || inGroup.contains(l->id) || taken.contains(l->id))
            continue;
        std::vector<core::LineId> unit{l->id};
        if (l->group == GroupMarker::Description) {
            const auto inner = core::groupContents(document, l->id);
            unit.insert(unit.end(), inner.begin(), inner.end());
        }
        if (std::ranges::contains(unit, description))
            return std::unexpected(CommandRefusal::Invalid);
        taken.insert(unit.begin(), unit.end());
        (passed ? after : before).insert((passed ? after : before).end(), unit.begin(), unit.end());
        if (ownerOf(l->id) != targetOwner)
            continue;
        const auto base = static_cast<std::ptrdiff_t>(core::groupDepth(*l));
        for (const auto id : unit) {
            const auto own = core::groupMemberships(*find(session, id));
            Markers markers = context;
            markers.insert(markers.end(), own.begin() + base, own.end());
            rebased.emplace(id, std::move(markers));
        }
    }
    if (before.empty() && after.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touched(taken);
    touched.insert(description);
    std::vector<core::LineId> order;
    const auto lastOfGroup = contents.empty() ? description : contents.back();
    for (const auto *l : linesOf(session)) {
        if (taken.contains(l->id))
            continue;
        order.push_back(l->id);
        if (l->id == description)
            order.insert(order.end(), before.begin(), before.end());
        if (l->id == lastOfGroup)
            order.insert(order.end(), after.begin(), after.end());
    }
    const auto ran = session.run(Command{"Adding line to tree", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto &[id, markers] : rebased)
                                                 if (!regroup(d, id, [&](Markers &m) { m = markers; }))
                                                     return false;
                                             std::vector<core::LineId> current;
                                             for (const auto *l : d.lines())
                                                 current.push_back(l->id);
                                             for (std::size_t k = 0; k < order.size(); ++k) {
                                                 if (current[k] == order[k])
                                                     continue;
                                                 if (!d.moveLine(order[k], current[k]))
                                                     return false;
                                                 current.erase(std::find(current.begin() + static_cast<std::ptrdiff_t>(k),
                                                                         current.end(), order[k]));
                                                 current.insert(current.begin() + static_cast<std::ptrdiff_t>(k), order[k]);
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    keepActiveShown(session, description);
    return {};
}

std::u8string copyGroup(const EditSession &session, core::LineId description)
{
    if (!isDescription(session, description))
        return {};
    std::vector<core::LineId> ids{description};
    const auto contents = core::groupContents(session.document(), description);
    ids.insert(ids.end(), contents.begin(), contents.end());
    return core::clipboardRows(session.document(), ids, translationMode(session));
}

}
