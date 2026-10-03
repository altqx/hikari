#pragma once

// Grid selection gestures (G1; docs/qt/ux/grid-operations.md, legacy
// SubsGridWindow OnMouseEvent/OnKeyPress at 20d647c4). Destinations follow the
// displayed order; range membership is the inclusive Document-order interval
// between anchor and destination, hidden Lines included. Identities are
// stable LineIds, never row numbers.

#include "hikari/application/edit_session.h"

#include <vector>

namespace hikari::application {

class GridSelection {
public:
    // `document`: every Line in Document order. `displayed`: the Lines the
    // Grid shows, in displayed order (a filtered or grouped projection).
    GridSelection(std::vector<core::LineId> document, std::vector<core::LineId> displayed);

    // Legacy GRID_CHANGE_ACTIVE_ON_SELECTION (default true): Shift moves the
    // active Line and keeps the anchor; false moves the extent instead.
    void setChangeActiveOnSelection(bool on) { m_changeActive = on; }

    // A plain arrow, Page, Home/End or click: the destination alone, which
    // becomes active and the anchor.
    Selection plain(const Selection &current, core::LineId destination) const;
    // Shift with arrows, Page, Home or End: `rows` displayed rows from the
    // moving end (clamped to the displayed range).
    Selection shiftKey(const Selection &current, int rows) const;
    // A plain displayed destination `rows` away from the active Line, clamped.
    std::optional<core::LineId> displayedFrom(std::optional<core::LineId> origin, int rows) const;
    // Ctrl+click toggles the Line (never leaving nothing selected); with
    // change-active it also becomes active.
    Selection ctrlClick(const Selection &current, core::LineId line) const;
    // Shift+click replaces the selection with the anchor-to-destination
    // interval; Ctrl+Shift adds it. With change-active the destination becomes active.
    Selection shiftClick(const Selection &current, core::LineId destination, bool add) const;
    // Ctrl+A: every Line, hidden ones included.
    Selection selectAll(const Selection &current) const;

private:
    std::vector<core::LineId> interval(core::LineId a, core::LineId b) const; // Document order, inclusive
    int documentIndex(core::LineId id) const;
    int displayedIndex(core::LineId id) const;

    std::vector<core::LineId> m_document, m_displayed;
    bool m_changeActive = true;
};

} // namespace hikari::application
