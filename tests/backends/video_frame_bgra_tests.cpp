// V1: player frames to BGRA off the GUI thread. Every Y'CbCr layout the
// decoder hands over gives the same colours for the same samples (8- and
// 16-bit, planar and semi-planar, either chroma order), with the frame's
// matrix and range; other formats go through Qt's own conversion.
#include "hikari/application/ycbcr_frame.h"
#include "hikari/application/yuv_bgra.h"
#include "hikari/backends/video_frame_bgra.h"

#include <QGuiApplication>
#include <QImage>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <random>

using hikari::application::IndexedFrame;
namespace yuv = hikari::application::yuv;

namespace {

constexpr int kY = 120, kU = 60, kV = 200; // 8-bit samples every layout carries

// The expected pixel from the documented formula.
std::array<int, 4> expected(const yuv::Matrix &m, int y, int u, int v)
{
    std::uint8_t out[4];
    const std::uint8_t ys[16] = {std::uint8_t(y)}, us[16] = {std::uint8_t(u)}, vs[16] = {std::uint8_t(v)};
    yuv::row(m, ys, us, vs, 1, out);
    return {out[0], out[1], out[2], out[3]};
}

void fill16(uchar *line, int count, std::uint16_t value)
{
    auto *w = reinterpret_cast<std::uint16_t *>(line);
    for (int i = 0; i < count; ++i)
        w[i] = value;
}

// A frame of uniform samples: luma kY, chroma kU and kV, in `format`'s layout.
QVideoFrame uniform(QVideoFrameFormat::PixelFormat format, int width, int height,
                    QVideoFrameFormat::ColorSpace space = QVideoFrameFormat::ColorSpace_BT709,
                    QVideoFrameFormat::ColorRange range = QVideoFrameFormat::ColorRange_Video)
{
    QVideoFrameFormat f(QSize(width, height), format);
    f.setColorSpace(space);
    f.setColorRange(range);
    QVideoFrame frame(f);
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    const int cw = (width + 1) / 2;
    // The chroma rows Qt allocated (an odd height may have one fewer).
    int ch = format == QVideoFrameFormat::Format_YUV422P ? height : (height + 1) / 2;
    for (int plane = 1; plane < frame.planeCount(); ++plane)
        ch = std::min(ch, int(frame.mappedBytes(plane) / frame.bytesPerLine(plane)));
    for (int y = 0; y < height; ++y) {
        uchar *line = frame.bits(0) + y * frame.bytesPerLine(0);
        switch (format) {
        case QVideoFrameFormat::Format_P010: fill16(line, width, std::uint16_t(kY << 8)); break; // 10 bits, high
        case QVideoFrameFormat::Format_P016: fill16(line, width, std::uint16_t(kY << 8)); break;
        case QVideoFrameFormat::Format_YUV420P10: fill16(line, width, std::uint16_t(kY << 2)); break; // low bits
        default: std::memset(line, kY, static_cast<std::size_t>(width)); break;
        }
    }
    for (int y = 0; y < ch; ++y) {
        switch (format) {
        case QVideoFrameFormat::Format_NV12:
        case QVideoFrameFormat::Format_NV21: {
            uchar *line = frame.bits(1) + y * frame.bytesPerLine(1);
            const bool vu = format == QVideoFrameFormat::Format_NV21;
            for (int x = 0; x < cw; ++x) {
                line[2 * x] = uchar(vu ? kV : kU);
                line[2 * x + 1] = uchar(vu ? kU : kV);
            }
            break;
        }
        case QVideoFrameFormat::Format_P010:
        case QVideoFrameFormat::Format_P016: {
            auto *line = reinterpret_cast<std::uint16_t *>(frame.bits(1) + y * frame.bytesPerLine(1));
            for (int x = 0; x < cw; ++x) {
                line[2 * x] = std::uint16_t(kU << 8);
                line[2 * x + 1] = std::uint16_t(kV << 8);
            }
            break;
        }
        case QVideoFrameFormat::Format_YUV420P10:
            fill16(frame.bits(1) + y * frame.bytesPerLine(1), cw, std::uint16_t(kU << 2));
            fill16(frame.bits(2) + y * frame.bytesPerLine(2), cw, std::uint16_t(kV << 2));
            break;
        case QVideoFrameFormat::Format_YV12: // V's plane first
            std::memset(frame.bits(1) + y * frame.bytesPerLine(1), kV, static_cast<std::size_t>(cw));
            std::memset(frame.bits(2) + y * frame.bytesPerLine(2), kU, static_cast<std::size_t>(cw));
            break;
        default:
            std::memset(frame.bits(1) + y * frame.bytesPerLine(1), kU, static_cast<std::size_t>(cw));
            std::memset(frame.bits(2) + y * frame.bytesPerLine(2), kV, static_cast<std::size_t>(cw));
            break;
        }
    }
    frame.unmap();
    return frame;
}

int differing(const IndexedFrame &out, std::array<int, 4> want)
{
    int bad = 0;
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const auto *p = reinterpret_cast<const std::uint8_t *>(out.bgra.data()) + y * out.stride + x * 4;
            for (int c = 0; c < 4; ++c)
                bad += p[c] != want[static_cast<std::size_t>(c)];
        }
    return bad;
}

