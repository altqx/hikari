#include "hikari/application/guide_links.h"

#include <array>

namespace hikari::application {

namespace {

using enum GuideSurface;

// Paths must exist on the site; its link check reads guide-links.json.
constexpr std::array kLinks{
    GuideLink{Panel, "Video", "/en/docs/basics/video"},
    GuideLink{Panel, "Audio", "/en/docs/basics/audio"},
    GuideLink{Panel, "Editor", "/en/docs/basics/line-editor"},
    GuideLink{Panel, "Grid", "/en/docs/basics/grid"},
    GuideLink{Panel, "Reference", "/en/docs/translation/line-matching"},
    GuideLink{Panel, "Timing", "/en/docs/basics/timing"},
    GuideLink{Panel, "Search", "/en/docs/basics/find-and-replace"},
    GuideLink{Panel, "Automation", "/en/docs/automation/automation-panel"},
    GuideLink{Panel, "Original", "/en/docs/translation/translation-mode"},
    GuideLink{Panel, "Log", "/en/docs/help/troubleshooting"},
    GuideLink{Panel, "Styles", "/en/docs/basics/styles"},
    GuideLink{Panel, "FixMinorErrors", "/en/docs/checking/fix-minor-errors"},
    GuideLink{Panel, "Spellchecker", "/en/docs/checking/spelling"},
    GuideLink{Panel, "Spellchecker.grammar", "/en/docs/checking/grammar"},
    GuideLink{Panel, "Template", "/en/docs/karaoke/template-editor"},
    GuideLink{Window, "allTagsEdition", "/en/docs/typesetting/shapes-and-tags-editors"},
    GuideLink{Window, "automation", "/en/docs/automation/running-scripts"},
    GuideLink{Window, "dummyVideo", "/en/docs/basics/video"},
    GuideLink{Window, "fontCatalogs", "/en/docs/typesetting/fonts"},
    GuideLink{Window, "fontCollector", "/en/docs/typesetting/fonts"},
    GuideLink{Window, "gettingStarted", "/en/docs/get-started/getting-started-window"},
    GuideLink{Window, "hotkeyCapture", "/en/docs/customize/hotkeys"},
    GuideLink{Window, "kanjiTimer", "/en/docs/karaoke/kanji-timer"},
    GuideLink{Window, "karaokeCheck", "/en/docs/karaoke/karaoke-check"},
    GuideLink{Window, "manageLayouts", "/en/docs/customize/layouts"},
    GuideLink{Window, "resample", "/en/docs/basics/files-and-formats"},
    GuideLink{Window, "saveLayout", "/en/docs/customize/layouts"},
    GuideLink{Window, "scriptProperties", "/en/docs/basics/files-and-formats"},
    GuideLink{Window, "settings", "/en/docs/customize/settings"},
    GuideLink{Window, "settingsImport", "/en/docs/get-started/moving-from-legacy"},
    GuideLink{Window, "shapesEdition", "/en/docs/typesetting/shapes-and-tags-editors"},
    GuideLink{Window, "styleManager", "/en/docs/basics/styles"},
};

}

std::string_view guideSurfaceName(GuideSurface kind)
{
    return kind == Panel ? "panel" : "window";
}

std::span<const GuideLink> guideLinks()
{
    return kLinks;
}

std::string_view guidePage(GuideSurface kind, std::string_view id)
{
    for (const GuideLink &link : kLinks)
        if (link.kind == kind && link.id == id)
            return link.path;
    return {};
}

std::string guideUrl(GuideSurface kind, std::string_view id)
{
    const std::string_view page = guidePage(kind, id);
    std::string url(kSiteUrl);
    url += page.empty() ? kGuideHome : page;
    return url;
}

}
