#pragma once

// Grid filtering (G8; legacy SubsGridFiltering at 20d647c4). Filtering sets
// each Line's visibility, which is Document content (the [hidden]/[visible]
// Actor markers), so each filter is an undo step: "Filtering", or "Removing
// filtering" for Turn off filtering. Selected LineIds stay selected (accepted
// contract); a hidden active Line gives way to the nearest shown one.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <expected>
#include <string>
#include <vector>

namespace hikari::application {

// GRID_FILTER_BY bits (legacy FILTER_BY_*).
namespace filter_by {
inline constexpr int Styles = 1, Selections = 2, Comments = 4, Unconfirmed = 8, Untranslated = 16;
}

struct FilterSettings {
    int filterBy = 0;                   // GRID_FILTER_BY
    std::vector<std::u8string> styles;  // GRID_FILTER_STYLES
    bool invert = false;                // GRID_FILTER_INVERTED ("Reverse filtering")
    bool addToFilter = false;           // GRID_ADD_TO_FILTER ("Do not reset previous filtering")
};

// GRID_FILTER: hides the Lines the settings name (styles, the shown
// selection, comments; in translation mode everything but the unconfirmed or
// untranslated Lines), or the others when inverted. `afterLoad` is
// GRID_FILTER_AFTER_LOAD's automatic filter, which never filters by selection.
std::expected<void, CommandRefusal> filterLines(EditSession &session, const FilterSettings &settings,
                                                const LineVisible &shown = {}, bool afterLoad = false);
// GRID_HIDE_SELECTED: hides the shown selected Lines, keeping earlier filtering.
std::expected<void, CommandRefusal> hideSelectedLines(EditSession &session, const LineVisible &shown = {});
// GRID_FILTER_BY_NOTHING: every Line shown again ("Removing filtering").
std::expected<void, CommandRefusal> turnOffFiltering(EditSession &session);
// The +/- mark after a shown Line (SubsGridFiltering::FilterPartial): the
// hidden block after it is revealed, or a revealed block hidden again. `row`
// is the Line's Document row; -1 for a block at the very start.
std::expected<void, CommandRefusal> toggleHiddenBlock(EditSession &session, int row);

// SubsFile::IsFiltered as legacy keeps it: Lines hidden outside a group, or a
// revealed block.
bool isFiltered(const core::Document &document);
// SubsFile::CheckIfHasHiddenBlock for the Line at `row` (-1 before the first):
// 1 a hidden block follows (+), 2 a revealed block follows (-), 0 none.
int hiddenBlockAfter(const core::Document &document, int row);

} // namespace hikari::application
