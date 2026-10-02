#include "hikari/backends/libass_renderer.h"

extern "C" {
#include <ass/ass.h>
}

#include <algorithm>

namespace hikari::backends {

using application::OverlayFrame;
using application::PixelRect;
using application::RenderError;

namespace {

constexpr int kMaxDimension = 16384;

PixelRect bounds(const std::vector<PixelRect> &rects)
{
    if (rects.empty())
        return {};
    int x0 = rects[0].x, y0 = rects[0].y, x1 = x0 + rects[0].width, y1 = y0 + rects[0].height;
    for (const auto &r : rects) {
        x0 = std::min(x0, r.x);
        y0 = std::min(y0, r.y);
        x1 = std::max(x1, r.x + r.width);
        y1 = std::max(y1, r.y + r.height);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace

struct LibassRenderer::Context {
    ASS_Library *library = nullptr;
    ASS_Renderer *renderer = nullptr;
    ASS_Track *track = nullptr;
    std::vector<application::FontLease> fonts; // leases held for the context's lifetime
    std::vector<PixelRect> lastRects;          // what the previous frame covered
    int lastWidth = 0, lastHeight = 0;
    std::vector<std::uint8_t> lastPixels;

    ~Context()
    {
        if (track)
            ass_free_track(track);
        if (renderer)
            ass_renderer_done(renderer);
        if (library)
            ass_library_done(library);
    }
};

LibassRenderer::LibassRenderer() = default;
LibassRenderer::~LibassRenderer() = default;

std::expected<std::uint64_t, RenderError> LibassRenderer::prepare(application::RenderSnapshot snapshot)
{
    std::lock_guard lock(m_mutex);
    auto context = std::make_unique<Context>();
    context->library = ass_library_init();
    if (!context->library)
        return std::unexpected(RenderError::BackendFailure);
    for (const auto &font : snapshot.fonts) {
        if (!font.bytes)
            continue;
        ass_add_font(context->library, font.family.c_str(),
                     const_cast<char *>(reinterpret_cast<const char *>(font.bytes->data())),
                     static_cast<int>(font.bytes->size()));
    }
    context->fonts = std::move(snapshot.fonts);
    context->renderer = ass_renderer_init(context->library);
    if (!context->renderer)
        return std::unexpected(RenderError::BackendFailure);
    ass_set_fonts(context->renderer, nullptr,
                  snapshot.defaultFamily.empty() ? nullptr : snapshot.defaultFamily.c_str(),
                  snapshot.systemFonts ? ASS_FONTPROVIDER_AUTODETECT : ASS_FONTPROVIDER_NONE, nullptr, 1);
    context->track = ass_read_memory(context->library,
                                     const_cast<char *>(reinterpret_cast<const char *>(snapshot.script.data())),
                                     snapshot.script.size(), nullptr);
    if (!context->track)
        return std::unexpected(RenderError::InvalidInput);
    m_context = std::move(context); // the previous context and its leases go here
    return ++m_generation;
}

std::expected<OverlayFrame, RenderError> LibassRenderer::render(core::DocumentTime time, int width, int height)
{
    std::lock_guard lock(m_mutex);
    if (!m_context)
        return std::unexpected(RenderError::NoSnapshot);
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
        return std::unexpected(RenderError::InvalidSize);
    Context &c = *m_context;
    const bool resized = width != c.lastWidth || height != c.lastHeight;
    if (resized) {
        ass_set_frame_size(c.renderer, width, height);
        ass_set_storage_size(c.renderer, width, height);
    }
    int change = 0;
    const long long ms = time.microseconds() / 1000; // libass times are milliseconds
    ASS_Image *images = ass_render_frame(c.renderer, c.track, ms, &change);

    OverlayFrame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.generation = m_generation;
    if (change == 0 && !resized) {
        // Nothing moved: the previous pixels are still current.
        frame.pixels = c.lastPixels;
        frame.empty = c.lastRects.empty();
        return frame;
    }
    frame.pixels.assign(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height), 0);
    std::vector<PixelRect> rects;
    for (ASS_Image *img = images; img; img = img->next) {
        if (img->w <= 0 || img->h <= 0)
            continue;
        rects.push_back({img->dst_x, img->dst_y, img->w, img->h});
        const int r = (img->color >> 24) & 0xff, g = (img->color >> 16) & 0xff, b = (img->color >> 8) & 0xff;
        const int opacity = 255 - static_cast<int>(img->color & 0xff);
        for (int y = 0; y < img->h; ++y) {
            const int py = img->dst_y + y;
            if (py < 0 || py >= height)
                continue;
            for (int x = 0; x < img->w; ++x) {
                const int px = img->dst_x + x;
                if (px < 0 || px >= width)
                    continue;
                const int a = img->bitmap[y * img->stride + x] * opacity / 255;
                if (a == 0)
                    continue;
                std::uint8_t *d = &frame.pixels[static_cast<std::size_t>(py) * frame.stride + px * 4];
                // Premultiplied "source over" in BGRA order.
                d[0] = static_cast<std::uint8_t>((b * a + d[0] * (255 - a)) / 255);
                d[1] = static_cast<std::uint8_t>((g * a + d[1] * (255 - a)) / 255);
                d[2] = static_cast<std::uint8_t>((r * a + d[2] * (255 - a)) / 255);
                d[3] = static_cast<std::uint8_t>(a + d[3] * (255 - a) / 255);
            }
        }
    }
    frame.empty = rects.empty();
    if (resized) {
        frame.changed.push_back({0, 0, width, height});
    } else {
        if (!c.lastRects.empty())
            frame.changed.push_back(bounds(c.lastRects));
        if (!rects.empty())
            frame.changed.push_back(bounds(rects));
    }
    c.lastRects = std::move(rects);
    c.lastWidth = width;
    c.lastHeight = height;
    c.lastPixels = frame.pixels;
    return frame;
}

} // namespace hikari::backends
