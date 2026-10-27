#pragma once

// DX1 (#367): the user guide on HikariSub's site and the page User guide
// (GLOBAL_HELP, F1) opens for the surface it is pressed on: a panel by its
// id (WorkspaceLayoutController::panelIds) or a DG1 window by its guide id
// (DialogWindow.guideId, its dialogId unless it names another). A surface
// without a page opens the guide's home. Qt-free. The docs export writes
// the table as guide-links.json, which the site's link check reads, so a
// renamed page fails there.

#include <span>
#include <string>
#include <string_view>

namespace hikari::application {

inline constexpr std::string_view kSiteUrl = "https://hikarisub.altqx.com";
inline constexpr std::string_view kGuideHome = "/en/docs";

enum class GuideSurface { Panel, Window };
// "panel", "window": the kind's name in guide-links.json.
std::string_view guideSurfaceName(GuideSurface kind);

struct GuideLink {
    GuideSurface kind;
    std::string_view id;
    std::string_view path; // under kSiteUrl
};
// Panels in panelIds' order, then windows by id.
std::span<const GuideLink> guideLinks();

// The page of a surface, else nothing.
std::string_view guidePage(GuideSurface kind, std::string_view id);
// The full URL User guide opens: the surface's page, else the guide's home.
std::string guideUrl(GuideSurface kind, std::string_view id);

} // namespace hikari::application
