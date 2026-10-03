// G8: Grid filtering (legacy SubsGridFiltering and SubsFile::
// CheckIfHasHiddenBlock at 20d647c4) on Line visibility, one undo step each,
// keeping the selection (accepted contract).

#include "hikari/application/grid_filtering.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kFour =
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "Style: Sign,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "\n[Events]\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
    "Comment: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,b\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,c\n"
    "Dialogue: 0,0:00:07.00,0:00:08.00,Sign,,0,0,0,,d\n";

struct FilterTest : ::testing::Test {
    EditSession session{load(kFour)};
    core::LineId a{1}, b{2}, c{3}, d{4};
    std::string hidden() const
    {
        std::string out;
        for (const auto *l : session.document().lines())
            out += l->visibility == core::LineVisibility::Hidden    ? 'h'
                   : l->visibility == core::LineVisibility::Visible ? 'v'
                                                                    : 'b';
        return out;
    }
    void select(std::set<core::LineId> lines, core::LineId active) { session.setSelection(Selection{active, lines, active, {}}); }
};

} // namespace

TEST_F(FilterTest, StylesCommentsAndInversion)
{
    const auto steps = session.historySize();
    ASSERT_TRUE(filterLines(session, {filter_by::Styles, {u8"Sign"}}));
    EXPECT_EQ(hidden(), "vhvh");
    EXPECT_EQ(session.history().back().name, "Filtering");
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_TRUE(isFiltered(session.document()));
    // A new filter resets the previous one unless asked not to.
    ASSERT_TRUE(filterLines(session, {filter_by::Comments}));
    EXPECT_EQ(hidden(), "vhvv");
    ASSERT_TRUE(filterLines(session, {filter_by::Comments, {}, true}));
    EXPECT_EQ(hidden(), "hvhh");
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(hidden(), "vhvh");
}

TEST_F(FilterTest, StylesTheDocumentLacksChangeNothing)
{
    const auto steps = session.historySize();
    ASSERT_TRUE(filterLines(session, {filter_by::Styles, {u8"Nowhere"}}));
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(hidden(), "vvvv");
}

TEST_F(FilterTest, SelectionFilterAndHideSelectedKeepTheSelection)
{
    select({a, c}, a);
    ASSERT_TRUE(filterLines(session, {filter_by::Selections}));
    EXPECT_EQ(hidden(), "hvhv");
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{a, c})); // kept
    EXPECT_EQ(session.selection().active, b); // the nearest shown Line
    // The automatic filter after loading never filters by selection.
    ASSERT_TRUE(turnOffFiltering(session));
    EXPECT_EQ(session.history().back().name, "Removing filtering");
    EXPECT_EQ(hidden(), "vvvv");
    EXPECT_FALSE(isFiltered(session.document()));
    ASSERT_TRUE(filterLines(session, {filter_by::Selections | filter_by::Comments}, {}, true));
    EXPECT_EQ(hidden(), "vhvv");
    // GRID_HIDE_SELECTED keeps earlier filtering.
    select({d}, d);
    ASSERT_TRUE(hideSelectedLines(session));
    EXPECT_EQ(hidden(), "vhvh");
    EXPECT_EQ(session.selection().active, c);
    select({b}, b); // only hidden Lines selected: nothing to hide
    EXPECT_FALSE(hideSelectedLines(session));
}

TEST_F(FilterTest, HiddenBlocksOpenAndCloseFromTheLineBeforeThem)
{
    select({b, c}, b);
    ASSERT_TRUE(hideSelectedLines(session));
    EXPECT_EQ(hidden(), "vhhv");
    EXPECT_EQ(hiddenBlockAfter(session.document(), 0), 1); // + after a
    EXPECT_EQ(hiddenBlockAfter(session.document(), 3), 0);
    ASSERT_TRUE(toggleHiddenBlock(session, 0));
    EXPECT_EQ(hidden(), "vbbv");
    EXPECT_EQ(hiddenBlockAfter(session.document(), 0), 2); // - after a
    EXPECT_TRUE(isFiltered(session.document()));
    ASSERT_TRUE(toggleHiddenBlock(session, 0));
    EXPECT_EQ(hidden(), "vhhv");
    // Hiding a Line after a revealed block: legacy compares the visibility the
    // Lines had before, so d is simply hidden and gets its own mark after c.
    ASSERT_TRUE(toggleHiddenBlock(session, 0));
    select({d}, d);
    ASSERT_TRUE(hideSelectedLines(session));
    EXPECT_EQ(hidden(), "vbbh");
    EXPECT_EQ(hiddenBlockAfter(session.document(), 2), 1);
}

TEST_F(FilterTest, TranslationFiltersShowOnlyTheLinesLeftToDo)
{
    core::Document document = load(kFour);
    document.setLineUnconfirmed(c, true);
    ASSERT_TRUE(document.editLine(a, [](core::LineRecord &l) { l.translation = u8"done"; }));
    ASSERT_TRUE(document.editLine(c, [](core::LineRecord &l) { l.translation = u8"draft"; }));
    EditSession tl{document};
    // "Show unconfirmed": everything else is hidden.
    ASSERT_TRUE(filterLines(tl, {filter_by::Unconfirmed}));
    std::string out;
    for (const auto *l : tl.document().lines())
        out += l->visibility == core::LineVisibility::Hidden ? 'h' : 'v';
    EXPECT_EQ(out, "hhvh");
    // With "Show untranslated" too, either keeps a Line shown.
    ASSERT_TRUE(filterLines(tl, {filter_by::Unconfirmed | filter_by::Untranslated}));
    out.clear();
    for (const auto *l : tl.document().lines())
        out += l->visibility == core::LineVisibility::Hidden ? 'h' : 'v';
    EXPECT_EQ(out, "hvvv");
}
