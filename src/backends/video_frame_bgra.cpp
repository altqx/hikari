#include "hikari/backends/video_frame_bgra.h"

#include "hikari/application/ycbcr_frame.h"
#include "hikari/application/yuv_bgra.h"

#include <QImage>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include <algorithm>
#include <cstring>
#include <vector>

namespace hikari::backends {

namespace yuv = application::yuv;

namespace {

enum class Layout { Planar, SemiPlanar };

struct Shape {
    Layout layout = Layout::Planar;
    bool wide = false;      // 16-bit samples
    bool highBits = false;  // 16-bit samples hold their value in the high bits (P010/P016)
    bool swapChroma = false; // V before U (YV12's planes, NV21's pairs)
    bool fullHeightChroma = false; // 4:2:2
};

bool shapeOf(QVideoFrameFormat::PixelFormat format, Shape &s)
{
    switch (format) {
    case QVideoFrameFormat::Format_NV12: s = {Layout::SemiPlanar}; return true;
    case QVideoFrameFormat::Format_NV21: s = {Layout::SemiPlanar, false, false, true}; return true;
    case QVideoFrameFormat::Format_P010:
    case QVideoFrameFormat::Format_P016: s = {Layout::SemiPlanar, true, true}; return true;
    case QVideoFrameFormat::Format_YUV420P: s = {Layout::Planar}; return true;
    case QVideoFrameFormat::Format_YV12: s = {Layout::Planar, false, false, true}; return true;
    case QVideoFrameFormat::Format_YUV420P10: s = {Layout::Planar, true}; return true;
    case QVideoFrameFormat::Format_YUV422P: s = {Layout::Planar, false, false, false, true}; return true;
    default: return false;
    }
}

yuv::Matrix matrixOf(const QVideoFrameFormat &format, int width, int height,
                     const std::optional<application::LegacyColourMatrix::Input> &colour)
{
    namespace ffms = application::ffms_colour;
    if (colour) {
        // FFMS2's (libavutil's) matrix constants; BT.2020's are 9 and 10.
        std::optional<yuv::Space> space;
        switch (colour->colorSpace) {
        case ffms::kBt709: space = yuv::Space::Bt709; break;
        case ffms::kBt470bg:
        case ffms::kSmpte170m: space = yuv::Space::Bt601; break;
        case ffms::kFcc: space = yuv::Space::Fcc; break;
        case ffms::kSmpte240m: space = yuv::Space::Smpte240m; break;
        case 9:
        case 10: space = yuv::Space::Bt2020; break;
        default: break;
        }
        if (space)
            return yuv::matrix(*space, colour->colorRange == ffms::kRangeJpeg);
    }
    yuv::Space space = width > 1024 || height >= 600 ? yuv::Space::Bt709 : yuv::Space::Bt601;
    switch (format.colorSpace()) {
    case QVideoFrameFormat::ColorSpace_BT601: space = yuv::Space::Bt601; break;
    case QVideoFrameFormat::ColorSpace_BT709: space = yuv::Space::Bt709; break;
    case QVideoFrameFormat::ColorSpace_BT2020: space = yuv::Space::Bt2020; break;
    default: break;
    }
    const bool full = colour ? colour->colorRange == ffms::kRangeJpeg
                             : format.colorRange() == QVideoFrameFormat::ColorRange_Full;
    return yuv::matrix(space, full);
}

std::uint8_t narrow(std::uint16_t v, bool highBits)
{
    const unsigned n = highBits ? (v + 128u) >> 8 : (v + 2u) >> 2; // 16-bit or 10-bit (low bits) to 8
    return static_cast<std::uint8_t>(n > 255 ? 255 : n);
}

const std::uint16_t *words(const uchar *p)
{
    return reinterpret_cast<const std::uint16_t *>(p);
}

bool viaImage(const QVideoFrame &frame, application::IndexedFrame &out)
{
    QImage image = frame.toImage();
    if (image.isNull())
        return false;
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_RGB32);
    out.width = image.width();
    out.height = image.height();
    out.stride = out.width * 4;
    out.ycbcr.reset();
    out.bgra.resize(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(out.height));
    for (int y = 0; y < out.height; ++y) {
        auto *row = reinterpret_cast<std::uint8_t *>(out.bgra.data()) + static_cast<std::size_t>(y) * out.stride;
        std::memcpy(row, image.constScanLine(y), static_cast<std::size_t>(out.stride));
        for (int x = 3; x < out.stride; x += 4)
            row[x] = 255;
    }
    return true;
}

} // namespace

