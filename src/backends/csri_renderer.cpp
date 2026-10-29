#include "hikari/backends/csri_renderer.h"
#include "hikari/backends/overlay_blend.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hikari::backends {

using application::OverlayFrame;
using application::PixelRect;
using application::RenderError;

namespace {
constexpr int kMaxDimension = 16384;

#ifdef _WIN32
// AddFontMemResourceEx fonts are private and not enumerable; VSFilter finds them by family name.
std::shared_ptr<void> addPrivateFont(const application::FontLease &font)
{
    if (!font.bytes || font.bytes->empty())
        return nullptr;
    DWORD faces = 0;
    HANDLE handle = AddFontMemResourceEx(const_cast<std::byte *>(font.bytes->data()),
                                         static_cast<DWORD>(font.bytes->size()), nullptr, &faces);
    if (!handle)
        return nullptr;
    return std::shared_ptr<void>(handle, [](void *h) { RemoveFontMemResourceEx(h); });
}
#endif

} // namespace

CsriRenderers::CsriRenderers(std::vector<csri::Renderer> renderers, std::vector<std::shared_ptr<void>> libraries)
    : m_renderers(std::move(renderers)), m_libraries(std::move(libraries))
{
#ifdef _WIN32
    m_fontLoader = addPrivateFont;
#endif
}

std::shared_ptr<void> CsriRenderers::loadFont(const application::FontLease &font) const
{
    return m_fontLoader ? m_fontLoader(font) : nullptr;
}

CsriRenderers::~CsriRenderers() = default;

std::vector<std::string> CsriRenderers::names() const
{
    std::vector<std::string> names;
    // MUST KILL: no names at all when the default renderer has no info.
    if (m_renderers.empty() || !m_renderers.front().info)
        return names;
    for (const auto &r : m_renderers)
        if (r.info && r.info->name)
            names.emplace_back(r.info->name);
    return names;
}

const csri::Renderer *CsriRenderers::select(std::string_view name) const
{
    if (m_renderers.empty())
        return nullptr;
    if (name.empty() || !m_renderers.front().info)
        return &m_renderers.front();
    for (const auto &r : m_renderers) {
        // MUST KILL: a renderer without info is taken before any later name match.
        if (!r.info)
            return &r;
        if (r.info->name && name == r.info->name)
            return &r;
    }
    return &m_renderers.front();
}

