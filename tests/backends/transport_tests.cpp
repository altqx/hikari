// I4: general <-> indexed handoff with the real Qt player and media helper.
// Frames carry their index as a barcode, so "the same frame" is checked by
// pixels on both sides of a switch.
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/qt_general_player.h"
#include "hikari/backends/transport_coordinator.h"

#include <QImage>
#include <QVideoFrame>
#include <QtTest>

#include <cstdio>
#include <optional>

using namespace hikari;
using namespace hikari::application;
using backends::HandoffAck;
using backends::HandoffError;
using backends::TransportCoordinator;
using backends::TransportMode;

namespace {

std::string fixture(const char *kind)
{
    return std::string(HIKARI_MEDIA_FIXTURES) + "/" + kind + ".mkv";
}

int barcode(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    if (image.isNull())
        return -1;
    int value = 0;
    for (int by = 0; by < 4; ++by)
        for (int bx = 0; bx < 4; ++bx) {
            const QRgb p = image.pixel((2 * bx + 1) * image.width() / 8, (2 * by + 1) * image.height() / 8);
            if ((qRed(p) + qGreen(p) + qBlue(p)) / 3 > 128)
                value |= 1 << (by * 4 + bx);
        }
    return value;
}

int barcode(const IndexedFrame &f)
{
    const QImage image(reinterpret_cast<const uchar *>(f.bgra.data()), f.width, f.height, f.stride,
                       QImage::Format_RGB32);
    return barcode(image);
}

// Accepts every presentation and remembers what it was shown.
class RecordingPresenter : public PresenterPort {
public:
    void present(Presentation p, Done done) override
    {
        shown.push_back(barcode(*p.frame));
        QMetaObject::invokeMethod(qApp, [done, g = p.generation] {
            done({g, PresentOutcome::Accepted, PresentStage::Rendered, {}});
        }, Qt::QueuedConnection);
    }
    std::vector<int> shown;
};

// The editor output, counting stops; nothing plays in these tests.
class SilentOutput : public AudioOutputPort {
public:
    std::vector<OutputDevice> devices() override { return {}; }
    std::expected<OutputFormat, OutputError> open(const std::string &, OutputFormat f) override { return f; }
    std::expected<void, OutputError> start() override { ++starts; return {}; }
    void stop() override { ++stops; }
    void close() override {}
    OutputFormat format() const override { return {}; }
    std::size_t write(std::span<const float> s) override { return s.size(); }
    OutputStatus status() const override { return {}; }
    ClockEstimate clock() const override { return {}; }
    int starts = 0, stops = 0;
};

} // namespace

class TransportTests : public QObject {
    Q_OBJECT

    struct Rig {
        backends::QtGeneralPlayer general{false};
        backends::FfmsIndexedSource indexed{QStringLiteral(HIKARI_MEDIA_HELPER)};
        SilentOutput output;
        RecordingPresenter presenter;
        TransportCoordinator coordinator{general, indexed, output, &presenter};
        int lastGeneralFrame = -1;
        Rig()
        {
            QObject::connect(&general, &backends::QtGeneralPlayer::frameDelivered, &general,
                             [this](const QVideoFrame &f) { lastGeneralFrame = barcode(f.toImage()); });
        }
    };

