#include "hikari/core/frame_shrink.h"

#include <algorithm>
#include <cstdint>

namespace hikari::core {

ShrunkFrame shrinkFrame(const std::byte *bgra, int width, int height, std::size_t stride, int maxEdge, bool luma)
{
    const int edge = std::max(width, height);
    const int w = edge <= maxEdge ? width : std::max(1, int((std::int64_t(width) * maxEdge + edge / 2) / edge));
    const int h = edge <= maxEdge ? height : std::max(1, int((std::int64_t(height) * maxEdge + edge / 2) / edge));
    ShrunkFrame out{w, h, std::string(std::size_t(w) * h * (luma ? 1 : 4), '\0')};
    for (int y = 0; y < h; ++y) {
        const int y0 = int(std::int64_t(y) * height / h), y1 = std::max(y0 + 1, int(std::int64_t(y + 1) * height / h));
        for (int x = 0; x < w; ++x) {
            const int x0 = int(std::int64_t(x) * width / w), x1 = std::max(x0 + 1, int(std::int64_t(x + 1) * width / w));
            std::int64_t sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy) {
                const std::byte *p = bgra + std::size_t(sy) * stride + std::size_t(x0) * 4;
                for (int sx = x0; sx < x1; ++sx, p += 4)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += std::to_integer<int>(p[c]);
            }
            const std::int64_t count = std::int64_t(x1 - x0) * (y1 - y0);
            const std::size_t at = std::size_t(y) * w + x;
            if (luma) {
                const std::int64_t b = sum[0], g = sum[1], r = sum[2];
                out.bytes[at] = char(((77 * r + 150 * g + 29 * b) / count + 128) >> 8);
            } else {
                for (int c = 0; c < 4; ++c)
                    out.bytes[at * 4 + c] = char((sum[c] + count / 2) / count);
            }
        }
    }
    return out;
}

}
