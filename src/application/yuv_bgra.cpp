#include "hikari/application/yuv_bgra.h"

#include <cmath>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define HIKARI_YUV_SSE2 1
#include <emmintrin.h>
#endif

namespace hikari::application::yuv {

Matrix matrix(Space space, bool fullRange)
{
    double kr = 0.2126, kb = 0.0722; // BT.709
    if (space == Space::Bt601) {
        kr = 0.299;
        kb = 0.114;
    } else if (space == Space::Bt2020) {
        kr = 0.2627;
        kb = 0.0593;
    } else if (space == Space::Fcc) {
        kr = 0.30;
        kb = 0.11;
    } else if (space == Space::Smpte240m) {
        kr = 0.212;
        kb = 0.087;
    }
    const double kg = 1.0 - kr - kb;
    const double luma = fullRange ? 1.0 : 255.0 / 219.0;
    const double chroma = fullRange ? 1.0 : 255.0 / 224.0;
    constexpr double one = 1 << 13;
    auto fixed = [](double v) { return static_cast<int>(std::lround(v * one)); };
    Matrix m;
    m.shift = 13;
    m.yOffset = fullRange ? 0 : 16;
    m.cy = fixed(luma);
    m.rv = fixed(2.0 * (1.0 - kr) * chroma);
    m.bu = fixed(2.0 * (1.0 - kb) * chroma);
    m.gu = fixed(-2.0 * kb * (1.0 - kb) / kg * chroma);
    m.gv = fixed(-2.0 * kr * (1.0 - kr) / kg * chroma);
    return m;
}

namespace {

std::uint8_t clamp8(int v)
{
    return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

void pixel(const Matrix &m, int y, int u, int v, std::uint8_t *out)
{
    const int round = 1 << (m.shift - 1);
    const int c = m.cy * (y - m.yOffset);
    const int d = u - 128, e = v - 128;
    out[0] = clamp8((c + m.bu * d + round) >> m.shift);
    out[1] = clamp8((c + m.gu * d + m.gv * e + round) >> m.shift);
    out[2] = clamp8((c + m.rv * e + round) >> m.shift);
    out[3] = 255;
}

#ifdef HIKARI_YUV_SSE2
// Two coefficients repeated over the four 32-bit lanes (for _mm_madd_epi16).
__m128i pair(int a, int b)
{
    return _mm_set1_epi32(static_cast<int>((static_cast<std::uint32_t>(b) << 16) |
                                           (static_cast<std::uint32_t>(a) & 0xffffu)));
}

struct Coefficients {
    __m128i yv, yu, yuG, v1, round, shift;
};

__m128i channel(__m128i lo, __m128i hi, const __m128i &shift)
{
    return _mm_packs_epi32(_mm_sra_epi32(lo, shift), _mm_sra_epi32(hi, shift));
}

// Eight pixels' channel values, 16-bit and saturated.
void eight(const Coefficients &k, __m128i y, __m128i u, __m128i v, __m128i &b, __m128i &g, __m128i &r)
{
    const __m128i one = _mm_set1_epi16(1);
    const __m128i yvLo = _mm_unpacklo_epi16(y, v), yvHi = _mm_unpackhi_epi16(y, v);
    const __m128i yuLo = _mm_unpacklo_epi16(y, u), yuHi = _mm_unpackhi_epi16(y, u);
    const __m128i v1Lo = _mm_unpacklo_epi16(v, one), v1Hi = _mm_unpackhi_epi16(v, one);
    r = channel(_mm_add_epi32(_mm_madd_epi16(yvLo, k.yv), k.round), _mm_add_epi32(_mm_madd_epi16(yvHi, k.yv), k.round),
                k.shift);
    b = channel(_mm_add_epi32(_mm_madd_epi16(yuLo, k.yu), k.round), _mm_add_epi32(_mm_madd_epi16(yuHi, k.yu), k.round),
                k.shift);
    g = channel(_mm_add_epi32(_mm_madd_epi16(yuLo, k.yuG), _mm_madd_epi16(v1Lo, k.v1)),
                _mm_add_epi32(_mm_madd_epi16(yuHi, k.yuG), _mm_madd_epi16(v1Hi, k.v1)), k.shift);
}
#endif

} // namespace

void row(const Matrix &m, const std::uint8_t *y, const std::uint8_t *u, const std::uint8_t *v, int width,
         std::uint8_t *bgra)
{
    int x = 0;
#ifdef HIKARI_YUV_SSE2
    const int round = 1 << (m.shift - 1);
    Coefficients k;
    k.yv = pair(m.cy, m.rv);
    k.yu = pair(m.cy, m.bu);
    k.yuG = pair(m.cy, m.gu);
    k.v1 = pair(m.gv, round);
    k.round = _mm_set1_epi32(round);
    k.shift = _mm_cvtsi32_si128(m.shift);
    const __m128i zero = _mm_setzero_si128();
    const __m128i yOffset = _mm_set1_epi16(static_cast<short>(m.yOffset));
    const __m128i c128 = _mm_set1_epi16(128);
    const __m128i alpha = _mm_set1_epi8(static_cast<char>(-1));
    for (; x + 16 <= width; x += 16) {
        const __m128i y8 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(y + x));
        const __m128i u16 = _mm_sub_epi16(
            _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(u + x / 2)), zero), c128);
        const __m128i v16 = _mm_sub_epi16(
            _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(v + x / 2)), zero), c128);
        __m128i b0, g0, r0, b1, g1, r1;
        eight(k, _mm_sub_epi16(_mm_unpacklo_epi8(y8, zero), yOffset), _mm_unpacklo_epi16(u16, u16),
              _mm_unpacklo_epi16(v16, v16), b0, g0, r0);
        eight(k, _mm_sub_epi16(_mm_unpackhi_epi8(y8, zero), yOffset), _mm_unpackhi_epi16(u16, u16),
              _mm_unpackhi_epi16(v16, v16), b1, g1, r1);
        const __m128i b = _mm_packus_epi16(b0, b1), g = _mm_packus_epi16(g0, g1), r = _mm_packus_epi16(r0, r1);
        const __m128i bgLo = _mm_unpacklo_epi8(b, g), bgHi = _mm_unpackhi_epi8(b, g);
        const __m128i raLo = _mm_unpacklo_epi8(r, alpha), raHi = _mm_unpackhi_epi8(r, alpha);
        auto *out = reinterpret_cast<__m128i *>(bgra + static_cast<std::ptrdiff_t>(x) * 4);
        _mm_storeu_si128(out, _mm_unpacklo_epi16(bgLo, raLo));
        _mm_storeu_si128(out + 1, _mm_unpackhi_epi16(bgLo, raLo));
        _mm_storeu_si128(out + 2, _mm_unpacklo_epi16(bgHi, raHi));
        _mm_storeu_si128(out + 3, _mm_unpackhi_epi16(bgHi, raHi));
    }
#endif
    for (; x < width; ++x)
        pixel(m, y[x], u[x / 2], v[x / 2], bgra + static_cast<std::ptrdiff_t>(x) * 4);
}

} // namespace hikari::application::yuv