// A frame of `format` whose mapped bytes (every plane, padding included) are
// random: 16-bit samples of any value, including out-of-range ones.
QVideoFrame noisy(QVideoFrameFormat::PixelFormat format, int width, int height, unsigned seed)
{
    QVideoFrame frame(QVideoFrameFormat(QSize(width, height), format));
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    std::mt19937 random(seed);
    for (int plane = 0; plane < frame.planeCount(); ++plane)
        for (qsizetype i = 0; i < frame.mappedBytes(plane); ++i)
            frame.bits(plane)[i] = uchar(random());
    frame.unmap();
    return frame;
}

// "": the same bytes; else where the first one differs.
QString firstDifference(const IndexedFrame &a, const IndexedFrame &b)
{
    if (a.width != b.width || a.height != b.height)
        return QStringLiteral("size %1x%2 against %3x%4").arg(a.width).arg(a.height).arg(b.width).arg(b.height);
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width * 4; ++x) {
            const auto p = a.bgra[static_cast<std::size_t>(y) * a.stride + x];
            const auto q = b.bgra[static_cast<std::size_t>(y) * b.stride + x];
            if (p != q)
                return QStringLiteral("pixel %1,%2 channel %3: %4 against %5")
                    .arg(x / 4).arg(y).arg(x % 4).arg(int(p)).arg(int(q));
        }
    return {};
}

} // namespace

class VideoFrameBgraTests : public QObject {
    Q_OBJECT

private slots:
    void everyLayoutGivesTheSameColours_data()
    {
        QTest::addColumn<int>("format");
        QTest::newRow("NV12") << int(QVideoFrameFormat::Format_NV12);
        QTest::newRow("NV21") << int(QVideoFrameFormat::Format_NV21);
        QTest::newRow("P010") << int(QVideoFrameFormat::Format_P010);
        QTest::newRow("P016") << int(QVideoFrameFormat::Format_P016);
        QTest::newRow("YUV420P") << int(QVideoFrameFormat::Format_YUV420P);
        QTest::newRow("YV12") << int(QVideoFrameFormat::Format_YV12);
        QTest::newRow("YUV420P10") << int(QVideoFrameFormat::Format_YUV420P10);
        QTest::newRow("YUV422P") << int(QVideoFrameFormat::Format_YUV422P);
    }
    void everyLayoutGivesTheSameColours()
    {
        QFETCH(int, format);
        // 38 wide: SIMD blocks of 16 and a scalar tail; an odd height.
        const QVideoFrame frame = uniform(QVideoFrameFormat::PixelFormat(format), 38, 9);
        QVERIFY(frame.isValid());
        IndexedFrame out;
        QVERIFY(hikari::backends::videoFrameToBgra(frame, out));
        QCOMPARE(out.width, 38);
        QCOMPARE(out.height, 9);
        QCOMPARE(out.stride, 38 * 4);
        QCOMPARE(differing(out, expected(yuv::matrix(yuv::Space::Bt709, false), kY, kU, kV)), 0);
    }

