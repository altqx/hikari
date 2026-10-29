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

const std::string kReference = events("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                      "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,b\n"
                                      "Dialogue: 0,0:00:02.50,0:00:04.00,Default,,0,0,0,,c\n"
                                      "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,d\n"
                                      "Dialogue: 0,0:00:05.50,0:00:07.00,Default,,0,0,0,,e\n"
                                      "Dialogue: 0,0:00:10.00,0:00:11.00,Default,,0,0,0,,f\n");

using Runs = std::vector<Occurrence>;

}

TEST(ReferenceNavigation, OverlappingLinesInRuns)
{
    const auto doc = load(kReference);
    EXPECT_EQ(seekOccurrences(doc, 2000, 3000), (Runs{{1, 2}}));
    EXPECT_EQ(seekOccurrences(doc, 1500, 5500), (Runs{{0, 4}}));
    EXPECT_EQ(seekOccurrences(doc, 5000, 6000), (Runs{{3, 2}}));
    EXPECT_EQ(seekOccurrences(doc, 0, 20000), (Runs{{0, 6}}));
}

TEST(ReferenceNavigation, HiddenLinesSplitRunsAndAreNeverChosen)
{
    const auto doc = load(events("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                 "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[hidden],0,0,0,,hidden\n"
                                 "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,c\n"
                                 "Dialogue: 0,0:00:01.50,0:00:03.00,Default,,0,0,0,,d\n"
                                 "Dialogue: 0,0:00:09.00,0:00:09.50,Default,[hidden],0,0,0,,far hidden\n"));
    ASSERT_EQ(doc.lines()[1]->visibility, core::LineVisibility::Hidden);
    EXPECT_EQ(seekOccurrences(doc, 1000, 2000), (Runs{{0, 1}, {2, 2}}));
    EXPECT_EQ(seekOccurrences(doc, 8000, 9200), (Runs{{3, 0}}));
}

TEST(ReferenceNavigation, NoOverlapGivesLegacysNearestLine)
{
    const auto doc = load(kReference);
    EXPECT_EQ(seekOccurrences(doc, 8000, 9000), (Runs{{5, 0}}));
    EXPECT_EQ(seekOccurrences(doc, 7200, 9900), (Runs{{5, 0}}));
    EXPECT_EQ(seekOccurrences(doc, 7000, 9500), (Runs{{4, 0}}));
    EXPECT_EQ(seekOccurrences(doc, 0, 500), (Runs{{0, 0}}));
    EXPECT_EQ(seekOccurrences(doc, 12000, 13000), (Runs{{5, 0}}));
}

TEST(ReferenceNavigation, EveryLineHiddenOrNone)
{
    const auto hidden = load(events("Dialogue: 0,0:00:01.00,0:00:02.00,Default,[hidden],0,0,0,,a\n"
                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[hidden],0,0,0,,b\n"));
    EXPECT_EQ(seekOccurrences(hidden, 1000, 2000), (Runs{{0, 0}}));
    EXPECT_TRUE(seekOccurrences(core::Document{}, 1000, 2000).empty());
}

TEST(ReferenceNavigation, EditingLineTimes)
{
    const auto doc = load(kReference);
    const auto editing = load(events("Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,x\n"));
    EXPECT_EQ(legacyMilliseconds(editing.lines()[0]->start), 2000);
    EXPECT_EQ(seekOccurrences(doc, *editing.lines()[0]), (Runs{{1, 2}}));
}

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

    match.set({});
    EXPECT_TRUE(match.noMatch());
    EXPECT_FALSE(match.nearest());

    match.clear();
    EXPECT_FALSE(match.active());
    EXPECT_FALSE(match.noMatch());
}
