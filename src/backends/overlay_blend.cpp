#include "hikari/backends/overlay_blend.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#define HIKARI_BLEND_X86 1
#include <immintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define HIKARI_TARGET_AVX2
#else
#define HIKARI_TARGET_AVX2 __attribute__((target("avx2")))
#endif
#endif

namespace hikari::backends {

using application::PixelRect;

namespace {

inline unsigned div255(unsigned x)
{
    x += 128;
    return (x + (x >> 8)) >> 8;
}

void blendPlain(std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src, std::ptrdiff_t srcStride,
                int width, int height, std::uint32_t color, int fromX = 0)
{
    const unsigned opacity = 255 - (color & 0xff);
    const unsigned channels[4] = {(color >> 8) & 0xff, (color >> 16) & 0xff, color >> 24, 255};
    for (int y = 0; y < height; ++y) {
        std::uint8_t *row = dst + y * dstStride;
        const std::uint8_t *cover = src + y * srcStride;
        for (int x = fromX; x < width; ++x) {
            const unsigned alpha = div255(opacity * cover[x]);
            if (!alpha)
                continue;
            std::uint8_t *pixel = row + x * 4;
            for (int c = 0; c < 4; ++c)
                pixel[c] = static_cast<std::uint8_t>(div255(channels[c] * alpha + pixel[c] * (255 - alpha)));
        }
    }
}

#ifdef HIKARI_BLEND_X86

inline __m128i div255(__m128i x)
{
    x = _mm_add_epi16(x, _mm_set1_epi16(128));
    return _mm_srli_epi16(_mm_add_epi16(x, _mm_srli_epi16(x, 8)), 8);
}

inline void blendFour(std::uint8_t *row, std::uint32_t coverage, __m128i colour, __m128i opacity)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i full = _mm_set1_epi16(255);
    const __m128i v = _mm_unpacklo_epi8(_mm_cvtsi32_si128(static_cast<int>(coverage)), zero);
    __m128i alpha = div255(_mm_mullo_epi16(v, opacity));
    alpha = _mm_unpacklo_epi16(alpha, alpha);
    const __m128i alphaLo = _mm_unpacklo_epi32(alpha, alpha);
    const __m128i alphaHi = _mm_unpackhi_epi32(alpha, alpha);
    const __m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i *>(row));
    __m128i lo = _mm_unpacklo_epi8(pixels, zero);
    __m128i hi = _mm_unpackhi_epi8(pixels, zero);
    lo = div255(_mm_add_epi16(_mm_mullo_epi16(colour, alphaLo), _mm_mullo_epi16(lo, _mm_sub_epi16(full, alphaLo))));
    hi = div255(_mm_add_epi16(_mm_mullo_epi16(colour, alphaHi), _mm_mullo_epi16(hi, _mm_sub_epi16(full, alphaHi))));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(row), _mm_packus_epi16(lo, hi));
}

__m128i colourOf(std::uint32_t color)
{
    const short b = static_cast<short>((color >> 8) & 0xff), g = static_cast<short>((color >> 16) & 0xff),
                r = static_cast<short>(color >> 24);
    return _mm_setr_epi16(b, g, r, 255, b, g, r, 255);
}

void blendSse2(std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src, std::ptrdiff_t srcStride,
               int width, int height, std::uint32_t color)
{
    const unsigned opacity = 255 - (color & 0xff);
    const __m128i colour = colourOf(color);
    const __m128i opacityVec = _mm_set1_epi16(static_cast<short>(opacity));
    const int wide = width & ~3;
    for (int y = 0; y < height; ++y) {
        std::uint8_t *row = dst + y * dstStride;
        const std::uint8_t *cover = src + y * srcStride;
        for (int x = 0; x < wide; x += 4) {
            std::uint32_t coverage;
            std::memcpy(&coverage, cover + x, 4);
            if (coverage)
                blendFour(row + x * 4, coverage, colour, opacityVec);
        }
    }
    if (wide < width)
        blendPlain(dst, dstStride, src, srcStride, width, height, color, wide);
}

HIKARI_TARGET_AVX2 inline __m256i div255x16(__m256i x)
{
    x = _mm256_add_epi16(x, _mm256_set1_epi16(128));
    return _mm256_srli_epi16(_mm256_add_epi16(x, _mm256_srli_epi16(x, 8)), 8);
}