CsriRenderer::CsriRenderer(std::shared_ptr<CsriRenderers> renderers, std::string name, CsriRenderers::Log log)
    : m_renderers(std::move(renderers)), m_name(std::move(name)), m_log(std::move(log)),
      m_fontReloads(application::subtitleFontsReloads())
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
    closeInstance();
    const std::uint64_t reloads = application::subtitleFontsReloads();
    const bool reloaded = reloads != m_fontReloads;
    m_fontReloads = reloads;
    const csri::Renderer *renderer = m_renderers->select(m_name);
    if (!renderer || !renderer->openMem) {
        m_renderer = nullptr;
        m_fonts.clear();
        if (m_log)
            m_log("Cannot initialize CSRI.");
        return std::unexpected(RenderError::BackendFailure);
    }
    m_renderer = renderer;
    std::vector<AddedFont> fonts;
    for (const auto &font : snapshot.fonts) {
        if (!font.bytes)
            continue;
        const auto kept = reloaded ? m_fonts.end() : std::ranges::find(m_fonts, font.bytes, &AddedFont::bytes);
        if (kept != m_fonts.end())
            fonts.push_back(*kept);
        else if (auto handle = m_renderers->loadFont(font))
            fonts.push_back({font.bytes, std::move(handle)});
    }
    // MUST KILL: new leases must be added before this drops the old, or shared fonts vanish.
    m_fonts = std::move(fonts);
    // Without a UTF-8 BOM VSFilter reads the ANSI code page and VSFilterMod rejects some scripts.
    const auto *bytes = reinterpret_cast<const char *>(snapshot.script.data());
    const std::size_t length = snapshot.script.empty() ? 0 : strnlen(bytes, snapshot.script.size());
    constexpr std::string_view bom = "\xEF\xBB\xBF";
    std::string text;
    if (!std::string_view(bytes, length).starts_with(bom)) {
        text.reserve(bom.size() + length);
        text.append(bom).append(bytes, length);
    }
    const std::string_view script = text.empty() ? std::string_view(bytes, length) : std::string_view(text);
    try {
        m_instance = renderer->openMem(renderer->rend, script.data(), script.size(), nullptr);
    } catch (...) {
        // VSFilterMod throws MFC exceptions on scripts it cannot open.
        m_instance = nullptr;
    }
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
        // VSFilterMod r5.2.6 refuses BGRA; r5.3.1-hikari.2 draws it with straight alpha.
        csri::Fmt fmt{csri::BGRA, static_cast<unsigned>(width), static_cast<unsigned>(height)};
        m_pixfmt = fmt.pixfmt;
        bool accepted = m_renderer->requestFmt(m_instance, &fmt) == 0;
        if (!accepted) {
            m_pixfmt = fmt.pixfmt = csri::BGR_;
            accepted = m_renderer->requestFmt(m_instance, &fmt) == 0;
        }
        if (!accepted) {
            if (m_log)
                m_log("CSRI does not support this format.");
            closeInstance();
            return std::unexpected(RenderError::BackendFailure);
        }
        m_width = width;
        m_height = height;
    }
    OverlayFrame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.generation = m_generation;
    const bool resized = m_buffers.setSize(width, height);
    if (resized)
        m_bounds = {};
    OverlayBuffers::Buffer &buffer = m_buffers.acquire();
    csri::Frame target{};
    target.pixfmt = m_pixfmt;
    target.planes[0] = buffer.bytes->data();
    target.strides[0] = frame.stride;
    const auto ms = static_cast<int>(time.microseconds() / 1000);
    m_renderer->render(m_instance, &target, double(ms / 1000.0));
    PixelRect bounds;
    if (m_pixfmt == csri::BGR_) {
        // BGR_ blends with no alpha: the same instant drawn onto black and white gives coverage.
        std::vector<std::uint8_t> &white = m_white;
        white.resize(buffer.bytes->size());
        for (std::size_t i = 0; i < white.size(); i += 4) {
            white[i] = white[i + 1] = white[i + 2] = 255;
            white[i + 3] = 0;
        }
        target.planes[0] = white.data();
        m_renderer->render(m_instance, &target, double(ms / 1000.0));
        int left = width, top = height, right = -1, bottom = -1;
        for (int y = 0; y < height; ++y) {
            std::uint8_t *row = buffer.bytes->data() + static_cast<std::size_t>(y) * frame.stride;
            const std::uint8_t *whiteRow = white.data() + static_cast<std::size_t>(y) * frame.stride;
            for (int x = 0; x < width; ++x) {
                std::uint8_t *p = row + 4 * x;
                const std::uint8_t *w = whiteRow + 4 * x;
                int through = 0;
                for (int c = 0; c < 3; ++c)
                    through += (std::max)(0, int(w[c]) - int(p[c]));
                const int a = 255 - (through + 1) / 3;
                if (a <= 0) {
                    p[0] = p[1] = p[2] = p[3] = 0;
                    continue;
                }
                for (int c = 0; c < 3; ++c)
                    p[c] = static_cast<std::uint8_t>((std::min)(int(p[c]), a));
                p[3] = static_cast<std::uint8_t>(a);
                left = (std::min)(left, x);
                right = (std::max)(right, x);
                top = (std::min)(top, y);
                bottom = (std::max)(bottom, y);
            }
        }
        if (right >= 0)
            bounds = {left, top, right - left + 1, bottom - top + 1};
    } else {
        bounds = premultiplyStraightAlpha(buffer.bytes->data(), width, height, frame.stride);
    }
    buffer.drawn.add(bounds);
    frame.pixels = application::OverlayPixels(buffer.bytes);
    frame.empty = bounds.width <= 0;
    if (!frame.empty)
        frame.bounds = bounds;
    if (resized) {
        frame.changed.push_back({0, 0, width, height});
    } else {
        if (m_bounds.width > 0)
            frame.changed.push_back(m_bounds);
        if (bounds.width > 0 && bounds != m_bounds)
            frame.changed.push_back(bounds);
    }
    m_bounds = bounds;
    return frame;
}

} // namespace hikari::backends
