#include "hikari/backends/subtitle_renderer_selection.h"

#ifdef _WIN32
#include "hikari/backends/csri_renderer.h"
#endif

#include <chrono>
#include <algorithm>
#include <iterator>

namespace hikari::backends {

bool isVsfilterMod(std::string_view name)
{
    return name == kVsfilterModRenderer;
}

std::vector<std::string> orderedProviders(const std::vector<std::string> &csriNames)
{
    std::vector<std::string> list{"libass"};
    std::ranges::copy_if(csriNames, std::back_inserter(list), [](const std::string &n) { return !isVsfilterMod(n); });
    std::ranges::copy_if(csriNames, std::back_inserter(list), [](const std::string &n) { return isVsfilterMod(n); });
    return list;
}

std::string resolveProvider(std::string_view stored, const std::vector<std::string> &providers)
{
    if (!stored.empty() && std::ranges::find(providers, stored) != providers.end())
        return std::string(stored);
    return "libass";
}

SubtitleRendererSelection::SubtitleRendererSelection() = default;
SubtitleRendererSelection::~SubtitleRendererSelection() = default;

void SubtitleRendererSelection::setCsriFolder(std::filesystem::path folder)
{
    m_csriFolder = std::move(folder);
}

std::shared_ptr<CsriRenderers> SubtitleRendererSelection::csriRenderers()
{
#ifdef _WIN32
    if (!m_csriRenderers) {
        const Log log = [this](const std::string &text) {
            if (m_log)
                m_log(text);
        };
        m_csriRenderers = m_csriFolder.empty() ? std::make_shared<CsriRenderers>(std::vector<csri::Renderer>{})
                                               : CsriRenderers::load(m_csriFolder, log);
    }
    return m_csriRenderers;
#else
    return nullptr;
#endif
}

std::vector<std::string> SubtitleRendererSelection::providers()
{
#ifdef _WIN32
    return orderedProviders(csriRenderers()->names());
#else
    return orderedProviders({});
#endif
}

bool SubtitleRendererSelection::select(const std::string &provider)
{
#ifdef _WIN32
    // libass loads no CSRI renderer; any other name must be installed.
    const std::string chosen = provider.empty() || provider == "libass" ? std::string("libass")
                                                                        : resolveProvider(provider, providers());
    // Logged once per name, not on every settings write that repeats it.
    if (chosen == provider || provider.empty()) {
        m_missingLogged.clear();
    } else if (provider != m_missingLogged) {
        m_missingLogged = provider;
        if (m_log)
            m_log("The subtitle renderer " + provider + " is not installed; libass is used.");
    }
    std::unique_lock lock(m_switch); // no render on another thread meanwhile
    if (chosen == m_provider)
        return false;
    m_provider = chosen;
    if (chosen == "libass") {
        m_csri.reset();
        m_active = &m_libass;
        return true;
    }
    const Log log = [this](const std::string &text) {
        if (m_log)
            m_log(text);
    };
    m_csri = std::make_unique<CsriRenderer>(csriRenderers(), chosen, log);
    m_active = m_csri.get();
    return true;
#else
    (void)provider;
    return false; // libass only
#endif
}

std::string SubtitleRendererSelection::activeName() const
{
#ifdef _WIN32
    if (m_csri)
        return m_csri->selectedName();
#endif
    return "libass";
}

std::expected<std::uint64_t, application::RenderError>
SubtitleRendererSelection::prepare(application::RenderSnapshot snapshot)
{
    std::shared_lock lock(m_switch);
    return m_active->prepare(std::move(snapshot));
}

std::expected<application::OverlayFrame, application::RenderError>
SubtitleRendererSelection::render(core::DocumentTime time, int width, int height)
{
    return renderScaled(time, width, height, width, height);
}

std::expected<application::OverlayFrame, application::RenderError>
SubtitleRendererSelection::renderScaled(core::DocumentTime time, int width, int height, int videoWidth,
                                        int videoHeight)
{
    std::shared_lock lock(m_switch);
    const auto started = std::chrono::steady_clock::now();
    auto frame = application::renderOverlay(*m_active, time, {width, height, videoWidth, videoHeight});
    if (frame)
        frame->cost.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() -
            frame->cost.countMilliseconds;
    return frame;
}

} // namespace hikari::backends
