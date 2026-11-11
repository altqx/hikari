#include "hikari/application/tag_removal.h"

#include <algorithm>
#include <set>

namespace hikari::application {

namespace tc = core::tagcleanup;

std::vector<TagRemovalChange> tagRemovalChanges(const EditSession &session, const tc::Options &options,
                                                TagRemovalLines lines, const LineVisible &visible)
{
    std::vector<TagRemovalChange> changes;
    const auto &all = session.document().lines();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const auto *l = all[i];
        const bool inScope = lines == TagRemovalLines::File
                                 ? !l->comment
                                 : session.selection().selected.contains(l->id) &&
                                       l->visibility != core::LineVisibility::Hidden && (!visible || visible(l->id));
        if (!inScope)
            continue;
        auto after = tc::apply(l->text, options);
        const bool deleted = options.cleanup.deleteEmptyLines && after.empty();
        if (after != l->text || deleted)
            changes.push_back(TagRemovalChange{l->id, static_cast<int>(i) + 1, l->text, std::move(after), deleted});
    }
    return changes;
}

std::expected<int, CommandRefusal> removeTags(EditSession &session, const tc::Options &options, TagRemovalLines lines,
                                              const LineVisible &visible)
{
    const auto changes = tagRemovalChanges(session, options, lines, visible);
    if (changes.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touched;
    std::set<core::LineId> deleted;
    for (const auto &c : changes) {
        touched.insert(c.id);
        if (c.deleted)
            deleted.insert(c.id);
    }
    const auto before = session.selection();
    std::size_t activeRow = 0;
    if (before.active)
        for (const auto *l : session.document().lines()) {
            if (l->id == *before.active)
                break;
            ++activeRow;
        }
    const auto ran = session.run(Command{"Remove tags", session.revision(), touched, [&](core::Document &d) {
        for (const auto &c : changes) {
            const bool done = c.deleted ? d.removeLine(c.id) : d.editLine(c.id, [&](core::LineRecord &l) { l.text = c.after; });
            if (!done)
                return false;
        }
        if (d.lines().empty()) {
            core::LineRecord line;
            line.style = u8"Default";
            line.end.value = core::DocumentTime(5'000'000);
            return d.appendLine(line, core::ChangeMark::Kept).has_value();
        }
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    if (!deleted.empty()) {
        const auto &now = session.document().lines();
        Selection selection;
        for (const auto id : before.selected)
            if (!deleted.contains(id))
                selection.selected.insert(id);
        if (before.active && !deleted.contains(*before.active))
            selection.active = before.active;
        else if (!now.empty())
            selection.active = now[std::min(activeRow, now.size() - 1)]->id;
        if (selection.active)
            selection.selected.insert(*selection.active);
        session.setSelection(std::move(selection));
    }
    return static_cast<int>(changes.size());
}

}
