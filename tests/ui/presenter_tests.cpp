#include "hikari/application/ycbcr_frame.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/libass_renderer.h"
#include "image_compare.h"
#include "video_presenter.h"

#include <QDir>
#include <QFile>
#include <QQuickWindow>
#include <rhi/qrhi.h>
#include <QtTest>

#include <cstring>
#include <optional>
#include <random>

using namespace hikari;
using application::OverlayFrame;
using application::OverlayPixels;
using application::Presentation;
using application::PresentOutcome;
using application::PresentResult;
using application::PresentStage;
using ui::VideoPresenter;

namespace {

constexpr int kW = 96, kH = 64;

std::shared_ptr<application::IndexedFrame> quadrants(int w = kW, int h = kH)
{
    auto f = std::make_shared<application::IndexedFrame>();
    f->width = w;
    f->height = h;
    f->stride = w * 4;
    f->bgra.resize(static_cast<std::size_t>(f->stride) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const QRgb c = y < h / 2 ? (x < w / 2 ? qRgb(200, 40, 40) : qRgb(40, 200, 40))
                                     : (x < w / 2 ? qRgb(40, 40, 200) : qRgb(220, 220, 220));
            std::byte *p = f->bgra.data() + static_cast<std::size_t>(y) * f->stride + x * 4;
            p[0] = std::byte(qBlue(c));
            p[1] = std::byte(qGreen(c));
            p[2] = std::byte(qRed(c));
            p[3] = std::byte(255);
        }
    return f;
}

std::shared_ptr<const OverlayFrame> libassOverlay(int width = kW, int height = kH)
{
    QFile font(QStringLiteral(HIKARI_TEST_FONT));
    if (!font.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray d = font.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    const std::string script =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 96\nPlayResY: 64\n\n[V4+ Styles]\nFormat: Name, Fontname, "
        "Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, "
        "Encoding\nStyle: D,Titillium Web,20,&H4030A0E0,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,"
        "0,0,0,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:00.00,0:00:05.00,D,,0,0,0,,{\\pos(10,10)\\p1}m 0 0 l 50 0 40 30 0 40{\\p0}\n";
    std::vector<std::byte> bytesOfScript(script.size());
    std::memcpy(bytesOfScript.data(), script.data(), script.size());
    backends::LibassRenderer renderer;
    if (!renderer.prepare({bytesOfScript, {{"Titillium Web", bytes}}, "Titillium Web", false}))
        return nullptr;
    auto frame = renderer.render(core::DocumentTime(1'000'000), width, height);
    if (!frame)
        return nullptr;
    return std::make_shared<OverlayFrame>(std::move(*frame));
}

QImage cpuReference(const application::IndexedFrame &f, const OverlayFrame *o)
{
    QImage out(f.width, f.height, QImage::Format_RGB32);
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            const std::byte *p = f.bgra.data() + static_cast<std::size_t>(y) * f.stride + x * 4;
            int b = std::to_integer<int>(p[0]), g = std::to_integer<int>(p[1]), r = std::to_integer<int>(p[2]);
            if (o && !o->empty) {
                const std::uint8_t *q = &o->pixels[static_cast<std::size_t>(y) * o->stride + x * 4];
                const int inv = 255 - q[3];
                b = q[0] + (b * inv + 127) / 255;
                g = q[1] + (g * inv + 127) / 255;
                r = q[2] + (r * inv + 127) / 255;
            }
            out.setPixel(x, y, qRgb(r, g, b));
        }
    return out;
}

std::shared_ptr<const OverlayFrame> box(const QRect &rect, int w = kW, int h = kH)
{
    auto o = std::make_shared<OverlayFrame>();
    o->width = w;
    o->height = h;
    o->stride = w * 4;
    OverlayPixels::Bytes pixels(static_cast<std::size_t>(o->stride) * h, 0);
    o->empty = false;
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        for (int x = rect.left(); x <= rect.right(); ++x)
            std::memset(&pixels[static_cast<std::size_t>(y) * o->stride + x * 4], 128, 4);
    o->pixels = std::move(pixels);
    return o;
}

enum class Planes { Nv12, Yuv420p, Yuv422p };

std::shared_ptr<application::IndexedFrame> planar(Planes kind, int w, int h, const application::yuv::Matrix &matrix,
                                                  unsigned seed = 1)
{
    auto f = std::make_shared<application::IndexedFrame>();
    f->width = w;
    f->height = h;
    f->index = -1;
    f->ycbcr = application::ycbcrLayout(w, h, kind == Planes::Nv12, kind == Planes::Yuv422p, matrix);
    f->bgra.resize(application::ycbcrBytes(*f->ycbcr));
    std::mt19937 random(seed);
    for (auto &b : f->bgra)
        b = std::byte(random());
    return f;
}

std::shared_ptr<application::IndexedFrame> converted(const application::IndexedFrame &f)
{
    auto out = std::make_shared<application::IndexedFrame>();
    if (!application::ycbcrToBgra(f, *out))
        return nullptr;
    return out;
}

Presentation withBounds(std::uint64_t generation, std::shared_ptr<const application::IndexedFrame> frame,
                        std::shared_ptr<const OverlayFrame> overlay, std::optional<QRect> bounds)
{
    Presentation p{generation, std::move(frame), std::move(overlay), {}};
    p.overlayBoundsKnown = true;
    if (bounds)
        p.overlayBounds = application::PixelRect{bounds->x(), bounds->y(), bounds->width(), bounds->height()};
    return p;
}

}

class PresenterTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QQuickWindow> window;
    VideoPresenter *presenter = nullptr;

    std::optional<PresentResult> presentAndWait(Presentation p)
    {
        std::optional<PresentResult> result;
        presenter->present(std::move(p), [&](PresentResult r) { result = std::move(r); });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, 10'000))
            return std::nullopt;
        return result;
    }

private slots:
    void init()
    {
        window = std::make_unique<QQuickWindow>();
        window->setColor(Qt::magenta);
        window->resize(160, 120); // above the smallest window Windows decorates
        presenter = new VideoPresenter(window->contentItem());
        presenter->setSize(QSizeF(kW, kH));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    }
    void cleanup()
    {
        window.reset();
        presenter = nullptr;
    }

    void frameAndOverlayMatchTheCpuReference()
    {
        const auto frame = quadrants();
        const auto overlay = libassOverlay();
        QVERIFY(overlay && !overlay->empty);
        const auto result = presentAndWait({7, frame, overlay, {}});
        QVERIFY(result);
        QCOMPARE(result->outcome, PresentOutcome::Accepted);
        QCOMPARE(result->stage, PresentStage::Rendered);
        QCOMPARE(result->generation, 7u);
        QCOMPARE(presenter->presentedGeneration(), 7u);

        const QImage window = this->window->grabWindow().convertToFormat(QImage::Format_RGB32);
        QVERIFY(window.width() >= kW && window.height() >= kH);
        const QImage grabbed = window.copy(0, 0, kW, kH);
        QString message;
        const bool match = testing::compareImages(grabbed, cpuReference(*frame, overlay.get()), {2, 0}).withinTolerance;
        if (!match)
            testing::matchesReference(grabbed, QStringLiteral("presenter-composite-actual"), {2, 0},
                                      QStringLiteral(HIKARI_TEST_ARTIFACT_DIR), QStringLiteral(HIKARI_TEST_ARTIFACT_DIR),
                                      &message);
        QVERIFY2(match, "the presented frame and overlay differ from the CPU reference");
    }

    void decodedColourFrameWithOverlayMatchesTheCpuReference()
    {
        backends::FfmsIndexedSource source(QStringLiteral(HIKARI_MEDIA_HELPER));
        std::optional<std::expected<application::SourceTimeline, application::SourceError>> opened;
        source.open(std::string(HIKARI_MEDIA_FIXTURES) + "/color709.mkv", {}, [&](auto r) { opened = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return opened.has_value(); }, 10'000));
        QVERIFY(opened->has_value());
        std::optional<std::expected<application::IndexedFrame, application::SourceError>> decoded;
        source.frame(3, [&](auto r) { decoded = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return decoded.has_value(); }, 10'000));
        QVERIFY(decoded->has_value());
        auto frame = std::make_shared<application::IndexedFrame>(std::move(**decoded));
        const auto overlay = libassOverlay(frame->width, frame->height);
        QVERIFY(overlay && !overlay->empty);
        window->resize(frame->width, frame->height);
        presenter->setSize(QSizeF(frame->width, frame->height));
        const auto result = presentAndWait({frame->generation, frame, overlay, {}});
        QVERIFY(result);
        QCOMPARE(result->outcome, PresentOutcome::Accepted);
        QVERIFY(QTest::qWaitFor([&] { return window->grabWindow().size() == QSize(frame->width, frame->height); },
                                5'000));
        const QImage grabbed = window->grabWindow().convertToFormat(QImage::Format_RGB32);
        const auto comparison = testing::compareImages(grabbed, cpuReference(*frame, overlay.get()), {2, 0});
        if (!comparison.withinTolerance) {
            QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
            grabbed.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/decoded-composite-actual.png"));
            comparison.diff.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/decoded-composite-diff.png"));
        }
        QVERIFY2(comparison.withinTolerance,
                 qPrintable(QStringLiteral("%1 pixels differ, max delta %2")
                                .arg(comparison.differingPixels)
                                .arg(comparison.maxChannelDelta)));
    }

    void newerSubmissionsSupersedeOlderOnes()
    {
        std::optional<PresentResult> first, second;
        presenter->present({1, quadrants(), nullptr, {}}, [&](PresentResult r) { first = r; });
        presenter->present({2, quadrants(), nullptr, {}}, [&](PresentResult r) { second = r; });
        QVERIFY(QTest::qWaitFor([&] { return first && second; }, 10'000));
        QCOMPARE(first->outcome, PresentOutcome::Superseded);
        QCOMPARE(first->stage, PresentStage::Submitted);
        QCOMPARE(second->outcome, PresentOutcome::Accepted);
        QCOMPARE(presenter->presentedGeneration(), 2u);
    }

    void submissionsForAnOldSurfaceAreRejected()
    {
        QVERIFY(presentAndWait({1, quadrants(), nullptr, {}}));
        std::optional<PresentResult> stale;
        presenter->present({2, quadrants(), nullptr, {}}, [&](PresentResult r) { stale = r; });
        QQuickWindow other;
        other.resize(160, 120);
        presenter->setParentItem(other.contentItem());
        other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        QVERIFY(QTest::qWaitFor([&] { return stale.has_value(); }, 10'000));
        QCOMPARE(stale->outcome, PresentOutcome::Error);
        QCOMPARE(stale->stage, PresentStage::Submitted);
        QCOMPARE(presenter->rejectedSubmissions(), 1u);
        const auto fresh = presentAndWait({3, quadrants(), nullptr, {}});
        QVERIFY(fresh);
        QCOMPARE(fresh->outcome, PresentOutcome::Accepted);
        presenter->setParentItem(window->contentItem());
    }

    void resizingPlacesTheFrameWithoutReuploading()
    {
        QVERIFY(presentAndWait({4, quadrants(), nullptr, {}}));
        window->resize(192, 96);
        presenter->setSize(QSizeF(192, 96));
        QVERIFY(QTest::qWaitFor([&] { return window->grabWindow().size() == QSize(192, 96); }, 10'000));
        QTRY_COMPARE(presenter->frameRect(), QRectF(24, 0, 144, 96));
        const QImage grabbed = window->grabWindow();
        QCOMPARE(QColor(grabbed.pixel(10, 48)), QColor(Qt::black));
        QCOMPARE(QColor(grabbed.pixel(181, 48)), QColor(Qt::black));
        const QColor topLeft(grabbed.pixel(24 + 36, 24)), bottomRight(grabbed.pixel(24 + 108, 72));
        QVERIFY2(std::abs(topLeft.red() - 200) <= 2 && std::abs(topLeft.green() - 40) <= 2, qPrintable(topLeft.name()));
        QVERIFY2(std::abs(bottomRight.red() - 220) <= 2, qPrintable(bottomRight.name()));
        QCOMPARE(presenter->presentedGeneration(), 4u);
    }

    void onlyTheChangedOverlayRegionIsUploaded()
    {
        const auto frame = quadrants();
        const QRect a(10, 10, 8, 6), b(50, 30, 10, 4);
        QVERIFY(presentAndWait(withBounds(1, frame, box(a), a)));
        const auto first = presenter->uploadStats();
        QCOMPARE(first.frameUploads, 1);
        QCOMPARE(first.overlayUploads, 1);
        QVERIFY(presentAndWait(withBounds(2, frame, box(b), b)));
        const auto second = presenter->uploadStats();
        QCOMPARE(second.frameUploads, 1);
        QCOMPARE(second.overlayUploads, 2);
        const QRect region = second.lastOverlayUpload;
        QVERIFY2(region.contains(b), qPrintable(QStringLiteral("%1,%2 %3x%4").arg(region.x()).arg(region.y())
                                                    .arg(region.width()).arg(region.height())));
        if (window->rhi())
            QCOMPARE(region, a.united(b));
        else
            QCOMPARE(region, b.adjusted(-2, -2, 2, 2));
        const QImage grabbed = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, kW, kH);
        QVERIFY(testing::compareImages(grabbed, cpuReference(*frame, box(b).get()), {2, 0}).withinTolerance);
        QVERIFY(presentAndWait({3, frame, box(a), {}}));
        QCOMPARE(presenter->uploadStats().overlayUploads, 3);
        const QImage again = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, kW, kH);
        QVERIFY(testing::compareImages(again, cpuReference(*frame, box(a).get()), {2, 0}).withinTolerance);
    }

    void anEmptyOverlayIsNotDrawn()
    {
        const auto frame = quadrants();
        QVERIFY(presentAndWait(withBounds(1, frame, box(QRect(10, 10, 8, 6)), QRect(10, 10, 8, 6))));
        const int uploads = presenter->uploadStats().overlayUploads;
        QVERIFY(presentAndWait(withBounds(2, frame, box(QRect()), std::nullopt)));
        QVERIFY(presentAndWait({3, frame, nullptr, {}}));
        auto empty = std::make_shared<OverlayFrame>(*box(QRect()));
        empty->empty = true;
        QVERIFY(presentAndWait({4, frame, empty, {}}));
        QCOMPARE(presenter->uploadStats().overlayUploads, uploads);
        QCOMPARE(presenter->uploadStats().frameUploads, 1);
        const QImage grabbed = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, kW, kH);
        QVERIFY(testing::compareImages(grabbed, cpuReference(*frame, nullptr), {2, 0}).withinTolerance);
        QVERIFY(presentAndWait({5, quadrants(), nullptr, {}}));
        QCOMPARE(presenter->uploadStats().frameUploads, 2);
    }

    void aTextureThatCannotBeMadeFailsThePresent()
    {
        QVERIFY(presentAndWait({1, quadrants(), nullptr, {}}));
        QRhi *rhi = window->rhi();
        if (!rhi || !rhi->isTextureFormatSupported(QRhiTexture::BGRA8))
            QSKIP("the textures are kept only with an RHI backend");
        const int largest = rhi->resourceLimit(QRhi::TextureSizeMax);
        const auto failed = presentAndWait({2, quadrants(largest + 2, 2), nullptr, {}});
        QVERIFY(failed);
        QCOMPARE(failed->outcome, PresentOutcome::Error);
        QCOMPARE(failed->message, std::string("texture upload failed"));
        QCOMPARE(presenter->presentedGeneration(), 1u);
        const auto shown = presentAndWait({3, quadrants(), nullptr, {}});
        QVERIFY(shown);
        QCOMPARE(shown->outcome, PresentOutcome::Accepted);
        QCOMPARE(shown->stage, PresentStage::Rendered);
        const QImage grabbed = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, kW, kH);
        QVERIFY(testing::compareImages(grabbed, cpuReference(*quadrants(), nullptr), {2, 0}).withinTolerance);
    }

    void anOverlayAtScreenSizeIsPlacedOnTheFrame()
    {
        const QRect topRight(kW, 0, kW, kH);
        const auto overlay = box(topRight, kW * 2, kH * 2);
        const auto shows = [&] {
            const QImage grabbed = window->grabWindow();
            const QColor covered(grabbed.pixel(72, 16)), left(grabbed.pixel(24, 16)), below(grabbed.pixel(72, 48));
            return std::abs(covered.red() - 148) <= 3 && std::abs(covered.green() - 228) <= 3 &&
                   std::abs(left.red() - 200) <= 2 && std::abs(below.red() - 220) <= 2;
        };
        QVERIFY(presentAndWait({1, quadrants(), overlay, {}}));
        QVERIFY(QTest::qWaitFor(shows, 10'000));
        QVERIFY(presentAndWait(withBounds(2, quadrants(), overlay, topRight)));
        QVERIFY(QTest::qWaitFor(shows, 10'000));
        const double dpr = window->effectiveDevicePixelRatio();
        QCOMPARE(presenter->screenSize(), QSize(qRound(kW * dpr), qRound(kH * dpr)));
        QSignalSpy changed(presenter, &VideoPresenter::screenSizeChanged);
        presenter->setSourceRect(QRectF(0, 0, kW / 2.0, kH / 2.0));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(presenter->screenSize(), QSize(qRound(kW * 2 * dpr), qRound(kH * 2 * dpr)));
        presenter->setSourceRect({});
        QCOMPARE(presenter->screenSize(), QSize(qRound(kW * dpr), qRound(kH * dpr)));
    }

    void anOverlayOfABrokenLayoutIsRefused()
    {
        auto broken = std::make_shared<OverlayFrame>(*box(QRect(0, 0, 4, 4), kW * 2, kH * 2));
        broken->stride = 4;
        const auto result = presentAndWait({1, quadrants(), broken, {}});
        QVERIFY(result);
        QCOMPARE(result->outcome, PresentOutcome::Error);
        QCOMPARE(result->message, std::string("overlay layout does not match its buffer"));
    }

    void pixelAspectWidensTheFrame()
    {
        QVERIFY(presentAndWait({5, quadrants(), nullptr, {2.0}}));
        QCOMPARE(presenter->frameRect(), QRectF(0, 16, 96, 32));
    }

    void sharedVideoRectPlacesTheFrame()
    {
        QVERIFY(presentAndWait({7, quadrants(), nullptr, {}}));
        presenter->setVideoRect(QRectF(10, 4, 60, 40));
        QCOMPARE(presenter->frameRect(), QRectF(10, 4, 60, 40));
        presenter->setSourceRect(QRectF(0, 0, 48, 32));
        const auto placed = [&] {
            const QImage grabbed = window->grabWindow();
            const QColor inside(grabbed.pixel(40, 24)), outside(grabbed.pixel(5, 24));
            return std::abs(inside.red() - 200) <= 2 && std::abs(inside.green() - 40) <= 2 &&
                   outside == QColor(Qt::black);
        };
        QVERIFY(QTest::qWaitFor(placed, 10'000));
        presenter->setVideoRect({});
        presenter->setSourceRect({});
        QCOMPARE(presenter->frameRect().isEmpty(), false);
    }

    void ycbcrFramesShowAsTheirCpuConversion_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<int>("matrix");
        const std::pair<const char *, Planes> kinds[] = {
            {"NV12", Planes::Nv12}, {"YUV420P", Planes::Yuv420p}, {"YUV422P", Planes::Yuv422p}};
        const QSize sizes[] = {{96, 64}, {95, 63}, {33, 17}, {2, 1}};
        for (const auto &[name, kind] : kinds)
            for (const QSize &size : sizes)
                for (int matrix = 0; matrix < 3; ++matrix)
                    QTest::addRow("%s %dx%d m%d", name, size.width(), size.height(), matrix)
                        << int(kind) << size << matrix;
    }
    void ycbcrFramesShowAsTheirCpuConversion()
    {
        QFETCH(int, kind);
        QFETCH(QSize, size);
        QFETCH(int, matrix);
        namespace yuv = application::yuv;
        const yuv::Matrix matrices[] = {yuv::matrix(yuv::Space::Bt709, false), yuv::matrix(yuv::Space::Bt601, true),
                                        yuv::Matrix{298, 409, -100, -208, 516, 16, 8}}; // DirectShow's BT.601
        const int w = size.width(), h = size.height();
        const auto frame = planar(Planes(kind), w, h, matrices[matrix], unsigned(w * 7 + h + matrix));
        const auto cpu = converted(*frame);
        QVERIFY(cpu);
        const QRect boxed(w / 4, h / 3, std::max(1, w / 2), std::max(1, h / 3));
        const auto overlay = box(boxed, w, h);
        presenter->setSize(QSizeF(w, h));
        QVERIFY(presentAndWait(withBounds(1, cpu, overlay, boxed)));
        const QImage reference = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, w, h);
        QVERIFY(testing::compareImages(reference, cpuReference(*cpu, overlay.get()), {2, 0}).withinTolerance);
        const auto before = presenter->uploadStats();
        const auto result = presentAndWait(withBounds(2, frame, overlay, boxed));
        QVERIFY(result);
        QCOMPARE(result->outcome, PresentOutcome::Accepted);
        QCOMPARE(result->stage, PresentStage::Rendered);
        const auto after = presenter->uploadStats();
        if (presenter->convertsYcbcr()) {
            QCOMPARE(after.planarFrameUploads, before.planarFrameUploads + 1);
            QCOMPARE(after.lastFrameBytes, application::ycbcrBytes(*frame->ycbcr));
            QCOMPARE(after.cpuConvertedFrames, before.cpuConvertedFrames);
        } else {
            QCOMPARE(after.cpuConvertedFrames, before.cpuConvertedFrames + 1);
            QCOMPARE(after.planarFrameUploads, before.planarFrameUploads);
        }
        const QImage grabbed = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, w, h);
        const auto comparison = testing::compareImages(grabbed, reference, {2, 0});
        if (!comparison.withinTolerance) {
            QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
            grabbed.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/ycbcr-actual.png"));
            reference.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/ycbcr-reference.png"));
        }
        QVERIFY2(comparison.withinTolerance, qPrintable(QStringLiteral("%1 pixels differ, max delta %2 (%3)")
                                                            .arg(comparison.differingPixels)
                                                            .arg(comparison.maxChannelDelta)
                                                            .arg(presenter->convertsYcbcr() ? "GPU" : "CPU")));
        QVERIFY2(comparison.maxChannelDelta <= 1, qPrintable(QString::number(comparison.maxChannelDelta)));
    }

    void ycbcrFramesFollowZoomAndPlacement()
    {
        const int w = 95, h = 63;
        const auto frame = planar(Planes::Nv12, w, h, application::yuv::matrix(application::yuv::Space::Bt709, false), 3);
        const auto cpu = converted(*frame);
        QVERIFY(cpu);
        const QRect boxed(20, 11, 41, 23);
        const auto overlay = box(boxed, w, h);
        window->resize(200, 150);
        presenter->setSize(QSizeF(200, 150));
        QVERIFY(QTest::qWaitFor([&] { return window->grabWindow().size() == QSize(200, 150); }, 10'000));
        struct Placement {
            QRectF video, source;
        };
        const Placement placements[] = {
            {QRectF(7, 5, 181, 121), QRectF()},
            {QRectF(3, 9, 150, 100), QRectF(13, 7, 50, 31)},
            {QRectF(11, 2, 60, 41), QRectF(1, 3, 90, 59)},
        };
        for (const Placement &place : placements) {
            presenter->setVideoRect(place.video);
            presenter->setSourceRect(place.source);
            QVERIFY(presentAndWait(withBounds(1, cpu, overlay, boxed)));
            const QImage reference = window->grabWindow().convertToFormat(QImage::Format_RGB32);
            const auto shown = presentAndWait(withBounds(2, frame, overlay, boxed));
            QVERIFY(shown);
            QCOMPARE(shown->outcome, PresentOutcome::Accepted);
            const QImage grabbed = window->grabWindow().convertToFormat(QImage::Format_RGB32);
            const auto comparison = testing::compareImages(grabbed, reference, {2, 0});
            if (!comparison.withinTolerance) {
                QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
                grabbed.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/ycbcr-placed-actual.png"));
                reference.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/ycbcr-placed-reference.png"));
            }
            QVERIFY2(comparison.withinTolerance,
                     qPrintable(QStringLiteral("%1,%2 %3x%4: %5 pixels differ, max delta %6")
                                    .arg(place.video.x()).arg(place.video.y()).arg(place.video.width())
                                    .arg(place.video.height()).arg(comparison.differingPixels)
                                    .arg(comparison.maxChannelDelta)));
        }
        presenter->setVideoRect({});
        presenter->setSourceRect({});
    }

    void aPlanarFrameShownAgainIsNotUploadedAgain()
    {
        const auto frame = planar(Planes::Nv12, kW, kH, application::yuv::matrix(application::yuv::Space::Bt709, false));
        const QRect a(10, 10, 8, 6), b(50, 30, 10, 4);
        const auto shown = presentAndWait(withBounds(1, frame, box(a), a));
        QVERIFY(shown);
        QCOMPARE(shown->outcome, PresentOutcome::Accepted);
        const auto first = presenter->uploadStats();
        QVERIFY(presentAndWait(withBounds(2, frame, box(b), b)));
        const auto second = presenter->uploadStats();
        QCOMPARE(second.frameUploads, first.frameUploads);
        QCOMPARE(second.planarFrameUploads, first.planarFrameUploads);
        QCOMPARE(second.cpuConvertedFrames, first.cpuConvertedFrames);
        QCOMPARE(second.overlayUploads, first.overlayUploads + 1);
        const QImage grabbed = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, kW, kH);
        QVERIFY(testing::compareImages(grabbed, cpuReference(*converted(*frame), box(b).get()), {2, 0}).withinTolerance);
        QVERIFY(presentAndWait({3, quadrants(), nullptr, {}}));
        const QImage bgra = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, kW, kH);
        QVERIFY(testing::compareImages(bgra, cpuReference(*quadrants(), nullptr), {2, 0}).withinTolerance);
        const auto next = planar(Planes::Yuv420p, kW, kH, application::yuv::matrix(application::yuv::Space::Bt601, false), 9);
        QVERIFY(presentAndWait({4, next, nullptr, {}}));
        const QImage again = window->grabWindow().convertToFormat(QImage::Format_RGB32).copy(0, 0, kW, kH);
        QVERIFY(testing::compareImages(again, cpuReference(*converted(*next), nullptr), {2, 0}).withinTolerance);
    }

    void theGpuPathIsChosenByTheBackend()
    {
        QVERIFY(presentAndWait({1, quadrants(), nullptr, {}}));
        QRhi *rhi = window->rhi();
        const bool expected = rhi && rhi->isTextureFormatSupported(QRhiTexture::R8) &&
                              rhi->isTextureFormatSupported(QRhiTexture::RG8);
        qInfo().noquote() << "graphics:" << (rhi ? rhi->backendName() : QStringLiteral("software"))
                          << "converts Y'CbCr:" << presenter->convertsYcbcr();
        QCOMPARE(presenter->convertsYcbcr(), expected);
    }

    void aPlaneThatCannotBeMadeFailsThePresent()
    {
        QVERIFY(presentAndWait({1, quadrants(), nullptr, {}}));
        if (!presenter->convertsYcbcr())
            QSKIP("planes are uploaded only with the GPU path");
        const int largest = window->rhi()->resourceLimit(QRhi::TextureSizeMax);
        const auto failed = presentAndWait(
            {2, planar(Planes::Nv12, largest + 2, 2, application::yuv::matrix(application::yuv::Space::Bt709, false)),
             nullptr, {}});
        QVERIFY(failed);
        QCOMPARE(failed->outcome, PresentOutcome::Error);
        QCOMPARE(failed->message, std::string("texture upload failed"));
        const auto shown = presentAndWait(
            {3, planar(Planes::Nv12, kW, kH, application::yuv::matrix(application::yuv::Space::Bt709, false)), nullptr, {}});
        QVERIFY(shown);
        QCOMPARE(shown->outcome, PresentOutcome::Accepted);
    }

    void invalidInputIsRefusedAtSubmission()
    {
        auto broken = quadrants();
        broken->bgra.resize(10);
        const auto result = presentAndWait({6, broken, nullptr, {}});
        QVERIFY(result);
        QCOMPARE(result->outcome, PresentOutcome::Error);
        QCOMPARE(result->stage, PresentStage::Submitted);
        const auto wrongOverlay = std::make_shared<OverlayFrame>();
        wrongOverlay->width = 10;
        wrongOverlay->height = 10;
        wrongOverlay->stride = 40;
        wrongOverlay->pixels.resize(399);
        wrongOverlay->empty = false;
        const auto mismatched = presentAndWait({6, quadrants(), wrongOverlay, {}});
        QVERIFY(mismatched);
        QCOMPARE(mismatched->outcome, PresentOutcome::Error);
        auto shortPlanes = planar(Planes::Nv12, kW, kH, application::yuv::matrix(application::yuv::Space::Bt709, false));
        shortPlanes->bgra.resize(shortPlanes->bgra.size() - 1);
        const auto planesResult = presentAndWait({6, shortPlanes, nullptr, {}});
        QVERIFY(planesResult);
        QCOMPARE(planesResult->outcome, PresentOutcome::Error);
        QCOMPARE(planesResult->stage, PresentStage::Submitted);
    }
};

QTEST_MAIN(PresenterTests)
#include "presenter_tests.moc"
