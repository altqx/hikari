#include "hikari/application/side_bars.h"

#include "hikari/json/json.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

namespace hikari::application {
namespace {

using json::Json;

constexpr std::array kMiddleOnly{std::string_view("Grid"), std::string_view("Video"), std::string_view("Audio"),
                                 std::string_view("Editor")};
constexpr std::array kAiPanels{std::string_view("Agent"), std::string_view("Review")};

SideBar &bar(SideBars &bars, SideBarId id) { return id == SideBarId::First ? bars.first : bars.second; }

bool isAi(std::string_view panel) { return std::ranges::find(kAiPanels, panel) != kAiPanels.end(); }

bool holds(const SideBar &bar, std::string_view panel) { return std::ranges::find(bar.panels, panel) != bar.panels.end(); }

void take(SideBar &bar, std::string_view panel)
{
    const auto at = std::ranges::find(bar.panels, panel);
    if (at == bar.panels.end())
        return;
    const auto index = static_cast<std::size_t>(at - bar.panels.begin());
    bar.panels.erase(at);
    if (bar.shown != panel)
        return;
    bar.shown = bar.panels.empty() ? std::string() : bar.panels[std::min(index, bar.panels.size() - 1)];
}

int clampedWidth(std::int64_t width)
{
    return static_cast<int>(std::clamp<std::int64_t>(width, kSideBarMinimumWidth, kSideBarMaximumWidth));
}

Json barJson(const SideBar &bar)
{
    Json panels = Json::array();
    for (const auto &panel : bar.panels)
        panels.push(panel);
    Json o = Json::object({{"panels", panels}});
    if (!bar.shown.empty())
        o["shown"] = bar.shown;
    o["width"] = bar.width;
    if (bar.hidden)
        o["hidden"] = true;
    return o;
}

std::optional<SideBar> parseBar(const Json *value)
{
    SideBar bar;
    if (!value)
        return bar;
    if (!value->isObject())
        return std::nullopt;
    if (const Json *panels = value->find("panels")) {
        if (!panels->isArray())
            return std::nullopt;
        for (const Json &panel : panels->asArray()) {
            if (!panel.isString())
                return std::nullopt;
            if (sideBarMayHold(panel.asString()) && !holds(bar, panel.asString()))
                bar.panels.push_back(panel.asString());
        }
    }
    if (const Json *shown = value->find("shown")) {
        if (!shown->isString())
            return std::nullopt;
        bar.shown = shown->asString();
    }
    if (const Json *width = value->find("width")) {
        if (!width->isInt())
            return std::nullopt;
        bar.width = clampedWidth(width->asInt());
    }
    if (const Json *hidden = value->find("hidden")) {
        if (!hidden->isBool())
            return std::nullopt;
        bar.hidden = hidden->asBool();
    }
    return bar;
}

void settleShown(SideBar &bar)
{
    if (!holds(bar, bar.shown))
        bar.shown = bar.panels.empty() ? std::string() : bar.panels.front();
}

}

AiSideBarEdge aiSideBarEdge(std::string_view name) { return name == "end" ? AiSideBarEdge::End : AiSideBarEdge::Start; }

std::string_view aiSideBarEdgeName(AiSideBarEdge edge) { return edge == AiSideBarEdge::End ? "end" : "start"; }

AiSideBarEdge swapped(AiSideBarEdge edge) { return edge == AiSideBarEdge::End ? AiSideBarEdge::Start : AiSideBarEdge::End; }

SideBarId sideBarOnLeft(AiSideBarEdge edge, bool rightToLeft)
{
    return (edge == AiSideBarEdge::Start) != rightToLeft ? SideBarId::First : SideBarId::Second;
}

std::optional<SideBarId> pluginSideBar(std::string_view area, AiSideBarEdge edge, bool rightToLeft)
{
    const SideBarId left = sideBarOnLeft(edge, rightToLeft);
    if (area == "left")
        return left;
    if (area == "right")
        return left == SideBarId::First ? SideBarId::Second : SideBarId::First;
    return std::nullopt;
}

SideBars defaultSideBars(SideBarStart start)
{
    SideBars bars;
    bars.first = {{"Agent", "Review"}, "Agent", kSideBarStartWidth, false};
    bars.second = {{"Explorer", "Search", "Timing", "Postprocessor", "Styles", "QcNotes"},
                   start.project ? "Explorer" : "Search", kSideBarStartWidth, false};
    return bars;
}

SideBars migratedSideBars(SideBarStart start, const std::vector<std::string> &rightOfGrid)
{
    SideBars bars = defaultSideBars(start);
    for (const auto &panel : rightOfGrid)
        if (sideBarMayHold(panel) && !sideOf(bars, panel))
            (isAi(panel) ? bars.first : bars.second).panels.push_back(panel);
    return bars;
}

bool sideBarMayHold(std::string_view panel)
{
    return !panel.empty() && std::ranges::find(kMiddleOnly, panel) == kMiddleOnly.end();
}

std::optional<SideBarId> sideOf(const SideBars &bars, std::string_view panel)
{
    if (holds(bars.first, panel))
        return SideBarId::First;
    if (holds(bars.second, panel))
        return SideBarId::Second;
    return std::nullopt;
}

SideBars moved(SideBars bars, std::string_view panel, SideBarId to, std::size_t index)
{
    if (!sideBarMayHold(panel))
        return bars;
    take(bars.first, panel);
    take(bars.second, panel);
    SideBar &target = bar(bars, to);
    target.panels.insert(target.panels.begin() + static_cast<std::ptrdiff_t>(std::min(index, target.panels.size())),
                         std::string(panel));
    target.shown = std::string(panel);
    target.hidden = false;
    return bars;
}

SideBars removed(SideBars bars, std::string_view panel)
{
    take(bars.first, panel);
    take(bars.second, panel);
    return bars;
}

SideBars shownPanel(SideBars bars, std::string_view panel)
{
    const auto side = sideOf(bars, panel);
    if (!side)
        return bars;
    SideBar &target = bar(bars, *side);
    target.shown = std::string(panel);
    target.hidden = false;
    return bars;
}

SideBars iconClicked(SideBars bars, std::string_view panel)
{
    const auto side = sideOf(bars, panel);
    if (!side)
        return bars;
    SideBar &target = bar(bars, *side);
    if (target.shown == panel && !target.hidden)
        target.hidden = true;
    else
        return shownPanel(std::move(bars), panel);
    return bars;
}

SideBars toggled(SideBars bars, SideBarId id)
{
    SideBar &target = bar(bars, id);
    target.hidden = !target.hidden;
    return bars;
}

SideBars widened(SideBars bars, SideBarId id, int width)
{
    SideBar &target = bar(bars, id);
    if (width < kSideBarMinimumWidth)
        target.hidden = true;
    else
        target.width = clampedWidth(width);
    return bars;
}

std::vector<std::string> availablePanels(const SideBar &bar, const PanelAvailable &available)
{
    std::vector<std::string> panels;
    std::ranges::copy_if(bar.panels, std::back_inserter(panels), [&](const std::string &p) { return available(p); });
    return panels;
}

bool sideBarShows(const SideBar &bar, const PanelAvailable &available)
{
    return !bar.hidden && (bar.panels.empty() || std::ranges::any_of(bar.panels, available));
}

std::string sideBarsJson(const SideBars &bars)
{
    return Json::object({{"first", barJson(bars.first)}, {"second", barJson(bars.second)}}).dump();
}

std::optional<SideBars> parseSideBars(std::string_view text)
{
    const auto parsed = Json::parse(text);
    if (!parsed || !parsed->isObject())
        return std::nullopt;
    auto first = parseBar(parsed->find("first"));
    auto second = parseBar(parsed->find("second"));
    if (!first || !second)
        return std::nullopt;
    std::erase_if(second->panels, [&](const std::string &p) { return holds(*first, p); });
    settleShown(*first);
    settleShown(*second);
    return SideBars{std::move(*first), std::move(*second)};
}

}
