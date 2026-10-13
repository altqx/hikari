#pragma once

// W2: the renderer the video's subtitles go through, as legacy
// SubtitlesProviderManager chooses it (20d647c4,
// SubtitlesProviderManager.cpp:28-50) by the VSFILTER_INSTANCE setting
// (video.subtitleProvider): "libass", or a CSRI renderer's name on Windows.
// An empty setting is libass on every platform: CSRI is selected explicitly
// (W2-libass-default; legacy Windows used the default CSRI renderer). On
// Linux libass is the only renderer and the setting is kept but not read.
// A name that is not among the installed renderers (providers()) draws
// through libass (W2-missing-renderer-libass, user 2026-10-13): legacy fell
// back to the default CSRI renderer (GetVSFilter), and with none at all drew
// nothing ("Cannot initialize CSRI.").

#include "hikari/backends/libass_renderer.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends {

class CsriRenderers;
class CsriRenderer;

// VSFilterMod's CSRI renderer (Csri/VSFiltermod.dll): slow, and only needed
// for the tags it adds, so it is never chosen by default and is listed last
// (user 2026-10-13).
inline constexpr std::string_view kVsfilterModRenderer = "vsfiltermod_textsub";
bool isVsfilterMod(std::string_view name);
// The Video page's list from the CSRI renderers' names (in CSRI order, the
// default first): every renderer but VSFilterMod, then "libass", then
// VSFilterMod. Legacy GetProviders listed the CSRI names then "libass".
std::vector<std::string> orderedProviders(const std::vector<std::string> &csriNames);
// What draws for the stored name: an empty or unknown name is "libass",
// never the first CSRI renderer (W2-libass-default,
// W2-missing-renderer-libass); a listed name is itself. Exact, as legacy's
// vsfilters.Index(name).
std::string resolveProvider(std::string_view stored, const std::vector<std::string> &providers);

class SubtitleRendererSelection : public application::SubtitleRendererPort {
public:
    using Log = std::function<void(const std::string &)>;
    SubtitleRendererSelection();
    ~SubtitleRendererSelection() override;

    // Legacy's silent log (HikariLogSilent) for the CSRI messages.
    void setLog(Log log) { m_log = std::move(log); }
    // Windows: the program's Csri folder (csrilib_os_init), read the first
    // time a CSRI renderer is needed. Ignored elsewhere.
    void setCsriFolder(std::filesystem::path folder);

    // GetProviders: the CSRI renderers' names and "libass", in the Video
    // page's order (orderedProviders).
    std::vector<std::string> providers();
    // The setting's value. Returns whether the renderer changed (legacy
    // DestroyProviders: the video's subtitles must be prepared again).
    bool select(const std::string &provider);
    bool usesLibass() const { return m_active == &m_libass; }
    // The chosen renderer's name: "libass" or an installed CSRI renderer's
    // name (resolveProvider).
    const std::string &provider() const { return m_provider; }
    // What draws now: "libass", or the CSRI renderer's name (empty when
    // there is none).
    std::string activeName() const;

    std::expected<std::uint64_t, application::RenderError> prepare(application::RenderSnapshot snapshot) override;
    std::expected<application::OverlayFrame, application::RenderError> render(core::DocumentTime time, int width,
                                                                             int height) override;

private:
    std::shared_ptr<CsriRenderers> csriRenderers();
    LibassRenderer m_libass;
    application::SubtitleRendererPort *m_active = &m_libass;
    std::string m_provider = "libass";
    Log m_log;
    std::filesystem::path m_csriFolder;
#ifdef _WIN32
    std::shared_ptr<CsriRenderers> m_csriRenderers;
    std::unique_ptr<CsriRenderer> m_csri;
    std::string m_missingLogged; // the uninstalled name last logged
#endif
};

} // namespace hikari::backends
