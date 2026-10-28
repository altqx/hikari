#include "hikari/application/grid_groups.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/core/clipboard_rows.h"
#include "hikari/core/line_groups.h"

#include <algorithm>
#include <set>

namespace hikari::application {

namespace {

using core::GroupMarker;
using core::LineVisibility;

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

// Group members are closed and hidden, or open and shown.
void setOpen(core::LineRecord &l, bool open)
{
    l.group = open ? GroupMarker::Opened : GroupMarker::Closed;
    l.visibility = open ? LineVisibility::Visible : LineVisibility::Hidden;
}

// The active Line hidden by a group change gives way to the description.
void keepActiveShown(EditSession &session, core::LineId description)
{
    Selection selection = session.selection();
    if (const auto *active = selection.active ? find(session, *selection.active) : nullptr;
        active && active->visibility == LineVisibility::Hidden) {
        selection.active = description;
        session.setSelection(std::move(selection));
    }
}

} // namespace

std::expected<void, CommandRefusal> makeGroups(EditSession &session, const LineVisible &shown)
{
    const auto lines = linesOf(session);
    const auto &selected = session.selection().selected;
    // Runs of shown selected Lines; hidden Lines are skipped, not run ends.
    std::vector<std::vector<core::LineId>> runs;
    bool start = true;
    for (const auto *l : lines) {
        if (l->visibility == LineVisibility::Hidden || (shown && !shown(l->id)))
            continue;
        if (selected.contains(l->id)) {
            if (start)
                runs.emplace_back();
            runs.back().push_back(l->id);
            start = false;
        } else {
            start = true;
        }
    }
    if (runs.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::optional<core::LineId> firstDescription;
    const auto ran = session.run(Command{"Adding tree", session.revision(), allIds(session), [&](core::Document &d) {
                                             for (const auto &run : runs) {
                                                 const core::LineRecord *first = nullptr;
                                                 for (const auto *l : d.lines())
                                                     if (l->id == run.front())
                                                         first = l;
                                                 // Legacy: a new Dialogue, comment, End 0, the member's Style.
                                                 core::LineRecord description;
                                                 description.comment = true;
                                                 description.style = first->style;
                                                 description.group = GroupMarker::Description;
                                                 // E6: a new Dialogue (state 0); the members are
                                                 // copied with keepstate (SubsGridFiltering.cpp:177-191).
                                                 const auto id = d.insertLineBefore(run.front(), description,
                                                                                    core::ChangeMark::Kept);
                                                 if (!id)
                                                     return false;
                                                 if (!firstDescription)
                                                     firstDescription = id;
                                                 for (const auto member : run)
                                                     if (!d.editLine(member, [](core::LineRecord &l) { setOpen(l, false); },
                                                                     core::ChangeMark::Kept))
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
    const auto members = core::groupMembers(session.document(), description);
    if (members.empty())
        return std::unexpected(CommandRefusal::Invalid);
    // OpenCloseTree: each member flips; the group opens if any member was closed.
    const auto ran = session.run(Command{"Opening or closing tree", session.revision(), {members.begin(), members.end()},
                                         [&](core::Document &d) {
                                             for (const auto id : members)
                                                 // E6: OpenCloseTree changes the Dialogues in place.
                                                 if (!d.editLine(id, [](core::LineRecord &l) {
                                                         setOpen(l, l.group == GroupMarker::Closed);
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
    if (!isDescription(session, description) || text.empty()) // legacy OkClick needs a name
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
    const auto members = core::groupMembers(session.document(), description);
    std::set<core::LineId> touched(members.begin(), members.end());
    touched.insert(description);
    const auto ran = session.run(Command{"Removing tree", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto id : members)
                                                 if (!d.editLine(id, [](core::LineRecord &l) {
                                                         l.group = GroupMarker::None;
                                                         if (l.visibility == LineVisibility::Hidden)
                                                             l.visibility = LineVisibility::Visible;
                                                     }))
                                                     return false;
                                             return d.removeLine(description);
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    Selection selection = session.selection();
    selection.selected.erase(description);
    if (selection.active == description)
        selection.active = members.empty() ? std::nullopt : std::optional(members.front());
    if (selection.anchor == description)
        selection.anchor = selection.active;
    session.setSelection(std::move(selection));
    return {};
}

std::expected<void, CommandRefusal> selectGroup(EditSession &session, core::LineId description)
{
    if (!isDescription(session, description))
        return std::unexpected(CommandRefusal::Invalid);
    const auto members = core::groupMembers(session.document(), description);
    if (members.empty())
        return std::unexpected(CommandRefusal::Invalid);
    bool anyClosed = false;
    for (const auto id : members)
        anyClosed = anyClosed || find(session, id)->group == GroupMarker::Closed;
    if (anyClosed) {
        const auto ran = session.run(Command{"Opening or closing tree", session.revision(),
                                             {members.begin(), members.end()}, [&](core::Document &d) {
                                                 for (const auto id : members)
                                                     // E6: TreeSelect opens them in place.
                                                     if (!d.editLine(id, [](core::LineRecord &l) {
                                                             if (l.group == GroupMarker::Closed)
                                                                 setOpen(l, true);
                                                         }, core::ChangeMark::Kept))
                                                         return false;
                                                 return true;
                                             }});
        if (!ran)
            return std::unexpected(ran.error());
    }
    Selection selection;
    selection.selected = {members.begin(), members.end()};
    selection.active = selection.anchor = session.selection().active ? session.selection().active : members.front();
    session.setSelection(std::move(selection));
    return {};
}

std::expected<void, CommandRefusal> addLinesToGroup(EditSession &session, core::LineId description)
{
    if (!isDescription(session, description))
        return std::unexpected(CommandRefusal::Invalid);
    const auto members = core::groupMembers(session.document(), description);
    const std::set<core::LineId> inGroup(members.begin(), members.end());
    const bool closed = !members.empty() && find(session, members.front())->group == GroupMarker::Closed;
    std::vector<core::LineId> before, after;
    bool passed = false;
    for (const auto *l : linesOf(session)) {
        if (l->id == description) {
            passed = true;
            continue;
        }
        if (!session.selection().selected.contains(l->id) || inGroup.contains(l->id) ||
            l->group == GroupMarker::Description)
            continue;
        (passed ? after : before).push_back(l->id);
    }
    if (before.empty() && after.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touched(before.begin(), before.end());
    touched.insert(after.begin(), after.end());
    touched.insert(description);
    // Legacy TreeAddLines: Lines before the group go right after its
    // description, Lines after it right after its last member, in order.
    std::vector<core::LineId> order;
    const std::set<core::LineId> moving(touched.begin(), touched.end());
    const auto lastOfGroup = members.empty() ? description : members.back();
    for (const auto *l : linesOf(session)) {
        if (l->id != description && moving.contains(l->id))
            continue;
        order.push_back(l->id);
        if (l->id == description)
            order.insert(order.end(), before.begin(), before.end());
        if (l->id == lastOfGroup)
            order.insert(order.end(), after.begin(), after.end());
    }
    const auto ran = session.run(Command{"Adding line to tree", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto id : before)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) { setOpen(l, !closed); }))
                                                     return false;
                                             for (const auto id : after)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) { setOpen(l, !closed); }))
                                                     return false;
                                             // Row by row, the wanted Line goes before the one now there.
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
    const auto members = core::groupMembers(session.document(), description);
    ids.insert(ids.end(), members.begin(), members.end());
    return core::clipboardRows(session.document(), ids, translationMode(session));
}

} // namespace hikari::application