HIKARI_TARGET_AVX2 void blendAvx2(std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src,
                                  std::ptrdiff_t srcStride, int width, int height, std::uint32_t color)
{
    const unsigned opacity = 255 - (color & 0xff);
    const __m128i colour4 = colourOf(color);
    const __m128i opacity8 = _mm_set1_epi16(static_cast<short>(opacity));
    const __m256i colour = _mm256_broadcastsi128_si256(colour4);
    const __m256i full = _mm256_set1_epi16(255);
    const int wide8 = width & ~7;
    const int wide4 = width & ~3;
    for (int y = 0; y < height; ++y) {
        std::uint8_t *row = dst + y * dstStride;
        const std::uint8_t *cover = src + y * srcStride;
        int x = 0;
        for (; x < wide8; x += 8) {
            std::uint64_t coverage;
            std::memcpy(&coverage, cover + x, 8);
            if (!coverage)
                continue;
            const __m128i v = _mm_cvtepu8_epi16(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(cover + x)));
            __m128i alpha = div255(_mm_mullo_epi16(v, opacity8));
            const __m128i pairsLo = _mm_unpacklo_epi16(alpha, alpha);
            const __m128i pairsHi = _mm_unpackhi_epi16(alpha, alpha);
            const __m256i alpha03 = _mm256_set_m128i(_mm_unpackhi_epi32(pairsLo, pairsLo),
                                                     _mm_unpacklo_epi32(pairsLo, pairsLo));
            const __m256i alpha47 = _mm256_set_m128i(_mm_unpackhi_epi32(pairsHi, pairsHi),
                                                     _mm_unpacklo_epi32(pairsHi, pairsHi));
            std::uint8_t *at = row + x * 4;
            const __m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(at));
            __m256i lo = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(pixels));
            __m256i hi = _mm256_cvtepu8_epi16(_mm256_extracti128_si256(pixels, 1));
            lo = div255x16(_mm256_add_epi16(_mm256_mullo_epi16(colour, alpha03),
                                            _mm256_mullo_epi16(lo, _mm256_sub_epi16(full, alpha03))));
            hi = div255x16(_mm256_add_epi16(_mm256_mullo_epi16(colour, alpha47),
                                            _mm256_mullo_epi16(hi, _mm256_sub_epi16(full, alpha47))));
            // packus packs within each 128-bit lane: put the halves back in order.
            const __m256i packed = _mm256_permute4x64_epi64(_mm256_packus_epi16(lo, hi), 0xd8);
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(at), packed);
        }
        for (; x < wide4; x += 4) {
            std::uint32_t coverage;
            std::memcpy(&coverage, cover + x, 4);
            if (coverage)
                blendFour(row + x * 4, coverage, colour4, opacity8);
        }
    }
    if (wide4 < width)
        blendPlain(dst, dstStride, src, srcStride, width, height, color, wide4);
}

bool cpuHasAvx2()
{
#if defined(_MSC_VER) && !defined(__clang__)
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7)
        return false;
    __cpuid(info, 1);
    const bool osxsave = (info[2] & (1 << 27)) != 0, avx = (info[2] & (1 << 28)) != 0;
    if (!osxsave || !avx || (_xgetbv(0) & 6) != 6) // the OS saves the YMM registers
        return false;
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
#else
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2"); // checks the OS's YMM state too
#endif
}

#endif // HIKARI_BLEND_X86

BlendPath detect()
{
#ifdef HIKARI_BLEND_X86
    return cpuHasAvx2() ? BlendPath::Avx2 : BlendPath::Sse2;
#else
    return BlendPath::Plain;
#endif
}

} // namespace

BlendPath blendPath()
{
    static const BlendPath path = detect();
    return path;
}

bool blendPathAvailable(BlendPath path)
{
    switch (path) {
    case BlendPath::Plain: return true;
    case BlendPath::Sse2: return blendPath() != BlendPath::Plain;
    case BlendPath::Avx2: return blendPath() == BlendPath::Avx2;
    }
    return false;
}

void blendAssBitmap(BlendPath path, std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src,
                    std::ptrdiff_t srcStride, int width, int height, std::uint32_t color)
{
    if (width <= 0 || height <= 0 || (color & 0xff) == 0xff)
        return;
#ifdef HIKARI_BLEND_X86
    if (path == BlendPath::Avx2)
        return blendAvx2(dst, dstStride, src, srcStride, width, height, color);
    if (path == BlendPath::Sse2)
        return blendSse2(dst, dstStride, src, srcStride, width, height, color);
#endif
    blendPlain(dst, dstStride, src, srcStride, width, height, color);
}

