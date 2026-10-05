#include "hikari/backends/subtitle_renderer_selection.h"

#ifdef _WIN32
#include "hikari/backends/csri_renderer.h"
#endif

namespace hikari::backends {

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
    std::vector<std::string> list;
#ifdef _WIN32
    list = csriRenderers()->names();
#endif
    list.emplace_back("libass");
    return list;
}

bool SubtitleRendererSelection::select(const std::string &provider)
{
    const std::string chosen = provider.empty() ? std::string("libass") : provider;
#ifdef _WIN32
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
    (void)chosen;
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
    return m_active->prepare(std::move(snapshot));
}

std::expected<application::OverlayFrame, application::RenderError>
SubtitleRendererSelection::render(core::DocumentTime time, int width, int height)
{
    return m_active->render(time, width, height);
}

} // namespace hikari::backends