bool videoFrameToBgra(const QVideoFrame &source, application::IndexedFrame &out,
                      std::optional<application::LegacyColourMatrix::Input> colour)
{
    if (!source.isValid())
        return false;
    Shape shape;
    if (!shapeOf(source.pixelFormat(), shape))
        return viaImage(source, out);
    QVideoFrame frame(source);
    if (!frame.map(QVideoFrame::ReadOnly))
        return false;
    const int w = frame.width(), h = frame.height();
    const int planes = frame.planeCount();
    if (w <= 0 || h <= 0 || planes < (shape.layout == Layout::Planar ? 3 : 2)) {
        frame.unmap();
        return false;
    }
    const yuv::Matrix m = matrixOf(frame.surfaceFormat(), w, h, colour);
    out.width = w;
    out.height = h;
    out.stride = w * 4;
    out.ycbcr.reset();
    out.bgra.resize(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(h));
    const int cw = (w + 1) / 2;
    // An odd height may map one chroma row fewer; the last one serves the rows below it.
    auto rows = [&frame](int plane) {
        const int line = frame.bytesPerLine(plane);
        return line > 0 ? static_cast<int>(frame.mappedBytes(plane) / line) : 0;
    };
    int ch = std::min(shape.fullHeightChroma ? h : (h + 1) / 2, rows(1));
    if (shape.layout == Layout::Planar)
        ch = std::min(ch, rows(2));
    if (ch <= 0) {
        frame.unmap();
        return false;
    }
    thread_local std::vector<std::uint8_t> yRow, uRow, vRow;
    yRow.resize(static_cast<std::size_t>(w));
    // MUST KILL: +8 padding because yuv::row reads chroma 8 samples at a time
    uRow.resize(static_cast<std::size_t>(cw) + 8);
    vRow.resize(static_cast<std::size_t>(cw) + 8);
    int chromaRow = -1;
    const std::uint8_t *u = nullptr, *v = nullptr;
    for (int y = 0; y < h; ++y) {
        const int cy = std::min(shape.fullHeightChroma ? y : y / 2, ch - 1);
        const uchar *luma = frame.bits(0) + static_cast<std::ptrdiff_t>(y) * frame.bytesPerLine(0);
        const std::uint8_t *Y = luma;
        if (shape.wide) {
            const std::uint16_t *s = words(luma);
            for (int x = 0; x < w; ++x)
                yRow[static_cast<std::size_t>(x)] = narrow(s[x], shape.highBits);
            Y = yRow.data();
        }
        if (cy != chromaRow) {
            chromaRow = cy;
            if (shape.layout == Layout::SemiPlanar) {
                const uchar *pairs = frame.bits(1) + static_cast<std::ptrdiff_t>(cy) * frame.bytesPerLine(1);
                for (int x = 0; x < cw; ++x) {
                    std::uint8_t a, b;
                    if (shape.wide) {
                        a = narrow(words(pairs)[2 * x], shape.highBits);
                        b = narrow(words(pairs)[2 * x + 1], shape.highBits);
                    } else {
                        a = pairs[2 * x];
                        b = pairs[2 * x + 1];
                    }
                    uRow[static_cast<std::size_t>(x)] = shape.swapChroma ? b : a;
                    vRow[static_cast<std::size_t>(x)] = shape.swapChroma ? a : b;
                }
                u = uRow.data();
                v = vRow.data();
            } else {
                const uchar *first = frame.bits(1) + static_cast<std::ptrdiff_t>(cy) * frame.bytesPerLine(1);
                const uchar *second = frame.bits(2) + static_cast<std::ptrdiff_t>(cy) * frame.bytesPerLine(2);
                const uchar *pu = shape.swapChroma ? second : first;
                const uchar *pv = shape.swapChroma ? first : second;
                if (shape.wide) {
                    for (int x = 0; x < cw; ++x) {
                        uRow[static_cast<std::size_t>(x)] = narrow(words(pu)[x], shape.highBits);
                        vRow[static_cast<std::size_t>(x)] = narrow(words(pv)[x], shape.highBits);
                    }
                } else {
                    // MUST KILL: copied only because yuv::row reads 8 samples at a time
                    std::memcpy(uRow.data(), pu, static_cast<std::size_t>(cw));
                    std::memcpy(vRow.data(), pv, static_cast<std::size_t>(cw));
                }
                u = uRow.data();
                v = vRow.data();
            }
        }
        yuv::row(m, Y, u, v, w,
                 reinterpret_cast<std::uint8_t *>(out.bgra.data()) + static_cast<std::size_t>(y) * out.stride);
    }
    frame.unmap();
    return true;
}