void blendAssBitmap(std::uint8_t *dst, std::ptrdiff_t dstStride, const std::uint8_t *src, std::ptrdiff_t srcStride,
                    int width, int height, std::uint32_t color)
{
    blendAssBitmap(blendPath(), dst, dstStride, src, srcStride, width, height, color);
}

PixelRect unite(const PixelRect &a, const PixelRect &b)
{
    if (a.width <= 0 || a.height <= 0)
        return b;
    if (b.width <= 0 || b.height <= 0)
        return a;
    const int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width), y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

void DrawnBoxes::add(const PixelRect &box)
{
    if (box.width <= 0 || box.height <= 0)
        return;
    const auto area = [](const PixelRect &r) { return static_cast<long long>(r.width) * r.height; };
    // libass lists an event's images together, so only the recent boxes overlap.
    constexpr std::size_t kRecent = 4;
    const std::size_t from = m_boxes.size() > kRecent ? m_boxes.size() - kRecent : 0;
    for (std::size_t i = m_boxes.size(); i-- > from;) {
        const PixelRect merged = unite(m_boxes[i], box);
        if (area(merged) <= area(m_boxes[i]) + area(box)) {
            m_boxes[i] = merged;
            return;
        }
    }
    m_boxes.push_back(box);
}

void DrawnBoxes::zero(std::uint8_t *pixels, std::ptrdiff_t stride) const
{
    for (const PixelRect &box : m_boxes)
        for (int y = box.y; y < box.y + box.height; ++y)
            std::memset(pixels + y * stride + box.x * 4, 0, static_cast<std::size_t>(box.width) * 4);
}

bool OverlayBuffers::setSize(int width, int height)
{
    if (width == m_width && height == m_height)
        return false;
    m_buffers.clear();
    m_width = width;
    m_height = height;
    return true;
}

OverlayBuffers::Buffer &OverlayBuffers::acquire()
{
    constexpr std::size_t kKept = 4;
    const auto it = std::ranges::find_if(m_buffers, [](const Buffer &b) { return b.bytes.use_count() == 1; });
    if (it != m_buffers.end()) {
        std::atomic_thread_fence(std::memory_order_acquire); // use_count() is a relaxed load
        std::rotate(it, it + 1, m_buffers.end());
        Buffer &b = m_buffers.back();
        b.drawn.zero(b.bytes->data(), stride());
        b.drawn.clear();
        return b;
    }
    if (m_buffers.size() >= kKept)
        m_buffers.erase(m_buffers.begin());
    const std::size_t bytes = static_cast<std::size_t>(stride()) * static_cast<std::size_t>(m_height);
    m_buffers.push_back(Buffer{std::make_shared<application::OverlayPixels::Bytes>(bytes, std::uint8_t{0}), {}});
    return m_buffers.back();
}

PixelRect premultiplyStraightAlpha(std::uint8_t *pixels, int width, int height, std::ptrdiff_t stride)
{
    int x0 = width, y0 = height, x1 = 0, y1 = 0;
    for (int y = 0; y < height; ++y) {
        std::uint8_t *row = pixels + y * stride;
        int rowX0 = width, rowX1 = 0;
        int x = 0;
        for (; x + 2 <= width; x += 2) {
            std::uint64_t word;
            std::memcpy(&word, row + x * 4, 8);
            if (!word)
                continue;
            for (int k = x; k < x + 2; ++k) {
                std::uint8_t *p = row + k * 4;
                const unsigned a = p[3];
                if (a == 0) {
                    p[0] = p[1] = p[2] = 0;
                    continue;
                }
                rowX0 = std::min(rowX0, k);
                rowX1 = k + 1;
                if (a != 255)
                    for (int c = 0; c < 3; ++c)
                        p[c] = static_cast<std::uint8_t>((p[c] * a + 127) / 255);
            }
        }
        if (x < width) {
            std::uint8_t *p = row + x * 4;
            const unsigned a = p[3];
            if (a == 0) {
                p[0] = p[1] = p[2] = 0;
            } else {
                rowX0 = std::min(rowX0, x);
                rowX1 = x + 1;
                if (a != 255)
                    for (int c = 0; c < 3; ++c)
                        p[c] = static_cast<std::uint8_t>((p[c] * a + 127) / 255);
            }
        }
        if (rowX1 > rowX0) {
            x0 = std::min(x0, rowX0);
            x1 = std::max(x1, rowX1);
            y0 = std::min(y0, y);
            y1 = y + 1;
        }
    }
    if (x1 <= x0)
        return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace hikari::backends
