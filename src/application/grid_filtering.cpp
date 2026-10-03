#include "hikari/application/grid_filtering.h"

#include "hikari/core/style.h"

#include <algorithm>
#include <map>
#include <set>

namespace hikari::application {

namespace {

using core::LineVisibility;

std::vector<const core::LineRecord *> linesOf(const EditSession &s)
{
    const auto lines = s.document().lines();
    return {lines.begin(), lines.end()};
}

// legacy isVisible is truthy for VISIBLE and VISIBLE_BLOCK.
bool shownFlag(LineVisibility v)
{
    return v != LineVisibility::Hidden;
}

// Applies the visibilities that changed as one step; nothing changed, no step.
std::expected<void, CommandRefusal> applyVisibility(EditSession &session, const std::string &name,
                                                    const std::map<std::uint64_t, LineVisibility> &target)
{
    std::vector<std::pair<core::LineId, LineVisibility>> changes;
    for (const auto *l : linesOf(session))
        if (const auto it = target.find(l->id.value); it != target.end() && it->second != l->visibility)
            changes.emplace_back(l->id, it->second);
    if (changes.empty())
        return {};
    std::set<core::LineId> touched;
    for (const auto &c : changes)
        touched.insert(c.first);
    const auto ran = session.run(Command{name, session.revision(), touched, [&](core::Document &d) {
                                             for (const auto &[id, v] : changes)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) { l.visibility = v; }))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    // A hidden active Line gives way to the nearest shown Line (ties: the
    // following one); the selection itself is kept.
    Selection selection = session.selection();
    const auto lines = linesOf(session);
    if (selection.active) {
        int row = -1;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id == *selection.active)
                row = static_cast<int>(i);
        if (row >= 0 && !shownFlag(lines[static_cast<std::size_t>(row)]->visibility)) {
            const int n = static_cast<int>(lines.size());
            for (int distance = 1; distance < n; ++distance) {
                bool found = false;
                for (const int candidate : {row + distance, row - distance})
                    if (candidate >= 0 && candidate < n && shownFlag(lines[static_cast<std::size_t>(candidate)]->visibility)) {
                        selection.active = lines[static_cast<std::size_t>(candidate)]->id;
                        found = true;
                        break;
                    }
                if (found)
                    break;
            }
            session.setSelection(std::move(selection));
        }
    }
    return {};
}

// The "keep earlier filtering" pass (legacy addToFilter and HideSelections):
// a Line bordering a revealed block joins it. Legacy compares the visibility
// the Lines had before this filter (it keeps pointers to the originals).
// HideSelections also counts the Lines it hides as the previous Line;
// Filter does not.
void keepBlocks(const std::vector<const core::LineRecord *> &lines, std::map<std::uint64_t, LineVisibility> &target,
                const std::vector<bool> &hideNow, bool hiddenAreLast)
{
    std::optional<std::size_t> last;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (hideNow[i]) {
            if (hiddenAreLast)
                last = i;
            continue;
        }
        const LineVisibility now = lines[i]->visibility;
        if (last && lines[*last]->visibility == LineVisibility::VisibleBlock && now == LineVisibility::Hidden)
            target[lines[i]->id.value] = LineVisibility::VisibleBlock;
        else if (last && lines[*last]->visibility == LineVisibility::Hidden && now == LineVisibility::VisibleBlock)
            target[lines[*last]->id.value] = LineVisibility::VisibleBlock;
        last = i;
    }
}

} // namespace

