#pragma once

#include <span>
#include <string>
#include <string_view>

namespace hikari::application {

inline constexpr std::string_view kSiteUrl = "https://hikarisub.altqx.com";
inline constexpr std::string_view kGuideHome = "/en/docs";

enum class GuideSurface { Panel, Window };
// The kind's name in guide-links.json.
std::string_view guideSurfaceName(GuideSurface kind);

struct GuideLink {
    GuideSurface kind;
    std::string_view id;
    std::string_view path; // under kSiteUrl
};
// Panels in panelIds' order, then windows by id.
std::span<const GuideLink> guideLinks();

// `id` is a panel id, "<panel id>.<mode>", or a window's guide id; empty when it has no page.
std::string_view guidePage(GuideSurface kind, std::string_view id);
// Falls back to the guide's home.
std::string guideUrl(GuideSurface kind, std::string_view id);

} // namespace hikari::application
