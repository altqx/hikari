// N7: the scene-graph presenter. A presented frame with its libass overlay
// must match the CPU reference (the frame with the premultiplied overlay
// composited source-over), in the software renderer (offscreen) and the RHI
// renderer of a real window (xvfb). Also: superseded and stale submissions,
// resize placement, and invalid input. The start of M50-present.
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/libass_renderer.h"
#include "image_compare.h"
#include "video_presenter.h"

#include <QDir>
#include <QFile>
#include <QQuickWindow>
#include <QtTest>

#include <cstring>
#include <optional>

using namespace hikari;
using application::OverlayFrame;
using application::Presentation;
using application::PresentOutcome;
using application::PresentResult;
using application::PresentStage;
using ui::VideoPresenter;

namespace {

constexpr int kW = 96, kH = 64;

// Four flat quadrants: placement checks survive texture filtering.
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

// The CPU reference: frame, then the premultiplied overlay source-over.
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

} // namespace

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
        window->setColor(Qt::magenta); // anything not drawn by the presenter shows up
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

    // I2 (M50-present): a decoded BT.709 frame from the media helper with a
    // semi-transparent libass overlay presents as the CPU composite.
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
        if (!comparison.withinTolerance) { // keep the evidence
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
        presenter->setParentItem(other.contentItem()); // a window change before the upload
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
        QTRY_COMPARE(presenter->frameRect(), QRectF(24, 0, 144, 96)); // 3:2 fitted to the height
        const QImage grabbed = window->grabWindow();
        QCOMPARE(QColor(grabbed.pixel(10, 48)), QColor(Qt::black)); // letterbox
        QCOMPARE(QColor(grabbed.pixel(181, 48)), QColor(Qt::black));
        const QColor topLeft(grabbed.pixel(24 + 36, 24)), bottomRight(grabbed.pixel(24 + 108, 72));
        QVERIFY2(std::abs(topLeft.red() - 200) <= 2 && std::abs(topLeft.green() - 40) <= 2, qPrintable(topLeft.name()));
        QVERIFY2(std::abs(bottomRight.red() - 220) <= 2, qPrintable(bottomRight.name()));
        QCOMPARE(presenter->presentedGeneration(), 4u); // placement presents nothing new
    }

    void pixelAspectWidensTheFrame()
    {
        QVERIFY(presentAndWait({5, quadrants(), nullptr, {2.0}}));
        QCOMPARE(presenter->frameRect(), QRectF(0, 16, 96, 32)); // 192:64 display fitted into 96:64
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
        wrongOverlay->pixels.resize(400);
        wrongOverlay->empty = false;
        const auto mismatched = presentAndWait({6, quadrants(), wrongOverlay, {}});
        QVERIFY(mismatched);
        QCOMPARE(mismatched->outcome, PresentOutcome::Error);
    }
};

QTEST_MAIN(PresenterTests)
#include "presenter_tests.moc"