bool videoFrameToYcbcr(const QVideoFrame &source, application::IndexedFrame &out,
                       std::optional<application::LegacyColourMatrix::Input> colour)
{
    if (!source.isValid())
        return false;
    Shape shape;
    if (!shapeOf(source.pixelFormat(), shape))
        return viaImage(source, out);
    QVideoFrame frame(source);
    if (!frame.map(QVideoFrame::ReadOnly))
        return false;
    const int w = frame.width(), h = frame.height();
    const bool interleaved = shape.layout == Layout::SemiPlanar;
    if (w <= 0 || h <= 0 || frame.planeCount() < (interleaved ? 2 : 3)) {
        frame.unmap();
        return false;
    }
    auto rows = [&frame](int plane) {
        const int line = frame.bytesPerLine(plane);
        return line > 0 ? static_cast<int>(frame.mappedBytes(plane) / line) : 0;
    };
    int ch = std::min(shape.fullHeightChroma ? h : (h + 1) / 2, rows(1));
    if (!interleaved)
        ch = std::min(ch, rows(2));
    if (ch <= 0) {
        frame.unmap();
        return false;
    }
    const application::YcbcrPlanes p =
        application::ycbcrLayout(w, h, interleaved, shape.fullHeightChroma, matrixOf(frame.surfaceFormat(), w, h, colour));
    out.width = w;
    out.height = h;
    out.stride = 0;
    out.bgra.resize(application::ycbcrBytes(p));
    auto *base = reinterpret_cast<std::uint8_t *>(out.bgra.data());
    for (int y = 0; y < h; ++y) {
        const uchar *luma = frame.bits(0) + static_cast<std::ptrdiff_t>(y) * frame.bytesPerLine(0);
        std::uint8_t *dst = base + p.offset[0] + static_cast<std::size_t>(y) * p.stride[0];
        if (shape.wide)
            for (int x = 0; x < w; ++x)
                dst[x] = narrow(words(luma)[x], shape.highBits);
        else
            std::memcpy(dst, luma, static_cast<std::size_t>(w));
    }
    const int cw = p.chromaWidth;
    for (int r = 0; r < p.chromaHeight; ++r) {
        const int cy = std::min(r, ch - 1);
        std::uint8_t *first = base + p.offset[1] + static_cast<std::size_t>(r) * p.stride[1];
        if (interleaved) {
            const uchar *pairs = frame.bits(1) + static_cast<std::ptrdiff_t>(cy) * frame.bytesPerLine(1);
            if (!shape.wide && !shape.swapChroma) {
                std::memcpy(first, pairs, static_cast<std::size_t>(cw) * 2);
                continue;
            }
            for (int x = 0; x < cw; ++x) {
                std::uint8_t a, b;
                if (shape.wide) {
                    a = narrow(words(pairs)[2 * x], shape.highBits);
                    b = narrow(words(pairs)[2 * x + 1], shape.highBits);
                } else {
                    a = pairs[2 * x];
                    b = pairs[2 * x + 1];
                }
                first[2 * x] = shape.swapChroma ? b : a;
                first[2 * x + 1] = shape.swapChroma ? a : b;
            }
        } else {
            std::uint8_t *second = base + p.offset[2] + static_cast<std::size_t>(r) * p.stride[2];
            const uchar *a = frame.bits(1) + static_cast<std::ptrdiff_t>(cy) * frame.bytesPerLine(1);
            const uchar *b = frame.bits(2) + static_cast<std::ptrdiff_t>(cy) * frame.bytesPerLine(2);
            const uchar *pu = shape.swapChroma ? b : a;
            const uchar *pv = shape.swapChroma ? a : b;
            if (shape.wide) {
                for (int x = 0; x < cw; ++x) {
                    first[x] = narrow(words(pu)[x], shape.highBits);
                    second[x] = narrow(words(pv)[x], shape.highBits);
                }
            } else {
                std::memcpy(first, pu, static_cast<std::size_t>(cw));
                std::memcpy(second, pv, static_cast<std::size_t>(cw));
            }
        }
    }
    frame.unmap();
    out.ycbcr = p;
    return true;
}

} // namespace hikari::backends
