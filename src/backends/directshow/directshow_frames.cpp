#include "directshow_frames.h"

#include <algorithm>
#include <cstring>

namespace hikari::backends::dshow {

namespace {

std::uint8_t clamp8(int v)
{
    return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

// Limited-range Y'CbCr to full-range R'G'B' (8-bit fixed point, x256).
struct Coefficients {
    int rv, gu, gv, bu;
};
constexpr Coefficients kBt601{409, -100, -208, 516};
constexpr Coefficients kBt709{459, -55, -136, 541};

void pixel(const Coefficients &k, int y, int u, int v, std::uint8_t *out)
{
    const int c = 298 * (y - 16);
    const int d = u - 128, e = v - 128;
    out[0] = clamp8((c + k.bu * d + 128) >> 8);
    out[1] = clamp8((c + k.gu * d + k.gv * e + 128) >> 8);
    out[2] = clamp8((c + k.rv * e + 128) >> 8);
    out[3] = 255;
}

} // namespace

std::size_t sampleBytes(const SampleFormat &f)
{
    const std::size_t pixels = static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height);
    switch (f.subtype) {
    case Subtype::RGB32: return pixels * 4;
    case Subtype::YUY2: return pixels * 2;
    case Subtype::YV12:
    case Subtype::NV12: return pixels + pixels / 2;
    }
    return 0;
}

const char *subtypeName(Subtype subtype)
{
    switch (subtype) {
    case Subtype::RGB32: return "RGB32";
    case Subtype::YUY2: return "YUY2";
    case Subtype::YV12: return "YV12";
    case Subtype::NV12: return "NV12";
    }
    return "";
}

bool toBgra(const SampleFormat &f, const std::uint8_t *data, std::size_t size, bool bt709,
            application::IndexedFrame &out)
{
    if (f.width <= 0 || f.height <= 0 || !data || size < sampleBytes(f))
        return false;
    const Coefficients &k = bt709 ? kBt709 : kBt601;
    const int w = f.width, h = f.height;
    out.width = w;
    out.height = h;
    out.stride = w * 4;
    out.bgra.resize(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(h));
    auto *dst = reinterpret_cast<std::uint8_t *>(out.bgra.data());
    switch (f.subtype) {
    case Subtype::RGB32:
        // Legacy m_SwapFrame: an RGB32 sample is a bottom-up DIB.
        for (int y = 0; y < h; ++y) {
            const std::uint8_t *src = data + static_cast<std::size_t>(f.bottomUp ? h - 1 - y : y) * w * 4;
            std::uint8_t *row = dst + static_cast<std::size_t>(y) * w * 4;
            std::memcpy(row, src, static_cast<std::size_t>(w) * 4);
            for (int x = 0; x < w; ++x)
                row[x * 4 + 3] = 255;
        }
        return true;
    case Subtype::YUY2:
        for (int y = 0; y < h; ++y) {
            const std::uint8_t *src = data + static_cast<std::size_t>(y) * w * 2;
            std::uint8_t *row = dst + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x + 1 < w; x += 2) {
                const int u = src[x * 2 + 1], v = src[x * 2 + 3];
                pixel(k, src[x * 2], u, v, row + x * 4);
                pixel(k, src[x * 2 + 2], u, v, row + (x + 1) * 4);
            }
        }
        return true;
    case Subtype::YV12:
    case Subtype::NV12: {
        const std::uint8_t *luma = data;
        const std::uint8_t *chroma = data + static_cast<std::size_t>(w) * h;
        const int cw = w / 2;
        for (int y = 0; y < h; ++y) {
            const std::uint8_t *ly = luma + static_cast<std::size_t>(y) * w;
            std::uint8_t *row = dst + static_cast<std::size_t>(y) * w * 4;
            const int cy = std::min(y / 2, std::max(0, h / 2 - 1));
            for (int x = 0; x < w; ++x) {
                int u = 128, v = 128;
                if (f.subtype == Subtype::NV12) {
                    const std::uint8_t *uv = chroma + static_cast<std::size_t>(cy) * w + (x / 2) * 2;
                    u = uv[0];
                    v = uv[1];
                } else {
                    const std::uint8_t *vPlane = chroma;
                    const std::uint8_t *uPlane = chroma + static_cast<std::size_t>(cw) * (h / 2);
                    v = vPlane[static_cast<std::size_t>(cy) * cw + x / 2];
                    u = uPlane[static_cast<std::size_t>(cy) * cw + x / 2];
                }
                pixel(k, ly[x], u, v, row + x * 4);
            }
        }
        return true;
    }
    }
    return false;
}

} // namespace hikari::backends::dshow