    void theFramesMatrixAndRangeApply()
    {
        struct Case {
            QVideoFrameFormat::ColorSpace space;
            QVideoFrameFormat::ColorRange range;
            int height;
            yuv::Space want;
            bool full;
        };
        const Case cases[] = {
            {QVideoFrameFormat::ColorSpace_BT601, QVideoFrameFormat::ColorRange_Video, 8, yuv::Space::Bt601, false},
            {QVideoFrameFormat::ColorSpace_BT709, QVideoFrameFormat::ColorRange_Full, 8, yuv::Space::Bt709, true},
            {QVideoFrameFormat::ColorSpace_BT2020, QVideoFrameFormat::ColorRange_Video, 8, yuv::Space::Bt2020, false},
            // Untagged: legacy's guess, BT.709 from 600 lines (or wider than 1024).
            {QVideoFrameFormat::ColorSpace_Undefined, QVideoFrameFormat::ColorRange_Unknown, 598, yuv::Space::Bt601,
             false},
            {QVideoFrameFormat::ColorSpace_Undefined, QVideoFrameFormat::ColorRange_Unknown, 600, yuv::Space::Bt709,
             false},
        };
        for (const Case &c : cases) {
            IndexedFrame out;
            QVERIFY(hikari::backends::videoFrameToBgra(
                uniform(QVideoFrameFormat::Format_YUV420P, 16, c.height, c.space, c.range), out));
            QCOMPARE(differing(out, expected(yuv::matrix(c.want, c.full), kY, kU, kV)), 0);
        }
    }

    // The indexed video's matrix, when given, wins over the frame's: a
    // player frame has the paused frame's colours (V4's YCbCr matrix,
    // e.g. TV.601 over a BT.709 source).
    void theIndexedVideosMatrixApplies()
    {
        namespace ffms = hikari::application::ffms_colour;
        using Input = hikari::application::LegacyColourMatrix::Input;
        struct Case {
            Input input;
            yuv::Space want;
            bool full;
        };
        const Case cases[] = {
            {{ffms::kBt470bg, ffms::kRangeMpeg}, yuv::Space::Bt601, false},
            {{ffms::kSmpte170m, ffms::kRangeUnspecified}, yuv::Space::Bt601, false},
            {{ffms::kBt709, ffms::kRangeJpeg}, yuv::Space::Bt709, true},
            {{ffms::kFcc, ffms::kRangeMpeg}, yuv::Space::Fcc, false},
            {{ffms::kSmpte240m, ffms::kRangeMpeg}, yuv::Space::Smpte240m, false},
            {{9, ffms::kRangeMpeg}, yuv::Space::Bt2020, false},
            // RGB (no matrix): the frame's own (BT.709 limited here), with the input's range.
            {{ffms::kRgb, ffms::kRangeMpeg}, yuv::Space::Bt709, false},
        };
        for (const Case &c : cases) {
            IndexedFrame out;
            QVERIFY(hikari::backends::videoFrameToBgra(uniform(QVideoFrameFormat::Format_NV12, 16, 8), out, c.input));
            QCOMPARE(differing(out, expected(yuv::matrix(c.want, c.full), kY, kU, kV)), 0);
        }
    }

    // Rotation and mirroring are not applied (the indexed frames and the
    // subtitles take the coded picture as it is): a rotated stream's frame
    // keeps its coded size and pixels.
    void rotationAndMirroringAreNotApplied()
    {
        const int w = 24, h = 8;
        QVideoFrameFormat f(QSize(w, h), QVideoFrameFormat::Format_YUV420P);
        f.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
        f.setRotation(QtVideo::Rotation::Clockwise90); // the stream's orientation
        f.setMirrored(true);
        QVideoFrame frame(f);
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                frame.bits(0)[y * frame.bytesPerLine(0) + x] = uchar(x < w / 2 ? 40 : 200); // dark left, light right
        for (int plane = 1; plane < 3; ++plane)
            std::memset(frame.bits(plane), 128, static_cast<std::size_t>(frame.mappedBytes(plane)));
        frame.unmap();
        frame.setRotation(QtVideo::Rotation::Clockwise180); // and the frame's own
        frame.setMirrored(true);
        IndexedFrame out;
        QVERIFY(hikari::backends::videoFrameToBgra(frame, out));
        QCOMPARE(out.width, w);
        QCOMPARE(out.height, h);
        const auto *p = reinterpret_cast<const std::uint8_t *>(out.bgra.data());
        QVERIFY(p[0] < 64);                   // the top left is still dark
        QVERIFY(p[(w - 1) * 4] > 192);        // and the top right light
        // What Qt's own conversion makes of it, for the record.
        const QImage qt = QVideoFrame(frame).toImage();
        qInfo("Qt's toImage of the rotated frame: %dx%d", qt.width(), qt.height());
    }

