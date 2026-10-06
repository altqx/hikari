// R2: the reference tray's linked matching against legacy
// SubsGridPreview::SeekForOccurences and NewSeeking (SubsGridPreview.cpp at
// 20d647c4). Expected values are worked through the legacy loop by hand;
// each case names the lines it follows.

#include "hikari/application/reference_navigation.h"
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

std::string events(std::string_view body)
{
    return "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
           std::string(body);
}

// The reference: rows 0-5 (legacy keys), times in ms.
//   0  1000- 2000   1  2000- 3000   2  2500- 4000
//   3  5000- 6000   4  5500- 7000   5 10000-11000
const std::string kReference = events("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                      "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,b\n"
                                      "Dialogue: 0,0:00:02.50,0:00:04.00,Default,,0,0,0,,c\n"
                                      "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,d\n"
                                      "Dialogue: 0,0:00:05.50,0:00:07.00,Default,,0,0,0,,e\n"
                                      "Dialogue: 0,0:00:10.00,0:00:11.00,Default,,0,0,0,,f\n");

using Runs = std::vector<Occurrence>;

} // namespace

// SubsGridPreview.cpp:876: Start < endTime && End > startTime. A Line that
// ends where the editing Line starts (row 0) or starts where it ends does not
// overlap; consecutive overlapping rows make one run (lineRangeLen).
TEST(ReferenceNavigation, OverlappingLinesInRuns)
{
    const auto doc = load(kReference);
    EXPECT_EQ(seekOccurrences(doc, 2000, 3000), (Runs{{1, 2}}));
    EXPECT_EQ(seekOccurrences(doc, 1500, 5500), (Runs{{0, 4}})); // row 4 starts at 5500: no
    EXPECT_EQ(seekOccurrences(doc, 5000, 6000), (Runs{{3, 2}}));
    EXPECT_EQ(seekOccurrences(doc, 0, 20000), (Runs{{0, 6}}));
}

// SubsGridPreview.cpp:875 `if (!dial->isVisible) continue;`: a Hidden Line is
// passed over, so it ends a run (lastLine + 1 != j), and is never the nearest.
TEST(ReferenceNavigation, HiddenLinesSplitRunsAndAreNeverChosen)
{
    const auto doc = load(events("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                 "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[hidden],0,0,0,,hidden\n"
                                 "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,c\n"
                                 "Dialogue: 0,0:00:01.50,0:00:03.00,Default,,0,0,0,,d\n"
                                 "Dialogue: 0,0:00:09.00,0:00:09.50,Default,[hidden],0,0,0,,far hidden\n"));
    ASSERT_EQ(doc.lines()[1]->visibility, core::LineVisibility::Hidden);
    EXPECT_EQ(seekOccurrences(doc, 1000, 2000), (Runs{{0, 1}, {2, 2}}));
    // Nothing overlaps 8000-9200; the hidden row 4 (9000) would be nearest.
    EXPECT_EQ(seekOccurrences(doc, 8000, 9200), (Runs{{3, 0}}));
}

// SubsGridPreview.cpp:880-895 and 897-917: with no overlap, the visible Line
// whose start is nearest the editing Line's start, or whose end is nearest
// its end when that is strictly closer, with lineRangeLen 0.
TEST(ReferenceNavigation, NoOverlapGivesLegacysNearestLine)
{
    const auto doc = load(kReference);
    // 8000-9000: starts 5500 (2500 before) and 10000 (2000 after): 10000,
    // row 5; ends 7000 (2000) and 11000 (2000): the earlier, row 4; the start
    // is not strictly farther than the end (2000 > 2000 is false): row 5.
    EXPECT_EQ(seekOccurrences(doc, 8000, 9000), (Runs{{5, 0}}));
    // 7200-9900: start 5500 (1700) beats 10000 (2800): row 4; end 11000
    // (1100) beats 7000 (2900): row 5; 1700 > 1100, so the end's row 5.
    EXPECT_EQ(seekOccurrences(doc, 7200, 9900), (Runs{{5, 0}}));
    // 7000-9500: start 5500 (1500), row 4; end 11000 (1500), row 5; a tie
    // keeps the start's row 4.
    EXPECT_EQ(seekOccurrences(doc, 7000, 9500), (Runs{{4, 0}}));
    // Before every Line (no start before it, startMax < 0): the first start
    // after it, row 0; no end before it either: the first end after, row 0.
    EXPECT_EQ(seekOccurrences(doc, 0, 500), (Runs{{0, 0}}));
    // After every Line: the last start (row 5) and the last end (row 5).
    EXPECT_EQ(seekOccurrences(doc, 12000, 13000), (Runs{{5, 0}}));
}

// Legacy left INT_MAX/-1 in place when every Line is hidden and picked key 0;
// a Document with no Lines has nothing to show.
TEST(ReferenceNavigation, EveryLineHiddenOrNone)
{
    const auto hidden = load(events("Dialogue: 0,0:00:01.00,0:00:02.00,Default,[hidden],0,0,0,,a\n"
                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[hidden],0,0,0,,b\n"));
    EXPECT_EQ(seekOccurrences(hidden, 1000, 2000), (Runs{{0, 0}}));
    EXPECT_TRUE(seekOccurrences(core::Document{}, 1000, 2000).empty());
}

// The editing Line's mstime (SubsGridPreview.cpp:856-858): centiseconds as
// milliseconds.
TEST(ReferenceNavigation, EditingLineTimes)
{
    const auto doc = load(kReference);
    const auto editing = load(events("Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,x\n"));
    EXPECT_EQ(legacyMilliseconds(editing.lines()[0]->start), 2000);
    EXPECT_EQ(seekOccurrences(doc, *editing.lines()[0]), (Runs{{1, 2}}));
}

// NewSeeking shows the first occurrence of the previewed grid
// (SubsGridPreview.cpp:917-924, :112); the tray counts the runs and steps
// between them, and a no-match is empty: legacy's length-0 substitute is kept
// apart as the nearest Line, never shown as a match.
TEST(ReferenceNavigation, LinkedMatchCandidatesAndNoMatch)
{
    LinkedMatch match;
    EXPECT_FALSE(match.active());
    EXPECT_FALSE(match.noMatch());
    match.set({{1, 2}, {5, 1}});
    EXPECT_TRUE(match.active());
    EXPECT_FALSE(match.noMatch());
    EXPECT_EQ(match.count(), 2u);
    EXPECT_EQ(match.current(), (Occurrence{1, 2}));
    EXPECT_FALSE(match.step(-1));
    EXPECT_TRUE(match.step(1));
    EXPECT_EQ(match.index(), 1u);
    EXPECT_EQ(match.current(), (Occurrence{5, 1}));
    EXPECT_FALSE(match.step(1));
    EXPECT_TRUE(match.step(-1));
    EXPECT_EQ(match.current(), (Occurrence{1, 2}));
    EXPECT_FALSE(match.nearest());

    match.set({{4, 0}});
    EXPECT_TRUE(match.noMatch());
    EXPECT_EQ(match.count(), 0u);
    EXPECT_FALSE(match.current());
    EXPECT_EQ(match.nearest(), 4u);
    EXPECT_FALSE(match.step(1));

    match.set({}); // an unpaired Line in a comparison: no match, no nearest
    EXPECT_TRUE(match.noMatch());
    EXPECT_FALSE(match.nearest());

    match.clear();
    EXPECT_FALSE(match.active());
    EXPECT_FALSE(match.noMatch());
}
