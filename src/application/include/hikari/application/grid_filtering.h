#pragma once

// Each filter is one undo step; the selection stays, a hidden active Line moves to the nearest shown one.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"

#include <expected>
#include <string>
#include <vector>

namespace hikari::application {

// grid.filterBy bits, as HikariSub legacy stores them.
namespace filter_by {
inline constexpr int Styles = 1, Selections = 2, Comments = 4, Unconfirmed = 8, Untranslated = 16;
}

struct FilterSettings {
    int filterBy = 0;
    std::vector<std::u8string> styles;
    bool invert = false;
    bool addToFilter = false;
};

// Hides the Lines the settings match, or the others when inverted; `afterLoad` never filters by selection.
std::expected<void, CommandRefusal> filterLines(EditSession &session, const FilterSettings &settings,
                                                const LineVisible &shown = {}, bool afterLoad = false);
// Only shown selected Lines count; earlier filtering is kept.
std::expected<void, CommandRefusal> hideSelectedLines(EditSession &session, const LineVisible &shown = {});
std::expected<void, CommandRefusal> turnOffFiltering(EditSession &session);
// Reveals the hidden block after Document row `row`, or hides a revealed one; -1 for the very start.
std::expected<void, CommandRefusal> toggleHiddenBlock(EditSession &session, int row);

// True when a Line outside a group is hidden or in a revealed block.
bool isFiltered(const core::Document &document);
// MUST KILL: int codes instead of an enum
// For the Line at `row` (-1 before the first): 1 a hidden block follows, 2 a revealed block, 0 none.
int hiddenBlockAfter(const core::Document &document, int row);

} // namespace hikari::application