    // Qt's own CPU conversion (what the player path used before) gives the
    // same colours within rounding, for both matrices and ranges.
    void matchesQtsOwnConversion_data()
    {
        QTest::addColumn<int>("space");
        QTest::addColumn<int>("range");
        QTest::newRow("BT.601 limited") << int(QVideoFrameFormat::ColorSpace_BT601)
                                        << int(QVideoFrameFormat::ColorRange_Video);
        QTest::newRow("BT.709 limited") << int(QVideoFrameFormat::ColorSpace_BT709)
                                        << int(QVideoFrameFormat::ColorRange_Video);
        QTest::newRow("BT.709 full") << int(QVideoFrameFormat::ColorSpace_BT709)
                                     << int(QVideoFrameFormat::ColorRange_Full);
    }
    void matchesQtsOwnConversion()
    {
        QFETCH(int, space);
        QFETCH(int, range);
        const QVideoFrame frame = uniform(QVideoFrameFormat::Format_YUV420P, 32, 16,
                                          QVideoFrameFormat::ColorSpace(space), QVideoFrameFormat::ColorRange(range));
        IndexedFrame out;
        QVERIFY(hikari::backends::videoFrameToBgra(frame, out));
        // Without a graphics device (offscreen) Qt's CPU fallback converts
        // every frame as BT.601 limited range; with one (xvfb, a desktop)
        // it follows the frame's matrix and range as the player path did.
        if (QGuiApplication::platformName() == QLatin1String("offscreen") &&
            !(space == QVideoFrameFormat::ColorSpace_BT601 && range == QVideoFrameFormat::ColorRange_Video))
            QSKIP("Qt's offscreen conversion is BT.601 limited only");
        const QImage qt = QVideoFrame(frame).toImage().convertToFormat(QImage::Format_RGB32);
        QVERIFY(!qt.isNull());
        const QRgb theirs = qt.pixel(5, 5);
        const auto *ours = reinterpret_cast<const std::uint8_t *>(out.bgra.data()) + 5 * out.stride + 5 * 4;
        const QString both = QStringLiteral("ours %1,%2,%3 Qt's %4,%5,%6")
                                 .arg(ours[2]).arg(ours[1]).arg(ours[0])
                                 .arg(qRed(theirs)).arg(qGreen(theirs)).arg(qBlue(theirs));
        qInfo().noquote() << both;
        QVERIFY2(std::abs(ours[2] - qRed(theirs)) <= 2 && std::abs(ours[1] - qGreen(theirs)) <= 2 &&
                     std::abs(ours[0] - qBlue(theirs)) <= 2,
                 qPrintable(both));
    }

    void aPooledBufferIsReusedAsItIs()
    {
        IndexedFrame out;
        QVERIFY(hikari::backends::videoFrameToBgra(uniform(QVideoFrameFormat::Format_NV12, 64, 32), out));
        const std::byte *buffer = out.bgra.data();
        QVERIFY(hikari::backends::videoFrameToBgra(uniform(QVideoFrameFormat::Format_NV12, 64, 32), out));
        QCOMPARE(out.bgra.data(), buffer);
    }

