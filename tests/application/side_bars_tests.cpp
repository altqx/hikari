#include "hikari/application/side_bars.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace hikari::application {
namespace {

using Panels = std::vector<std::string>;

const Panels kEverything{"Agent", "Review", "Explorer", "Search", "Timing", "Postprocessor", "Styles", "QcNotes"};

TEST(SideBars, DefaultsPutTheAiFirstAndTheToolsSecond)
{
    const SideBars bars = defaultSideBars({.project = true});
    EXPECT_EQ(bars.first.panels, (Panels{"Agent", "Review"}));
    EXPECT_EQ(bars.first.shown, "Agent");
    EXPECT_EQ(bars.second.panels, (Panels{"Explorer", "Search", "Timing", "Postprocessor", "Styles", "QcNotes"}));
    EXPECT_EQ(bars.second.shown, "Explorer");
    EXPECT_EQ(bars.first.width, kSideBarStartWidth);
    EXPECT_FALSE(bars.first.hidden);
    EXPECT_FALSE(bars.second.hidden);
}

TEST(SideBars, WithoutAProjectTheToolsSideBarStartsOnFind)
{
    EXPECT_EQ(defaultSideBars({.project = false}).second.shown, "Search");
}

TEST(SideBars, AnyPanelGoesInEitherSideBar)
{
    SideBars bars = defaultSideBars({.project = true});
    bars = moved(bars, "Agent", SideBarId::Second, 1);
    EXPECT_EQ(bars.first.panels, (Panels{"Review"}));
    EXPECT_EQ(bars.second.panels, (Panels{"Explorer", "Agent", "Search", "Timing", "Postprocessor", "Styles", "QcNotes"}));
    EXPECT_EQ(bars.second.shown, "Agent");
    EXPECT_EQ(bars.first.shown, "Review");
    EXPECT_EQ(sideOf(bars, "Agent"), SideBarId::Second);
}

TEST(SideBars, MovingWithinASideBarReorders)
{
    SideBars bars = defaultSideBars({.project = true});
    bars = moved(bars, "Styles", SideBarId::Second, 0);
    EXPECT_EQ(bars.second.panels.front(), "Styles");
    EXPECT_EQ(bars.second.panels.size(), 6u);
}

TEST(SideBars, AnIndexPastTheEndAppends)
{
    SideBars bars = moved(defaultSideBars({}), "Search", SideBarId::First, 99);
    EXPECT_EQ(bars.first.panels, (Panels{"Agent", "Review", "Search"}));
}

TEST(SideBars, MovingAPanelInFromTheMiddleAddsIt)
{
    SideBars bars = moved(defaultSideBars({}), "Automation", SideBarId::First, 99);
    EXPECT_EQ(bars.first.panels.back(), "Automation");
    EXPECT_EQ(bars.first.shown, "Automation");
}

TEST(SideBars, MovingIntoAHiddenSideBarShowsIt)
{
    SideBars bars = defaultSideBars({});
    bars.first.hidden = true;
    bars = moved(bars, "Search", SideBarId::First, 0);
    EXPECT_FALSE(bars.first.hidden);
}

TEST(SideBars, TakingOutTheLastPanelLeavesAnEmptySideBarShown)
{
    SideBars bars = defaultSideBars({});
    bars = removed(bars, "Agent");
    bars = removed(bars, "Review");
    EXPECT_TRUE(bars.first.panels.empty());
    EXPECT_EQ(bars.first.shown, "");
    EXPECT_FALSE(bars.first.hidden);
    EXPECT_EQ(sideOf(bars, "Agent"), std::nullopt);
}

TEST(SideBars, TakingOutTheShownPanelShowsItsNeighbour)
{
    SideBars bars = defaultSideBars({.project = true});
    bars = shownPanel(bars, "Timing");
    bars = removed(bars, "Timing");
    EXPECT_EQ(bars.second.shown, "Postprocessor");
    bars = shownPanel(bars, "QcNotes");
    bars = removed(bars, "QcNotes");
    EXPECT_EQ(bars.second.shown, "Styles");
}

TEST(SideBars, ClickingTheShownIconHidesTheSideBarAndAnotherShowsIt)
{
    SideBars bars = defaultSideBars({.project = true});
    bars = iconClicked(bars, "Explorer");
    EXPECT_TRUE(bars.second.hidden);
    EXPECT_EQ(bars.second.shown, "Explorer");
    bars = iconClicked(bars, "Explorer");
    EXPECT_FALSE(bars.second.hidden);
    bars = iconClicked(bars, "Search");
    EXPECT_FALSE(bars.second.hidden);
    EXPECT_EQ(bars.second.shown, "Search");
}

TEST(SideBars, ShowingAPanelInAHiddenSideBarShowsTheSideBar)
{
    SideBars bars = defaultSideBars({});
    bars.second.hidden = true;
    bars = shownPanel(bars, "Timing");
    EXPECT_FALSE(bars.second.hidden);
    EXPECT_EQ(bars.second.shown, "Timing");
}

TEST(SideBars, ShowingAPanelNotInASideBarChangesNothing)
{
    const SideBars bars = defaultSideBars({});
    EXPECT_EQ(shownPanel(bars, "Grid"), bars);
}

TEST(SideBars, ToggleKeepsThePanelAndTheWidth)
{
    SideBars bars = defaultSideBars({});
    bars = widened(bars, SideBarId::First, 400);
    bars = toggled(bars, SideBarId::First);
    EXPECT_TRUE(bars.first.hidden);
    bars = toggled(bars, SideBarId::First);
    EXPECT_FALSE(bars.first.hidden);
    EXPECT_EQ(bars.first.width, 400);
    EXPECT_EQ(bars.first.shown, "Agent");
}

TEST(SideBars, DraggingUnderTheMinimumHidesAndKeepsTheLastWidth)
{
    SideBars bars = widened(defaultSideBars({}), SideBarId::Second, 360);
    bars = widened(bars, SideBarId::Second, kSideBarMinimumWidth - 1);
    EXPECT_TRUE(bars.second.hidden);
    EXPECT_EQ(bars.second.width, 360);
    bars = widened(bars, SideBarId::Second, kSideBarMinimumWidth);
    EXPECT_EQ(bars.second.width, kSideBarMinimumWidth);
}

TEST(SideBars, AvailablePanelsLeaveOutTheAiWhileItIsOff)
{
    const SideBars bars = defaultSideBars({});
    const auto aiOff = [](std::string_view panel) { return panel != "Agent" && panel != "Review"; };
    EXPECT_TRUE(availablePanels(bars.first, aiOff).empty());
    EXPECT_FALSE(sideBarShows(bars.first, aiOff));
    EXPECT_TRUE(sideBarShows(bars.second, aiOff));
    EXPECT_EQ(bars.first.panels, (Panels{"Agent", "Review"}));
}

TEST(SideBars, AnEmptySideBarStillShowsItsPlaceholder)
{
    SideBar empty;
    const auto all = [](std::string_view) { return true; };
    EXPECT_TRUE(sideBarShows(empty, all));
    empty.hidden = true;
    EXPECT_FALSE(sideBarShows(empty, all));
}

TEST(SideBars, ThePhysicalLeftFollowsTheSettingAndTheDirection)
{
    EXPECT_EQ(sideBarOnLeft(AiSideBarEdge::Start, false), SideBarId::First);
    EXPECT_EQ(sideBarOnLeft(AiSideBarEdge::End, false), SideBarId::Second);
    EXPECT_EQ(sideBarOnLeft(AiSideBarEdge::Start, true), SideBarId::Second);
    EXPECT_EQ(sideBarOnLeft(AiSideBarEdge::End, true), SideBarId::First);
    EXPECT_EQ(swapped(AiSideBarEdge::Start), AiSideBarEdge::End);
}

TEST(SideBars, TheEdgeSettingReadsItsTwoNames)
{
    EXPECT_EQ(aiSideBarEdge("start"), AiSideBarEdge::Start);
    EXPECT_EQ(aiSideBarEdge("end"), AiSideBarEdge::End);
    EXPECT_EQ(aiSideBarEdge("sideways"), AiSideBarEdge::Start);
    EXPECT_EQ(aiSideBarEdgeName(AiSideBarEdge::End), "end");
}

TEST(SideBars, JsonRoundTrips)
{
    SideBars bars = moved(defaultSideBars({.project = true}), "Agent", SideBarId::Second, 2);
    bars = widened(bars, SideBarId::First, 288);
    bars = toggled(bars, SideBarId::Second);
    const std::string text = sideBarsJson(bars);
    EXPECT_EQ(text.find('\n'), std::string::npos);
    EXPECT_EQ(parseSideBars(text), bars);
}

TEST(SideBars, JsonNamesItsFields)
{
    SideBars bars;
    bars.first = {{"Agent"}, "Agent", 320, false};
    bars.second = {{}, "", 300, true};
    EXPECT_EQ(sideBarsJson(bars),
              R"({"first":{"panels":["Agent"],"shown":"Agent","width":320},"second":{"panels":[],"width":300,"hidden":true}})");
}

TEST(SideBars, BrokenJsonIsRefused)
{
    for (const char *text : {"", "[]", "{", R"({"first":1})", R"({"first":{"panels":"Agent"}})",
                             R"({"first":{"panels":[1]}})", R"({"first":{"width":"wide"}})",
                             R"({"first":{"hidden":1}})"})
        EXPECT_EQ(parseSideBars(text), std::nullopt) << text;
}

TEST(SideBars, ReadingDropsRepeatsAShownPanelThatIsNotThereAndClampsTheWidth)
{
    const auto bars = parseSideBars(
        R"({"first":{"panels":["Agent","Agent","Search"],"shown":"Grid","width":12},"second":{"panels":["Search","Styles"],"width":99999}})");
    ASSERT_TRUE(bars);
    EXPECT_EQ(bars->first.panels, (Panels{"Agent", "Search"}));
    EXPECT_EQ(bars->first.shown, "Agent");
    EXPECT_EQ(bars->first.width, kSideBarMinimumWidth);
    EXPECT_EQ(bars->second.panels, (Panels{"Styles"}));
    EXPECT_EQ(bars->second.width, kSideBarMaximumWidth);
}

TEST(SideBars, AMissingSideBarReadsEmpty)
{
    const auto bars = parseSideBars(R"({"first":{"panels":["Agent"],"shown":"Agent"}})");
    ASSERT_TRUE(bars);
    EXPECT_TRUE(bars->second.panels.empty());
    EXPECT_EQ(bars->second.width, kSideBarStartWidth);
}

TEST(SideBars, MigrationMovesTheRightOfGridColumnInOnce)
{
    const SideBars bars = migratedSideBars({.project = false}, {"Search", "Agent", "Automation", "Timing"});
    EXPECT_EQ(bars.first.panels, (Panels{"Agent", "Review"}));
    EXPECT_EQ(bars.second.panels, (Panels{"Explorer", "Search", "Timing", "Postprocessor", "Styles", "QcNotes", "Automation"}));
    EXPECT_EQ(bars.second.shown, "Search");
}

TEST(SideBars, MigrationSendsPluginPanelsToTheTools)
{
    const SideBars bars = migratedSideBars({}, {"plugin:karaoke/main"});
    EXPECT_EQ(bars.second.panels.back(), "plugin:karaoke/main");
}

TEST(SideBars, MiddleOnlyPanelsNeverGoIn)
{
    for (const char *panel : {"Grid", "Video", "Audio", "Editor"}) {
        EXPECT_FALSE(sideBarMayHold(panel)) << panel;
        EXPECT_EQ(moved(defaultSideBars({}), panel, SideBarId::First, 0), defaultSideBars({})) << panel;
    }
    for (const char *panel : kEverything)
        EXPECT_TRUE(sideBarMayHold(panel)) << panel;
    EXPECT_TRUE(sideBarMayHold("plugin:karaoke/main"));
}

TEST(SideBars, APluginAreaPicksItsFirstSideBar)
{
    EXPECT_EQ(pluginSideBar("left", AiSideBarEdge::Start, false), SideBarId::First);
    EXPECT_EQ(pluginSideBar("right", AiSideBarEdge::Start, false), SideBarId::Second);
    EXPECT_EQ(pluginSideBar("left", AiSideBarEdge::End, false), SideBarId::Second);
    EXPECT_EQ(pluginSideBar("bottom", AiSideBarEdge::Start, false), std::nullopt);
}

}
}