    static bool open(Rig &rig, const char *kind, int audio = 0)
    {
        std::optional<std::expected<SourceTimeline, HandoffError>> opened;
        rig.coordinator.open(fixture(kind), audio, [&](auto r) { opened = std::move(r); });
        return QTest::qWaitFor([&] { return opened.has_value(); }, 15'000) && opened->has_value();
    }
    static std::optional<std::expected<HandoffAck, HandoffError>> run(std::function<void(TransportCoordinator::Acked)> f)
    {
        std::optional<std::expected<HandoffAck, HandoffError>> ack;
        f([&](auto r) { ack = std::move(r); });
        if (!QTest::qWaitFor([&] { return ack.has_value(); }, 15'000))
            return std::nullopt;
        return ack;
    }

private slots:
    void toIndexedAcknowledgesTheFrameThePlayerShowed_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("cfr") << QStringLiteral("cfr");
        QTest::newRow("vfr") << QStringLiteral("vfr");
        QTest::newRow("bframes") << QStringLiteral("bframes");
    }
    void toIndexedAcknowledgesTheFrameThePlayerShowed()
    {
        QFETCH(QString, kind);
        Rig rig;
        QVERIFY(open(rig, kind.toUtf8().constData()));
        QCOMPARE(rig.coordinator.mode(), TransportMode::General);
        rig.general.play();
        QVERIFY(QTest::qWaitFor([&] { return rig.lastGeneralFrame >= 10; }, 10'000));
        const int showing = rig.lastGeneralFrame;
        const auto ack = run([&](auto done) { rig.coordinator.toIndexed(std::move(done)); });
        QVERIFY(ack && ack->has_value());
        QCOMPARE(rig.general.playbackState(), PlaybackState::Stopped);
        QCOMPARE(rig.coordinator.mode(), TransportMode::Indexed);
        // The frame resolved by timestamp lookup is the one the player showed.
        QCOMPARE((*ack)->indexedFrame, showing);
        QCOMPARE(rig.presenter.shown, std::vector<int>{showing});
        QCOMPARE(rig.output.starts, 0); // the new owner has not started anything
        std::fprintf(stderr, "%s: player showed frame %d at %lld us; indexed frame %d at %lld us\n",
                     qPrintable(kind), showing, static_cast<long long>((*ack)->carriedUs), (*ack)->indexedFrame,
                     static_cast<long long>((*ack)->indexedFrameUs));
    }

    void toGeneralStopsTheEditorOutputAndReportsTheDeliveredFrame()
    {
        Rig rig;
        QVERIFY(open(rig, "cfr"));
        rig.general.play();
        QVERIFY(QTest::qWaitFor([&] { return rig.lastGeneralFrame >= 20; }, 10'000));
        QVERIFY(run([&](auto done) { rig.coordinator.toIndexed(std::move(done)); })->has_value());
        const int indexed = rig.coordinator.indexedFrame();
        const int stopsBefore = rig.output.stops;
        rig.lastGeneralFrame = -1;
        const auto ack = run([&](auto done) { rig.coordinator.toGeneral(std::move(done)); });
        QVERIFY(ack && ack->has_value());
        QCOMPARE(rig.coordinator.mode(), TransportMode::General);
        QVERIFY(rig.output.stops > stopsBefore); // the editor output stopped and flushed first
        QVERIFY((*ack)->delivered.has_value());
        QCOMPARE((*ack)->carriedUs, rig.coordinator.frameStartUs(indexed));
        QVERIFY(QTest::qWaitFor([&] { return rig.lastGeneralFrame >= 0; }, 5'000));
        // General playback promises no exact frame: report what it delivered.
        std::fprintf(stderr, "to general from indexed frame %d: player delivered frame %d at %lld us\n", indexed,
                     rig.lastGeneralFrame, static_cast<long long>((*ack)->delivered->deliveredStartUs));
        QVERIFY(std::abs(rig.lastGeneralFrame - indexed) <= 1);
    }

    void aNewerSwitchSupersedesOneInFlight()
    {
        Rig rig;
        QVERIFY(open(rig, "cfr"));
        rig.general.play();
        QVERIFY(QTest::qWaitFor([&] { return rig.lastGeneralFrame >= 5; }, 10'000));
        std::optional<std::expected<HandoffAck, HandoffError>> first, second;
        rig.coordinator.toIndexed([&](auto r) { first = std::move(r); });
        rig.coordinator.toGeneral([&](auto r) { second = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return first && second; }, 15'000));
        QCOMPARE(first->error(), HandoffError::Superseded);
        QVERIFY(second->has_value());
        QCOMPARE(rig.coordinator.mode(), TransportMode::General);
        QVERIFY(rig.presenter.shown.empty()); // the superseded frame was never presented
    }

    void tracksAreMatchedAndPlayerSubtitlesSuppressed()
    {
        Rig rig;
        QVERIFY(open(rig, "tracks", 1));
        const auto d = rig.general.description();
        QCOMPARE(d.activeSubtitle, -1);
        QCOMPARE(d.activeAudio, 1);
        QCOMPARE(rig.coordinator.timeline().audioTracks.size(), 2u);
        std::optional<std::expected<AudioBlock, SourceError>> block;
        rig.indexed.audio(48000, 4, [&](auto r) { block = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return block.has_value(); }, 10'000));
        QVERIFY(block->has_value());
        // The second track is silence; the first is the ramp.
        for (std::byte b : (*block)->samples)
            QCOMPARE(std::to_integer<int>(b), 0);
    }

    void wrongModesAreRefused()
    {
        Rig rig;
        auto ack = run([&](auto done) { rig.coordinator.toIndexed(std::move(done)); });
        QVERIFY(ack && !ack->has_value());
        QCOMPARE(ack->error(), HandoffError::NotOpen);
        QVERIFY(open(rig, "cfr"));
        ack = run([&](auto done) { rig.coordinator.toGeneral(std::move(done)); });
        QCOMPARE(ack->error(), HandoffError::WrongMode);
    }
};

QTEST_MAIN(TransportTests)
#include "transport_tests.moc"
