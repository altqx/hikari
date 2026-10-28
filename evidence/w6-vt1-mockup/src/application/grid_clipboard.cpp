#include "hikari/application/grid_clipboard.h"

#include <vector>

namespace hikari::application {

namespace {

std::vector<const core::LineRecord *> shownSelected(const EditSession &session, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *line : session.document().lines())
        if (session.selection().selected.contains(line->id) && (!visible || visible(line->id)))
            out.push_back(line);
    return out;
}

bool inGroup(core::GroupMarker marker)
{
    // Legacy treeState > TREE_DESCRIPTION: a group member, opened or closed.
    return marker == core::GroupMarker::Opened || marker == core::GroupMarker::Closed;
}

} // namespace

bool translationMode(const EditSession &session)
{
    return session.document().scriptInfo(u8"TLMode") == u8"Yes";
}

std::u8string copyRows(const EditSession &session)
{
    std::vector<core::LineId> ids;
    for (const auto *line : session.document().lines())
        if (session.selection().selected.contains(line->id))
            ids.push_back(line->id);
    return core::clipboardRows(session.document(), ids, translationMode(session));
}

std::u8string copyColumns(const EditSession &session, int columns)
{
    std::vector<core::LineId> ids;
    for (const auto *line : session.document().lines())
        if (session.selection().selected.contains(line->id))
            ids.push_back(line->id);
    return core::clipboardColumns(session.document(), ids, columns, translationMode(session));
}

std::expected<void, CommandRefusal> pasteRows(EditSession &session, std::u8string_view text, const LineVisible &visible,
                                              const core::PasteConversion &conversion)
{
    const auto selected = shownSelected(session, visible);
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    auto pasted = core::parseClipboardRows(text, session.document().format(), conversion);
    if (pasted.empty())
        return {};
    // The Line at the paste position and the one before it decide the group
    // and visibility of the pasted Lines.
    const auto lines = session.document().lines();
    std::size_t row = 0;
    while (lines[row]->id != selected.front()->id)
        ++row;
    const core::LineRecord *after = lines[row];
    const core::LineRecord *before = row > 0 ? lines[row - 1] : nullptr;
    const core::GroupMarker group = before && inGroup(before->group) && inGroup(after->group) ? before->group
                                    : inGroup(after->group)                                   ? after->group
                                                                                              : core::GroupMarker::None;
    // Legacy reads the visibility before the position from the wrong variable,
    // so only the Line after it counts (characterized).
    const core::LineVisibility visibility = after->visibility;
    for (auto &line : pasted) {
        if (group != core::GroupMarker::None)
            line.group = group;
        if (visibility != core::LineVisibility::Visible)
            line.visibility = visibility;
    }
    std::vector<core::LineId> added;
    const core::LineId at = after->id;
    const auto ran = session.run(Command{"Pasting lines", session.revision(), {at}, [&](core::Document &d) {
                                             added.clear();
                                             for (const auto &line : pasted) {
                                                 const auto id = d.insertLineBefore(at, line);
                                                 if (!id)
                                                     return false;
                                                 added.push_back(*id);
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    Selection selection;
    selection.active = selection.anchor = added.front();
    selection.selected = {added.begin(), added.end()};
    session.setSelection(std::move(selection));
    return {};
}

std::expected<void, CommandRefusal> pasteColumns(EditSession &session, std::u8string_view text, int columns,
                                                 const LineVisible &visible, const core::PasteConversion &conversion)
{
    const auto selected = shownSelected(session, visible);
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto pasted = core::parseClipboardRows(text, session.document().format(), conversion,
                                                 (columns & core::column::Translation) != 0);
    std::vector<std::pair<core::LineId, const core::LineRecord *>> changes;
    for (std::size_t i = 0; i < pasted.size() && i < selected.size(); ++i)
        changes.emplace_back(selected[i]->id, &pasted[i]);
    // Legacy ChangeCell: only the chosen fields.
    auto apply = [columns](core::LineRecord &l, const core::LineRecord &from) {
        using namespace core::column;
        if (columns & Layer)
            l.layer.value = from.layer.value;
        if (columns & Start) {
            l.start.value = from.start.value;
            l.startFrame = from.startFrame;
        }
        if (columns & End) {
            l.end.value = from.end.value;
            l.endFrame = from.endFrame;
        }
        if (columns & Style)
            l.style = from.style;
        if (columns & Actor)
            l.actor = from.actor;
        if (columns & MarginLeft)
            l.marginLeft.value = from.marginLeft.value;
        if (columns & MarginRight)
            l.marginRight.value = from.marginRight.value;
        if (columns & MarginVertical)
            l.marginVertical.value = from.marginVertical.value;
        if (columns & Effect)
            l.effect = from.effect;
        if (columns & Text)
            l.text = from.text;
        if (columns & Translation)
            l.translation = from.translation;
    };
    std::vector<core::LineId> touched;
    for (const auto &[id, from] : changes)
        touched.push_back(id);
    if (!changes.empty()) {
        const auto ran = session.run(Command{"Pasting columns", session.revision(), {touched.begin(), touched.end()}, [&](core::Document &d) {
                                                 for (const auto &[id, from] : changes)
                                                     if (!d.editLine(id, [&](core::LineRecord &l) { apply(l, *from); }))
                                                         return false;
                                                 return true;
                                             }});
        if (!ran)
            return std::unexpected(ran.error());
    }
    Selection selection = session.selection();
    selection.active = selected.front()->id;
    session.setSelection(std::move(selection));
    return {};
}

} // namespace hikari::application
