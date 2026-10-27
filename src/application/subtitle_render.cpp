#include "hikari/application/subtitle_render.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace hikari::application {

namespace {
std::atomic<std::uint64_t> g_fontReloads{0};
}

void reloadSubtitleFonts()
{
    g_fontReloads.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t subtitleFontsReloads()
{
    return g_fontReloads.load(std::memory_order_relaxed);
}

void compositeOverlay(const OverlayFrame &overlay, std::uint8_t *bgra, int width, int height, std::ptrdiff_t stride)
{
    if (overlay.empty || overlay.width != width || overlay.height != height || overlay.pixels.empty())
        return;
    for (int y = 0; y < height; ++y) {
        std::uint8_t *dst = bgra + y * stride;
        const std::uint8_t *src = overlay.pixels.data() + static_cast<std::ptrdiff_t>(y) * overlay.stride;
        for (int x = 0; x < width * 4; x += 4) {
            std::uint32_t word;
            std::memcpy(&word, src + x, 4);
            if (!word)
                continue; // nothing drawn here
            const unsigned inv = 255u - src[x + 3];
            for (int c = 0; c < 3; ++c)
                dst[x + c] = static_cast<std::uint8_t>(std::min(255u, src[x + c] + (dst[x + c] * inv + 127u) / 255u));
        }
    }
}

OverlaySize overlaySizeFor(int videoWidth, int videoHeight, std::optional<std::pair<int, int>> screen)
{
    OverlaySize size{videoWidth, videoHeight, videoWidth, videoHeight};
    if (!screen || screen->first <= 0 || screen->second <= 0 || videoWidth <= 0 || videoHeight <= 0)
        return size;
    // At most 8K (a zoomed picture's whole frame is larger than the screen):
    // past it the overlay is scaled up with the picture again.
    constexpr double kMaxWidth = 7680, kMaxHeight = 4320;
    const double fit = std::min({1.0, kMaxWidth / screen->first, kMaxHeight / screen->second});
    size.width = std::max(1, static_cast<int>(screen->first * fit + 0.5));
    size.height = std::max(1, static_cast<int>(screen->second * fit + 0.5));
    return size;
}

} // namespace hikari::application
