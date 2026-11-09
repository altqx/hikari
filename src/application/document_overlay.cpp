#include "hikari/application/document_overlay.h"

#include <algorithm>
#include <utility>

namespace hikari::application {

DocumentOverlay::DocumentOverlay(SubtitleRendererPort &renderer, Encode encode)
    : m_renderer(renderer), m_encode(std::move(encode))
{
}

std::expected<OverlayFrame, RenderError> DocumentOverlay::render(const core::Document &document,
                                                                 std::vector<FontLease> fonts, core::DocumentTime time,
                                                                 int width, int height)
{
    auto script = m_encode(document);
    const auto same = [](const FontLease &a, const FontLease &b) { return a.family == b.family && a.bytes == b.bytes; };
    if (!m_script || *m_script != script || !std::ranges::equal(fonts, m_fonts, same)) {
        m_script.reset();
        RenderSnapshot snapshot;
        snapshot.script = script;
        snapshot.fonts = fonts;
        if (const auto prepared = m_renderer.prepare(std::move(snapshot)); !prepared)
            return std::unexpected(prepared.error());
        m_script = std::move(script);
        m_fonts = std::move(fonts);
    }
    return m_renderer.render(time, width, height);
}

core::Document withOnlyLine(const core::Document &document, core::LineId line)
{
    core::Document only = document;
    only.removeLinesIf([line](const core::LineRecord &record) { return record.id != line; });
    return only;
}

std::optional<PixelRect> inkBounds(const OverlayFrame &overlay)
{
    if (overlay.empty || overlay.width <= 0 || overlay.height <= 0 || overlay.stride < overlay.width * 4 ||
        overlay.pixels.size() < static_cast<std::size_t>(overlay.stride) * static_cast<std::size_t>(overlay.height))
        return std::nullopt;
    PixelRect area{0, 0, overlay.width, overlay.height};
    if (overlay.bounds) {
        const int x0 = std::max(0, overlay.bounds->x), y0 = std::max(0, overlay.bounds->y);
        const int x1 = std::min(overlay.width, overlay.bounds->x + overlay.bounds->width);
        const int y1 = std::min(overlay.height, overlay.bounds->y + overlay.bounds->height);
        if (x0 >= x1 || y0 >= y1)
            return std::nullopt;
        area = {x0, y0, x1 - x0, y1 - y0};
    }
    int left = area.x + area.width, right = -1, top = -1, bottom = -1;
    for (int y = area.y; y < area.y + area.height; ++y) {
        const std::uint8_t *row = overlay.pixels.data() + static_cast<std::size_t>(y) * overlay.stride;
        for (int x = area.x; x < area.x + area.width; ++x)
            if (row[x * 4 + 3] != 0) {
                left = std::min(left, x);
                right = std::max(right, x);
                if (top < 0)
                    top = y;
                bottom = y;
            }
    }
    if (right < 0)
        return std::nullopt;
    return PixelRect{left, top, right - left + 1, bottom - top + 1};
}

}
