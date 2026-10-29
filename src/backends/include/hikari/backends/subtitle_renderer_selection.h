#pragma once

#include "hikari/backends/libass_renderer.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends {

class CsriRenderers;
class CsriRenderer;

inline constexpr std::string_view kVsfilterModRenderer = "vsfiltermod_textsub";
bool isVsfilterMod(std::string_view name);
// "libass" first, then the other CSRI renderers in CSRI order, then VSFilterMod.
std::vector<std::string> orderedProviders(const std::vector<std::string> &csriNames);
// An empty or unknown name resolves to "libass"; a listed name (exact match) to itself.
std::string resolveProvider(std::string_view stored, const std::vector<std::string> &providers);

// select() waits for a render in progress; prepare() and render() wait for a select().
class SubtitleRendererSelection : public application::SubtitleRendererPort {
public:
    using Log = std::function<void(const std::string &)>;
    SubtitleRendererSelection();
    ~SubtitleRendererSelection() override;

    void setLog(Log log) { m_log = std::move(log); }
    // Windows only; read the first time a CSRI renderer is needed.
    void setCsriFolder(std::filesystem::path folder);

    // In orderedProviders() order.
    std::vector<std::string> providers();
    // Returns whether the renderer changed; if so, the subtitles must be prepared again.
    bool select(const std::string &provider);
    bool usesLibass() const { return m_active == &m_libass; }
    // The resolved name (resolveProvider), not the stored one.
    const std::string &provider() const { return m_provider; }
    // "libass" or the CSRI renderer's name; empty when there is none.
    std::string activeName() const;
    // libass only; CSRI cannot count what it draws.
    void countCost(bool on) { m_libass.countCost(on); }
    // Called per frame on the rendering threads, possibly concurrently; null clears it.
    void observeCost(std::function<void(double milliseconds)> observer);

    std::expected<std::uint64_t, application::RenderError> prepare(application::RenderSnapshot snapshot) override;
    std::expected<application::OverlayFrame, application::RenderError> render(core::DocumentTime time, int width,
                                                                             int height) override;
    // libass draws at width x height; a CSRI renderer at the video's size.
    std::expected<application::OverlayFrame, application::RenderError>
    renderScaled(core::DocumentTime time, int width, int height, int videoWidth, int videoHeight) override;

private:
    std::shared_ptr<CsriRenderers> csriRenderers();
    std::shared_mutex m_switch;
    LibassRenderer m_libass;
    application::SubtitleRendererPort *m_active = &m_libass;
    std::string m_provider = "libass";
    Log m_log;
    std::filesystem::path m_csriFolder;
    std::function<void(double)> m_costObserver;
#ifdef _WIN32
    std::shared_ptr<CsriRenderers> m_csriRenderers;
    std::unique_ptr<CsriRenderer> m_csri;
    std::string m_missingLogged; // the uninstalled name last logged
#endif
};

} // namespace hikari::backends
