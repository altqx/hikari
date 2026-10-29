#include "directshow_frames.h"

#include "hikari/application/ycbcr_frame.h"
#include "hikari/application/yuv_bgra.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace hikari::backends::dshow {

namespace {

// Limited-range Y'CbCr to full-range R'G'B' (8-bit fixed point, x256).
constexpr application::yuv::Matrix kBt601{298, 409, -100, -208, 516, 16, 8};
constexpr application::yuv::Matrix kBt709{298, 459, -55, -136, 541, 16, 8};

}

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
    const application::yuv::Matrix &k = bt709 ? kBt709 : kBt601;
    const int w = f.width, h = f.height;
    out.width = w;
    out.height = h;
    out.stride = w * 4;
    out.ycbcr.reset();
    out.bgra.resize(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(h));
    auto *dst = reinterpret_cast<std::uint8_t *>(out.bgra.data());
    const int cw = (w + 1) / 2;
    thread_local std::vector<std::uint8_t> yRow, uRow, vRow;
    yRow.resize(static_cast<std::size_t>(w));
    uRow.resize(static_cast<std::size_t>(cw) + 8);
    vRow.resize(static_cast<std::size_t>(cw) + 8);
    switch (f.subtype) {
    case Subtype::RGB32:
        for (int y = 0; y < h; ++y) {
            const std::uint8_t *src = data + static_cast<std::size_t>(f.bottomUp ? h - 1 - y : y) * w * 4;
            std::uint8_t *row = dst + static_cast<std::size_t>(y) * w * 4;
            std::memcpy(row, src, static_cast<std::size_t>(w) * 4);
            for (int x = 0; x < w; ++x)
                row[x * 4 + 3] = 255;
        }
        return true;
    case Subtype::YUY2:
        // Y0 U Y1 V: each pair of pixels shares its U and V.
        for (int y = 0; y < h; ++y) {
            const std::uint8_t *src = data + static_cast<std::size_t>(y) * w * 2;
            for (int x = 0; x < w; ++x)
                yRow[static_cast<std::size_t>(x)] = src[x * 2];
            for (int x = 0; x < w / 2; ++x) {
                uRow[static_cast<std::size_t>(x)] = src[x * 4 + 1];
                vRow[static_cast<std::size_t>(x)] = src[x * 4 + 3];
            }
            application::yuv::row(k, yRow.data(), uRow.data(), vRow.data(), w - w % 2,
                                  dst + static_cast<std::size_t>(y) * w * 4);
            if (w % 2) // a last pixel without a pair stays empty
                std::memset(dst + static_cast<std::size_t>(y) * w * 4 + (w - 1) * 4, 0, 4);
        }
        return true;
    case Subtype::YV12:
    case Subtype::NV12: {
        const std::uint8_t *luma = data;
        const std::uint8_t *chroma = data + static_cast<std::size_t>(w) * h;
        const int half = w / 2;
        int chromaRow = -1;
        for (int y = 0; y < h; ++y) {
            const int cy = std::min(y / 2, std::max(0, h / 2 - 1));
            if (cy != chromaRow) {
                chromaRow = cy;
                if (f.subtype == Subtype::NV12) {
                    const std::uint8_t *uv = chroma + static_cast<std::size_t>(cy) * w;
                    for (int x = 0; x < half; ++x) {
                        uRow[static_cast<std::size_t>(x)] = uv[x * 2];
                        vRow[static_cast<std::size_t>(x)] = uv[x * 2 + 1];
                    }
                } else {
                    const std::uint8_t *vPlane = chroma;
                    const std::uint8_t *uPlane = chroma + static_cast<std::size_t>(half) * (h / 2);
                    std::memcpy(vRow.data(), vPlane + static_cast<std::size_t>(cy) * half, static_cast<std::size_t>(half));
                    std::memcpy(uRow.data(), uPlane + static_cast<std::size_t>(cy) * half, static_cast<std::size_t>(half));
                }
                if (half < cw) {
                    uRow[static_cast<std::size_t>(half)] = half > 0 ? uRow[static_cast<std::size_t>(half - 1)] : 128;
                    vRow[static_cast<std::size_t>(half)] = half > 0 ? vRow[static_cast<std::size_t>(half - 1)] : 128;
                }
            }
            application::yuv::row(k, luma + static_cast<std::size_t>(y) * w, uRow.data(), vRow.data(), w,
                                  dst + static_cast<std::size_t>(y) * w * 4);
        }
        return true;
    }
    }
    return false;
}

bool toYcbcr(const SampleFormat &f, const std::uint8_t *data, std::size_t size, bool bt709,
             application::IndexedFrame &out)
{
    if (f.subtype != Subtype::YV12 && f.subtype != Subtype::NV12)
        return toBgra(f, data, size, bt709, out);
    if (f.width <= 0 || f.height <= 0 || !data || size < sampleBytes(f))
        return false;
    const int w = f.width, h = f.height;
    const bool nv12 = f.subtype == Subtype::NV12;
    const application::YcbcrPlanes p = application::ycbcrLayout(w, h, nv12, false, bt709 ? kBt709 : kBt601);
    out.width = w;
    out.height = h;
    out.stride = 0;
    out.bgra.resize(application::ycbcrBytes(p)); // a reused buffer of the same size is not cleared
    auto *base = reinterpret_cast<std::uint8_t *>(out.bgra.data());
    for (int y = 0; y < h; ++y)
        std::memcpy(base + p.offset[0] + static_cast<std::size_t>(y) * p.stride[0], data + static_cast<std::size_t>(y) * w,
                    static_cast<std::size_t>(w));
    const std::uint8_t *chroma = data + static_cast<std::size_t>(w) * h;
    const int half = w / 2;
    for (int r = 0; r < p.chromaHeight; ++r) {
        const int cy = std::min(r, std::max(0, h / 2 - 1));
        std::uint8_t *first = base + p.offset[1] + static_cast<std::size_t>(r) * p.stride[1];
        if (nv12) {
            std::memcpy(first, chroma + static_cast<std::size_t>(cy) * w, static_cast<std::size_t>(half) * 2);
            if (half < p.chromaWidth) {
                first[half * 2] = half > 0 ? first[half * 2 - 2] : 128;
                first[half * 2 + 1] = half > 0 ? first[half * 2 - 1] : 128;
            }
        } else {
            std::uint8_t *second = base + p.offset[2] + static_cast<std::size_t>(r) * p.stride[2];
            const std::uint8_t *vPlane = chroma;
            const std::uint8_t *uPlane = chroma + static_cast<std::size_t>(half) * (h / 2);
            std::memcpy(first, uPlane + static_cast<std::size_t>(cy) * half, static_cast<std::size_t>(half));
            std::memcpy(second, vPlane + static_cast<std::size_t>(cy) * half, static_cast<std::size_t>(half));
            if (half < p.chromaWidth) {
                first[half] = half > 0 ? first[half - 1] : 128;
                second[half] = half > 0 ? second[half - 1] : 128;
            }
        }
    }
    out.ycbcr = p;
    return true;
}

}
