#pragma once

// Destinations follow the displayed order; a range is the inclusive
// Document-order interval between anchor and destination, hidden Lines included.

#include "hikari/application/edit_session.h"

#include <vector>

namespace hikari::application {

class GridSelection {
public:
    // `document`: every Line in Document order. `displayed`: the Lines the
    // Grid shows, in displayed order (a filtered or grouped projection).
    GridSelection(std::vector<core::LineId> document, std::vector<core::LineId> displayed);

    // Default true: Shift moves the active Line and keeps the anchor; false
    // moves the extent instead.
    void setChangeActiveOnSelection(bool on) { m_changeActive = on; }

    // The destination alone, which becomes active and the anchor.
    Selection plain(const Selection &current, core::LineId destination) const;
    // `rows` displayed rows from the moving end, clamped to the displayed range.
    Selection shiftKey(const Selection &current, int rows) const;
    // A plain displayed destination `rows` away from the active Line, clamped.
    std::optional<core::LineId> displayedFrom(std::optional<core::LineId> origin, int rows) const;
    // Ctrl+click toggles the Line (never leaving nothing selected); with
    // change-active it also becomes active.
    Selection ctrlClick(const Selection &current, core::LineId line) const;
    // Shift+click replaces the selection with the anchor-to-destination
    // interval; Ctrl+Shift adds it. With change-active the destination becomes active.
    Selection shiftClick(const Selection &current, core::LineId destination, bool add) const;
    // Every Line, hidden ones included.
    Selection selectAll(const Selection &current) const;

private:
    std::vector<core::LineId> interval(core::LineId a, core::LineId b) const; // Document order, inclusive
    int documentIndex(core::LineId id) const;
    int displayedIndex(core::LineId id) const;

    std::vector<core::LineId> m_document, m_displayed;
    bool m_changeActive = true;
};

} // namespace hikari::application
