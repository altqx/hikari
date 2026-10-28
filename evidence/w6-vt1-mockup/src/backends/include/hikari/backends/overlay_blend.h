#pragma once

// The subtitle adapters' pixel work on premultiplied BGRA overlays.
//
// blendAssBitmap is legacy BlendAssBitmap (HikariSub/AssBlend.cpp): one
// libass bitmap (coverage bytes) blended in its colour (libass 0xRRGGBBAA,
// AA the transparency) over BGRA pixels with the alpha kept premultiplied,
// every division by 255 rounded. Its results are legacy's to the byte on
// every path; the path is chosen once at run time (AVX2 where the CPU and
// the OS have it, else SSE2 on x86, else plain C++).
//
// premultiplyStraightAlpha is the CSRI adapter's pass over what a renderer
// that takes BGRA drew (straight alpha) into a cleared frame.

#include "hikari/application/subtitle_render.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace hikari::backends {

enum class BlendPath { Plain, Sse2, Avx2 };

// The path blendAssBitmap takes on this machine.
BlendPath blendPath();
// Whether `path` can run on this machine (Plain always can).
bool blendPathAvailable(BlendPath path);

// `dst` and `src` point at the first pixel and coverage byte; the caller
// has clipped the bitmap to the frame.
void blendAssBitmap(std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src, std::ptrdiff_t srcStride,
                    int width, int height, std::uint32_t color);
// The same on a named path (tests and measurements); `path` must be available.
void blendAssBitmap(BlendPath path, std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src,
                    std::ptrdiff_t srcStride, int width, int height, std::uint32_t color);

// Boxes cover what was drawn into a buffer, so the next use clears only them.
// A box that overlaps a recent one is merged into it when that does not
// clear more than the two apart, which keeps the many boxes of one event
// (fill, border and shadow of each glyph run) down to a few.
class DrawnBoxes {
public:
    void add(const application::PixelRect &box);
    void clear() { m_boxes.clear(); }
    bool empty() const { return m_boxes.empty(); }
    const std::vector<application::PixelRect> &boxes() const { return m_boxes; }
    // Zeroes every box in `pixels` (BGRA, `stride` bytes a row).
    void zero(std::uint8_t *pixels, std::ptrdiff_t stride) const;

private:
    std::vector<application::PixelRect> m_boxes;
};

// An adapter's overlay buffers of one size, shared with the frames it
// returns (application::OverlayPixels). A buffer is drawn into again only
// once nobody else holds it; until then another one is used. The latest few
// are kept, the shown one last.
class OverlayBuffers {
public:
    struct Buffer {
        std::shared_ptr<application::OverlayPixels::Bytes> bytes;
        DrawnBoxes drawn; // what was drawn into it last
    };
    // A new size drops the buffers (returns true then).
    bool setSize(int width, int height);
    // A buffer nobody else holds, with what it drew last cleared, which
    // becomes the shown one.
    Buffer &acquire();
    // The shown buffer, null before the first acquire after a new size.
    const Buffer *shown() const { return m_buffers.empty() ? nullptr : &m_buffers.back(); }
    std::size_t count() const { return m_buffers.size(); }
    std::ptrdiff_t stride() const { return static_cast<std::ptrdiff_t>(m_width) * 4; }

private:
    std::vector<Buffer> m_buffers;
    int m_width = 0, m_height = 0;
};

// The bounds of two rectangles; an empty one (no width or height) adds nothing.
application::PixelRect unite(const application::PixelRect &a, const application::PixelRect &b);

// Straight-alpha BGRA to premultiplied, in place: (colour * alpha + 127) / 255,
// colour 0 where alpha is 0. Skips the empty 8-byte words. Returns the bounds
// of the pixels whose alpha is not 0 (empty when there are none).
application::PixelRect premultiplyStraightAlpha(std::uint8_t *pixels, int width, int height, std::ptrdiff_t stride);

} // namespace hikari::backends
