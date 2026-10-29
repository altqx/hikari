#pragma once

#include "hikari/application/subtitle_render.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace hikari::backends {

enum class BlendPath { Plain, Sse2, Avx2 };

BlendPath blendPath();
bool blendPathAvailable(BlendPath path);

// `color` is libass's 0xRRGGBBAA (AA = transparency) over premultiplied BGRA; the caller
// clips the bitmap to the frame.
void blendAssBitmap(std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src, std::ptrdiff_t srcStride,
                    int width, int height, std::uint32_t color);
// `path` must be available.
void blendAssBitmap(BlendPath path, std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src,
                    std::ptrdiff_t srcStride, int width, int height, std::uint32_t color);

class DrawnBoxes {
public:
    void add(const application::PixelRect &box);
    void clear() { m_boxes.clear(); }
    bool empty() const { return m_boxes.empty(); }
    const std::vector<application::PixelRect> &boxes() const { return m_boxes; }
    void zero(std::uint8_t *pixels, std::ptrdiff_t stride) const;

private:
    std::vector<application::PixelRect> m_boxes;
};

class OverlayBuffers {
public:
    struct Buffer {
        std::shared_ptr<application::OverlayPixels::Bytes> bytes;
        DrawnBoxes drawn;
    };
    // True when the size changed and the buffers were dropped.
    bool setSize(int width, int height);
    // A buffer no frame still holds, its last drawing cleared; it becomes shown().
    Buffer &acquire();
    // Null before the first acquire() after a new size.
    const Buffer *shown() const { return m_buffers.empty() ? nullptr : &m_buffers.back(); }
    std::size_t count() const { return m_buffers.size(); }
    std::ptrdiff_t stride() const { return static_cast<std::ptrdiff_t>(m_width) * 4; }

private:
    std::vector<Buffer> m_buffers;
    int m_width = 0, m_height = 0;
};

// An empty rectangle adds nothing.
application::PixelRect unite(const application::PixelRect &a, const application::PixelRect &b);

// In place; returns the bounds of the pixels with nonzero alpha (empty when none).
application::PixelRect premultiplyStraightAlpha(std::uint8_t *pixels, int width, int height, std::ptrdiff_t stride);

}
