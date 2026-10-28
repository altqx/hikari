#include "hikari/application/grid_selection.h"

#include <algorithm>

namespace hikari::application {

GridSelection::GridSelection(std::vector<core::LineId> document, std::vector<core::LineId> displayed)
    : m_document(std::move(document)), m_displayed(std::move(displayed))
{
}

int GridSelection::documentIndex(core::LineId id) const
{
    const auto it = std::find(m_document.begin(), m_document.end(), id);
    return it == m_document.end() ? -1 : static_cast<int>(it - m_document.begin());
}

int GridSelection::displayedIndex(core::LineId id) const
{
    const auto it = std::find(m_displayed.begin(), m_displayed.end(), id);
    return it == m_displayed.end() ? -1 : static_cast<int>(it - m_displayed.begin());
}

std::vector<core::LineId> GridSelection::interval(core::LineId a, core::LineId b) const
{
    int i = documentIndex(a), j = documentIndex(b);
    if (i < 0 || j < 0)
        return {b};
    if (i > j)
        std::swap(i, j);
    return {m_document.begin() + i, m_document.begin() + j + 1};
}

std::optional<core::LineId> GridSelection::displayedFrom(std::optional<core::LineId> origin, int rows) const
{
    if (m_displayed.empty())
        return std::nullopt;
    int at = origin ? displayedIndex(*origin) : -1;
    if (at < 0) {
        // A hidden or missing origin: the nearest shown Line in Document order.
        if (origin) {
            const int doc = documentIndex(*origin);
            for (int k = 0; k < static_cast<int>(m_displayed.size()); ++k)
                if (documentIndex(m_displayed[static_cast<std::size_t>(k)]) >= doc) {
                    at = rows > 0 ? k - 1 : k;
                    break;
                }
            if (at < 0)
                at = static_cast<int>(m_displayed.size());
        } else {
            at = rows > 0 ? -1 : 0;
        }
    }
    const long long target = std::clamp<long long>(static_cast<long long>(at) + rows, 0,
                                                   static_cast<long long>(m_displayed.size()) - 1);
    return m_displayed[static_cast<std::size_t>(target)];
}

Selection GridSelection::plain(const Selection &current, core::LineId destination) const
{
    Selection s = current;
    s.active = destination;
    s.selected = {destination};
    s.anchor = destination;
    s.extent.reset();
    return s;
}

Selection GridSelection::shiftKey(const Selection &current, int rows) const
{
    if (!current.active)
        return current;
    Selection s = current;
    // A fresh keyboard range starts from the active Line.
    if (!s.extent)
        s.extent = s.anchor = s.active;
    core::LineId rangeFrom = *s.active, rangeTo = *s.extent;
    if (m_changeActive) {
        const auto dest = displayedFrom(s.active, rows);
        if (!dest)
            return current;
        rangeFrom = *dest;
        s.active = *dest;
    } else {
        const auto dest = displayedFrom(s.extent, rows);
        if (!dest)
            return current;
        s.extent = s.anchor = *dest;
        rangeTo = *dest;
    }
    const auto range = interval(rangeFrom, rangeTo);
    s.selected = {range.begin(), range.end()};
    return s;
}

Selection GridSelection::ctrlClick(const Selection &current, core::LineId line) const
{
    Selection s = current;
    s.anchor = line;
    // Legacy: Ctrl+click on the active Line when it alone is selected does nothing.
    if (!(current.active == line && current.selected.size() == 1 && current.selected.contains(line))) {
        if (s.selected.contains(line))
            s.selected.erase(line);
        else
            s.selected.insert(line);
        if (s.selected.empty() && s.active)
            s.selected.insert(*s.active);
    }
    if (m_changeActive)
        s.active = line;
    return s;
}

Selection GridSelection::shiftClick(const Selection &current, core::LineId destination, bool add) const
{
    Selection s = current;
    const core::LineId from = s.anchor ? *s.anchor : (s.active ? *s.active : destination);
    const auto range = interval(from, destination);
    if (!add)
        s.selected.clear();
    s.selected.insert(range.begin(), range.end());
    s.anchor = from;
    s.extent = from; // keyboard selection continues from where the mouse was last used
    if (m_changeActive)
        s.active = destination;
    return s;
}

Selection GridSelection::selectAll(const Selection &current) const
{
    Selection s = current;
    s.selected = {m_document.begin(), m_document.end()};
    return s;
}

} // namespace hikari::application