    void otherFormatsGoThroughQt()
    {
        QVideoFrame frame(QVideoFrameFormat(QSize(5, 3), QVideoFrameFormat::Format_BGRX8888));
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 5; ++x) {
                uchar *p = frame.bits(0) + y * frame.bytesPerLine(0) + x * 4;
                p[0] = 10;
                p[1] = 20;
                p[2] = 30;
                p[3] = 255;
            }
        frame.unmap();
        IndexedFrame out;
        QVERIFY(hikari::backends::videoFrameToBgra(frame, out));
        QCOMPARE(out.width, 5);
        QCOMPARE(out.height, 3);
        QCOMPARE(differing(out, {10, 20, 30, 255}), 0);
    }

    // V1, the GPU path: the planes videoFrameToYcbcr keeps (the presenter's
    // shader input) convert to exactly videoFrameToBgra's bytes, for every
    // layout, odd and tiny sizes, padded rows, short chroma mappings,
    // and the indexed video's matrix or the frame's own.
    void planesConvertToTheCpuPathsBytes_data()
    {
        QTest::addColumn<int>("format");
        QTest::addColumn<QSize>("size");
        const std::pair<const char *, QVideoFrameFormat::PixelFormat> formats[] = {
            {"NV12", QVideoFrameFormat::Format_NV12},       {"NV21", QVideoFrameFormat::Format_NV21},
            {"P010", QVideoFrameFormat::Format_P010},       {"P016", QVideoFrameFormat::Format_P016},
            {"YUV420P", QVideoFrameFormat::Format_YUV420P}, {"YV12", QVideoFrameFormat::Format_YV12},
            {"YUV420P10", QVideoFrameFormat::Format_YUV420P10}, {"YUV422P", QVideoFrameFormat::Format_YUV422P}};
        const QSize sizes[] = {{38, 9}, {33, 17}, {64, 36}, {7, 2}, {5, 3}, {3, 2}};
        for (const auto &[name, format] : formats)
            for (const QSize &size : sizes)
                QTest::addRow("%s %dx%d", name, size.width(), size.height()) << int(format) << size;
    }
    void planesConvertToTheCpuPathsBytes()
    {
        QFETCH(int, format);
        QFETCH(QSize, size);
        namespace ffms = hikari::application::ffms_colour;
        using Input = hikari::application::LegacyColourMatrix::Input;
        const std::optional<Input> colours[] = {std::nullopt, Input{ffms::kBt470bg, ffms::kRangeMpeg},
                                                Input{ffms::kBt709, ffms::kRangeJpeg}};
        const QVideoFrame frame = noisy(QVideoFrameFormat::PixelFormat(format), size.width(), size.height(), 7);
        QVERIFY(frame.isValid());
        for (const auto &colour : colours) {
            IndexedFrame bgra, planes, back;
            QVERIFY(hikari::backends::videoFrameToBgra(frame, bgra, colour));
            QVERIFY(hikari::backends::videoFrameToYcbcr(frame, planes, colour));
            QVERIFY2(planes.ycbcr.has_value(), "the frame was not kept as planes");
            QCOMPARE(planes.stride, 0);
            QCOMPARE(QString::fromStdString(hikari::application::ycbcrLayoutError(planes)), QString());
            QVERIFY(hikari::application::ycbcrToBgra(planes, back));
            const QString difference = firstDifference(back, bgra);
            QVERIFY2(difference.isEmpty(), qPrintable(difference));
        }
    }

    // A 1080p NV12 or YUV420P frame's planes take 1.5 bytes a pixel (the
    // BGRA frame 4): what the presenter uploads.
    void planesTakeOneAndAHalfBytesAPixel()
    {
        for (const auto format : {QVideoFrameFormat::Format_NV12, QVideoFrameFormat::Format_YUV420P}) {
            IndexedFrame planes;
            QVERIFY(hikari::backends::videoFrameToYcbcr(uniform(format, 1920, 1080), planes));
            QVERIFY(planes.ycbcr);
            QCOMPARE(hikari::application::ycbcrBytes(*planes.ycbcr), std::size_t(1920 * 1080 * 3 / 2));
        }
    }

    // A format without planes is made BGRA, as videoFrameToBgra makes it.
    void otherFormatsStayBgra()
    {
        QVideoFrame frame = noisy(QVideoFrameFormat::Format_BGRX8888, 5, 3, 3);
        IndexedFrame bgra, kept;
        QVERIFY(hikari::backends::videoFrameToBgra(frame, bgra));
        QVERIFY(hikari::backends::videoFrameToYcbcr(frame, kept));
        QVERIFY(!kept.ycbcr);
        QCOMPARE(kept.stride, 5 * 4);
        QVERIFY(firstDifference(kept, bgra).isEmpty());
        QVERIFY(!hikari::backends::videoFrameToYcbcr(QVideoFrame(), kept));
    }

    void anInvalidFrameGivesNothing()
    {
        IndexedFrame out;
        QVERIFY(!hikari::backends::videoFrameToBgra(QVideoFrame(), out));
    }
};

QTEST_MAIN(VideoFrameBgraTests)
#include "video_frame_bgra_tests.moc"
