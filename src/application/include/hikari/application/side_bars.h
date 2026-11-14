#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A shell window's two side bars (ADR 0032). The first is the one "AI side bar" places;
// it starts with the AI panels, the second with the tools, and either can hold any panel
// a side bar may hold. Panels are the workspace's panel ids ("Agent", "plugin:<id>/<panel>").
namespace hikari::application {

inline constexpr int kSideBarMinimumWidth = 240;
inline constexpr int kSideBarStartWidth = 320;
inline constexpr int kSideBarMaximumWidth = 2000;
inline constexpr int kMiddleWidthBesideBothSideBars = 1500;
inline constexpr int kBothSideBarsWindowWidth = 2 * kSideBarStartWidth + kMiddleWidthBesideBothSideBars;

enum class SideBarId { First, Second };

struct SideBar {
    std::vector<std::string> panels;
    std::string shown; // "" while it holds none
    int width = kSideBarStartWidth;
    bool hidden = false;
    bool operator==(const SideBar &) const = default;
};

struct SideBars {
    SideBar first;
    SideBar second;
    bool operator==(const SideBars &) const = default;
};

// Settings › Appearance › AI side bar: the first side bar's edge in reading order.
enum class AiSideBarEdge { Start, End };
AiSideBarEdge aiSideBarEdge(std::string_view name);
std::string_view aiSideBarEdgeName(AiSideBarEdge edge);
AiSideBarEdge swapped(AiSideBarEdge edge);
SideBarId sideBarOnLeft(AiSideBarEdge edge, bool rightToLeft);
// A plugin manifest's `area`; nothing for one that stays in the middle.
std::optional<SideBarId> pluginSideBar(std::string_view area, AiSideBarEdge edge, bool rightToLeft);

struct SideBarStart {
    bool project = false; // the window has a Project, so the tools side bar shows the Explorer
};
SideBars defaultSideBars(SideBarStart start);
// A first start of this version: the panels a saved arrangement docked right of the Grid join the side bars,
// and a window narrower than kBothSideBarsWindowWidth starts with the AI side bar hidden.
SideBars migratedSideBars(SideBarStart start, int windowWidth, const std::vector<std::string> &rightOfGrid);

// The Grid, Video, Audio and the Line editor stay in the middle.
bool sideBarMayHold(std::string_view panel);
std::optional<SideBarId> sideOf(const SideBars &bars, std::string_view panel);

// Each returns the side bars after the change, unchanged where it doesn't apply.
SideBars moved(SideBars bars, std::string_view panel, SideBarId to, std::size_t index);
SideBars removed(SideBars bars, std::string_view panel);
SideBars shownPanel(SideBars bars, std::string_view panel);
SideBars iconClicked(SideBars bars, std::string_view panel);
SideBars toggled(SideBars bars, SideBarId id);
SideBars widened(SideBars bars, SideBarId id, int width);

using PanelAvailable = std::function<bool(std::string_view panel)>;
std::vector<std::string> availablePanels(const SideBar &bar, const PanelAvailable &available);
// Not hidden, and not left without panels by ones that went away (AI off); an empty one shows its placeholder.
bool sideBarShows(const SideBar &bar, const PanelAvailable &available);

// Compact JSON for the Session's window entry and the `workspace.sideBars` setting.
std::string sideBarsJson(const SideBars &bars);
std::optional<SideBars> parseSideBars(std::string_view text);

}
