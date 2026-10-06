#include "hikari/backends/csri_renderer.h"

#include <algorithm>
#include <cstring>

namespace hikari::backends {

using application::OverlayFrame;
using application::RenderError;

namespace {
constexpr int kMaxDimension = 16384;
}

CsriRenderers::CsriRenderers(std::vector<csri::Renderer> renderers, std::vector<std::shared_ptr<void>> libraries)
    : m_renderers(std::move(renderers)), m_libraries(std::move(libraries))
{
}

CsriRenderers::~CsriRenderers() = default;

std::vector<std::string> CsriRenderers::names() const
{
    // SubtitlesVSFilter::GetProviders (SubtitlesVSFilter.cpp:252-279): none
    // when the default renderer has no info, then each one that has.
    std::vector<std::string> names;
    if (m_renderers.empty() || !m_renderers.front().info)
        return names;
    for (const auto &r : m_renderers)
        if (r.info && r.info->name)
            names.emplace_back(r.info->name);
    return names;
}

const csri::Renderer *CsriRenderers::select(std::string_view name) const
{
    // GetVSFilter (SubtitlesVSFilter.cpp:227-250): the default, unless a name
    // is set; then the walk stops at the renderer with that name, or at one
    // without info (which is then used), and falls back to the default when
    // the list ends.
    if (m_renderers.empty())
        return nullptr;
    if (name.empty() || !m_renderers.front().info)
        return &m_renderers.front();
    for (const auto &r : m_renderers) {
        if (!r.info)
            return &r;
        if (r.info->name && name == r.info->name)
            return &r;
    }
    return &m_renderers.front();
}

CsriRenderer::CsriRenderer(std::shared_ptr<CsriRenderers> renderers, std::string name, CsriRenderers::Log log)
    : m_renderers(std::move(renderers)), m_name(std::move(name)), m_log(std::move(log))
{
}

CsriRenderer::~CsriRenderer()
{
    if (m_renderers) {
        std::lock_guard lock(m_renderers->mutex());
        closeInstance();
    }
}

void CsriRenderer::closeInstance()
{
    if (m_instance && m_renderer && m_renderer->close)
        m_renderer->close(m_instance);
    m_instance = nullptr;
    m_width = m_height = 0;
}

std::string CsriRenderer::selectedName() const
{
    return m_renderer && m_renderer->info && m_renderer->info->name ? std::string(m_renderer->info->name)
                                                                    : std::string();
}

std::expected<std::uint64_t, RenderError> CsriRenderer::prepare(application::RenderSnapshot snapshot)
{
    if (!m_renderers) {
        if (m_log)
            m_log("Cannot initialize CSRI.");
        return std::unexpected(RenderError::BackendFailure);
    }
    std::lock_guard lock(m_renderers->mutex());
    closeInstance(); // Open: the previous instance is no longer shown
    const csri::Renderer *renderer = m_renderers->select(m_name);
    if (!renderer || !renderer->openMem) {
        m_renderer = nullptr;
        if (m_log)
            m_log("Cannot initialize CSRI.");
        return std::unexpected(RenderError::BackendFailure);
    }
    m_renderer = renderer;
    // ParseInstance: csri_open_mem(mb_str(wxConvUTF8), strlen), so the script
    // ends at its first NUL.
    const auto *bytes = reinterpret_cast<const char *>(snapshot.script.data());
    const std::size_t length = snapshot.script.empty() ? 0 : strnlen(bytes, snapshot.script.size());
    m_instance = renderer->openMem(renderer->rend, bytes, length, nullptr);
    if (!m_instance) {
        if (m_log)
            m_log("Cannot create CSRI instance.");
        return std::unexpected(RenderError::InvalidInput);
    }
    m_pixfmt = csri::BGRA;
    return ++m_generation;
}

std::expected<OverlayFrame, RenderError> CsriRenderer::render(core::DocumentTime time, int width, int height)
{
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
        return std::unexpected(RenderError::InvalidSize);
    if (!m_renderers)
        return std::unexpected(RenderError::NoSnapshot);
    std::lock_guard lock(m_renderers->mutex());
    if (!m_instance)
        return std::unexpected(RenderError::NoSnapshot);
    if (width != m_width || height != m_height) {
        // SetVideoParameters with ARGB32: BGRA, else BGR_.
        csri::Fmt fmt{csri::BGRA, static_cast<unsigned>(width), static_cast<unsigned>(height)};
        m_pixfmt = csri::BGRA;
        if (m_renderer->requestFmt(m_instance, &fmt) != 0) {
            m_pixfmt = fmt.pixfmt = csri::BGR_;
            if (m_renderer->requestFmt(m_instance, &fmt) != 0) {
                if (m_log)
                    m_log("CSRI does not support this format.");
                closeInstance();
                return std::unexpected(RenderError::BackendFailure);
            }
        }
        m_width = width;
        m_height = height;
    }
    OverlayFrame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.generation = m_generation;
    // DrawOverlay: a cleared overlay, drawn whole every time.
    frame.pixels.assign(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height), 0);
    csri::Frame target{};
    target.pixfmt = m_pixfmt;
    target.planes[0] = frame.pixels.data();
    target.strides[0] = frame.stride;
    // Draw: csri_render(instance, frame, double(time / 1000.0)) with the
    // frame's time in integer milliseconds.
    const auto ms = static_cast<int>(time.microseconds() / 1000);
    m_renderer->render(m_instance, &target, double(ms / 1000.0));
    bool drawn = false;
    for (std::size_t i = 0; i < frame.pixels.size(); i += 4) {
        std::uint8_t *p = &frame.pixels[i];
        const unsigned a = p[3];
        if (a == 0) {
            p[0] = p[1] = p[2] = 0;
            continue;
        }
        drawn = true;
        if (a != 255)
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<std::uint8_t>((p[c] * a + 127) / 255);
    }
    frame.empty = !drawn;
    frame.changed.push_back({0, 0, width, height});
    return frame;
}

} // namespace hikari::backends