std::expected<void, CommandRefusal> filterLines(EditSession &session, const FilterSettings &settings,
                                                const LineVisible &shown, bool afterLoad)
{
    int filterBy = settings.filterBy;
    std::vector<std::u8string> styles;
    if (filterBy & filter_by::Styles) {
        // Only Styles the Document has count.
        const auto documentStyles = core::decodeStyles(session.document());
        for (const auto &name : settings.styles)
            if (std::any_of(documentStyles.begin(), documentStyles.end(), [&](const auto &s) { return s.name == name; }))
                styles.push_back(name);
        if (styles.empty()) {
            if (filterBy == filter_by::Styles)
                return {};
            filterBy ^= filter_by::Styles;
        }
    }
    const auto lines = linesOf(session);
    std::set<core::LineId> selected;
    if (filterBy & filter_by::Selections) {
        if (afterLoad)
            filterBy ^= filter_by::Selections;
        else
            for (const auto *l : lines)
                if (session.selection().selected.contains(l->id) && shownFlag(l->visibility) && (!shown || shown(l->id)))
                    selected.insert(l->id);
    }
    // SubsGridFiltering::CheckHiding: every criterion that does not hide the
    // Line clears its bit; a bit left over hides it.
    auto hides = [&](const core::LineRecord &l) {
        int result = filterBy;
        if (filterBy & filter_by::Selections) {
            if (selected.contains(l.id))
                return true;
            result ^= filter_by::Selections;
        }
        if (filterBy & filter_by::Styles) {
            if (std::find(styles.begin(), styles.end(), l.style) != styles.end())
                return true;
            result ^= filter_by::Styles;
        }
        if ((filterBy & filter_by::Comments) && !l.comment)
            result ^= filter_by::Comments;
        if ((filterBy & filter_by::Unconfirmed) && l.unconfirmed) {
            result ^= filter_by::Unconfirmed;
            if (filterBy & filter_by::Untranslated)
                result ^= filter_by::Untranslated;
        }
        if ((filterBy & filter_by::Untranslated) && l.translation.empty()) {
            result ^= filter_by::Untranslated;
            if (filterBy & filter_by::Unconfirmed)
                result ^= filter_by::Unconfirmed;
        }
        return result != 0;
    };
    std::map<std::uint64_t, LineVisibility> target;
    std::vector<bool> hideNow(lines.size(), false);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const bool hide = hides(*lines[i]);
        if (hide != settings.invert) {
            target[lines[i]->id.value] = LineVisibility::Hidden;
            hideNow[i] = true;
        } else if (!settings.addToFilter && lines[i]->visibility != LineVisibility::Visible) {
            target[lines[i]->id.value] = LineVisibility::Visible;
        }
    }
    if (settings.addToFilter)
        keepBlocks(lines, target, hideNow, false);
    return applyVisibility(session, "Filtering", target);
}

std::expected<void, CommandRefusal> hideSelectedLines(EditSession &session, const LineVisible &shown)
{
    const auto lines = linesOf(session);
    std::map<std::uint64_t, LineVisibility> target;
    std::vector<bool> hideNow(lines.size(), false);
    bool any = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto *l = lines[i];
        // Legacy GetSelections: the selected Lines that are shown.
        if (session.selection().selected.contains(l->id) && shownFlag(l->visibility) && (!shown || shown(l->id))) {
            target[l->id.value] = LineVisibility::Hidden;
            hideNow[i] = true;
            any = true;
        }
    }
    if (!any)
        return std::unexpected(CommandRefusal::Invalid);
    keepBlocks(lines, target, hideNow, true);
    return applyVisibility(session, "Filtering", target);
}

std::expected<void, CommandRefusal> turnOffFiltering(EditSession &session)
{
    std::map<std::uint64_t, LineVisibility> target;
    for (const auto *l : linesOf(session))
        if (l->visibility != LineVisibility::Visible)
            target[l->id.value] = LineVisibility::Visible;
    return applyVisibility(session, "Removing filtering", target);
}

std::expected<void, CommandRefusal> toggleHiddenBlock(EditSession &session, int row)
{
    const auto lines = linesOf(session);
    // FilterPartial: from the Line after `row` up to the next shown Line.
    // (Legacy's own first-row case only works around its scroll position.)
    const std::size_t from = static_cast<std::size_t>(std::max(row + 1, 0));
    std::map<std::uint64_t, LineVisibility> target;
    bool hide = true;
    bool started = false;
    for (std::size_t i = from; i < lines.size(); ++i) {
        const auto *l = lines[i];
        if (started && l->visibility == LineVisibility::Visible)
            break;
        if (!shownFlag(l->visibility))
            hide = false;
        target[l->id.value] = hide ? LineVisibility::Hidden : LineVisibility::VisibleBlock;
        started = true;
    }
    if (target.empty())
        return std::unexpected(CommandRefusal::Invalid);
    return applyVisibility(session, "Filtering", target);
}

bool isFiltered(const core::Document &document)
{
    for (const auto *l : document.lines())
        if (l->visibility != LineVisibility::Visible && l->group == core::GroupMarker::None)
            return true;
    return false;
}

int hiddenBlockAfter(const core::Document &document, int row)
{
    const auto lines = document.lines();
    const int size = static_cast<int>(lines.size());
    const int first = row + 1;
    if (first < size && lines[static_cast<std::size_t>(first)]->visibility == LineVisibility::VisibleBlock) {
        if (first == 0)
            return 2;
        return lines[static_cast<std::size_t>(first - 1)]->visibility != LineVisibility::VisibleBlock ? 2 : 0;
    }
    if (row >= size)
        return 0;
    int hidden = 0;
    for (int k = std::max(first, 0); k < size; ++k) {
        if (shownFlag(lines[static_cast<std::size_t>(k)]->visibility))
            return hidden ? 1 : 0;
        ++hidden;
    }
    return hidden ? 1 : 0;
}

} // namespace hikari::application
