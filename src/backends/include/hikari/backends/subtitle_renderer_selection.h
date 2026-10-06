#pragma once

// W2: the renderer the video's subtitles go through, as legacy
// SubtitlesProviderManager chooses it (20d647c4,
// SubtitlesProviderManager.cpp:28-50) by the VSFILTER_INSTANCE setting
// (video.subtitleProvider): "libass", or a CSRI renderer's name on Windows.
// An empty setting is libass on every platform: CSRI is selected explicitly
// (W2; legacy Windows used the default CSRI renderer). On Linux libass is the
// only renderer and the setting is kept but not read. A named CSRI renderer
// that is not installed falls back to the default CSRI renderer as legacy's
// GetVSFilter does, and with none at all nothing is drawn ("Cannot initialize
// CSRI."): there is no fallback to libass.

#include "hikari/backends/libass_renderer.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hikari::backends {

class CsriRenderers;
class CsriRenderer;

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

    // GetProviders: the CSRI renderers' names, then "libass".
    std::vector<std::string> providers();
    // The setting's value. Returns whether the renderer changed (legacy
    // DestroyProviders: the video's subtitles must be prepared again).
    bool select(const std::string &provider);
    bool usesLibass() const { return m_active == &m_libass; }
    // The chosen renderer's name: "libass" or the setting's CSRI name.
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
#endif
};

} // namespace hikari::backends
