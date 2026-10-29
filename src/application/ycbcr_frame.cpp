#include "hikari/application/ycbcr_frame.h"

#include <algorithm>
#include <vector>

namespace hikari::application {

namespace {

int aligned(int bytes)
{
    return (bytes + 3) & ~3;
}

}

YcbcrPlanes ycbcrLayout(int width, int height, bool interleaved, bool fullHeightChroma, const yuv::Matrix &matrix)
{
    YcbcrPlanes p;
    p.interleaved = interleaved;
    p.fullHeightChroma = fullHeightChroma;
    p.chromaWidth = (width + 1) / 2;
    p.chromaHeight = fullHeightChroma ? height : (height + 1) / 2;
    p.matrix = matrix;
    p.stride[0] = aligned(width);
    p.stride[1] = aligned(interleaved ? p.chromaWidth * 2 : p.chromaWidth);
    p.stride[2] = interleaved ? 0 : p.stride[1];
    p.offset[0] = 0;
    p.offset[1] = static_cast<std::size_t>(p.stride[0]) * static_cast<std::size_t>(height);
    p.offset[2] = p.offset[1] + static_cast<std::size_t>(p.stride[1]) * static_cast<std::size_t>(p.chromaHeight);
    return p;
}

std::size_t ycbcrBytes(const YcbcrPlanes &p)
{
    return p.offset[2] + static_cast<std::size_t>(p.stride[2]) * static_cast<std::size_t>(p.chromaHeight);
}

std::string ycbcrLayoutError(const IndexedFrame &f)
{
    if (!f.ycbcr)
        return "not a Y'CbCr frame";
    const YcbcrPlanes &p = *f.ycbcr;
    if (f.width <= 0 || f.height <= 0)
        return "empty frame";
    if (p.chromaWidth != (f.width + 1) / 2 || p.chromaHeight != (p.fullHeightChroma ? f.height : (f.height + 1) / 2))
        return "chroma size does not match the frame";
    const int rowBytes[3] = {f.width, p.interleaved ? p.chromaWidth * 2 : p.chromaWidth, p.chromaWidth};
    const int rows[3] = {f.height, p.chromaHeight, p.chromaHeight};
    for (int i = 0; i < (p.interleaved ? 2 : 3); ++i) {
        if (p.stride[i] < rowBytes[i] || p.stride[i] % 4 != 0)
            return "plane stride does not fit its rows";
        const std::size_t end = p.offset[i] + static_cast<std::size_t>(p.stride[i]) * static_cast<std::size_t>(rows[i]);
        if (end > f.bgra.size())
            return "planes do not fit the buffer";
    }
    if (p.matrix.shift <= 0 || p.matrix.shift > 15)
        return "invalid matrix";
    return {};
}

bool ycbcrToBgra(const IndexedFrame &f, IndexedFrame &out)
{
    if (!ycbcrLayoutError(f).empty())
        return false;
    const YcbcrPlanes &p = *f.ycbcr;
    const auto *base = reinterpret_cast<const std::uint8_t *>(f.bgra.data());
    out.generation = f.generation;
    out.index = f.index;
    out.pts = f.pts;
    out.width = f.width;
    out.height = f.height;
    out.stride = f.width * 4;
    out.ycbcr.reset();
    out.bgra.resize(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(f.height));
    // MUST KILL: +8 padding because yuv::row reads 8 chroma samples at a time
    std::vector<std::uint8_t> u(static_cast<std::size_t>(p.chromaWidth) + 8), v(u.size());
    int chromaRow = -1;
    for (int y = 0; y < f.height; ++y) {
        const int cy = p.fullHeightChroma ? y : y / 2;
        if (cy != chromaRow) {
            chromaRow = cy;
            const std::uint8_t *c1 = base + p.offset[1] + static_cast<std::size_t>(cy) * p.stride[1];
            if (p.interleaved) {
                for (int x = 0; x < p.chromaWidth; ++x) {
                    u[static_cast<std::size_t>(x)] = c1[2 * x];
                    v[static_cast<std::size_t>(x)] = c1[2 * x + 1];
                }
            } else {
                const std::uint8_t *c2 = base + p.offset[2] + static_cast<std::size_t>(cy) * p.stride[2];
                std::copy_n(c1, p.chromaWidth, u.begin());
                std::copy_n(c2, p.chromaWidth, v.begin());
            }
        }
        yuv::row(p.matrix, base + p.offset[0] + static_cast<std::size_t>(y) * p.stride[0], u.data(), v.data(),
                 f.width, reinterpret_cast<std::uint8_t *>(out.bgra.data()) + static_cast<std::size_t>(y) * out.stride);
    }
    return true;
}

}
