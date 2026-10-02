// N5: general playback through Qt Multimedia. Every fixture frame carries its
// index as a barcode, so a delivered frame is identified by its pixels, not by
// a timestamp: the seek evidence compares the requested position, the
// delivered frame's reported start time, and which frame actually arrived.
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/qt_general_player.h"

#include <QImage>
#include <QSignalSpy>
#include <QVideoFrame>
#include <QtTest>

#include <cstdio>
#include <optional>

using hikari::application::Chapter;
using hikari::application::MediaDescription;
using hikari::application::PlayerError;
using hikari::application::SeekResult;
using hikari::backends::FfmsIndexedSource;
using hikari::backends::QtGeneralPlayer;

namespace {

std::string fixture(const char *kind)
{
    return std::string(HIKARI_MEDIA_FIXTURES) + "/" + kind + ".mkv";
}

// The 4x4 barcode: block (bx, by) is bit by * 4 + bx, white = 1.
int barcode(const QVideoFrame &frame)
{
    const QImage image = frame.toImage().convertToFormat(QImage::Format_RGB32);
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

// Frame start times of the fixtures, in microseconds.
std::int64_t cfrStartUs(int index)
{
    return static_cast<std::int64_t>(index) * 1'001'000'000 / 24'000;
}
std::int64_t vfrStartUs(int index)
{
    std::int64_t ms = 0;
    for (int i = 0; i < index; ++i)
        ms += 30 + 20 * (i % 3);
    return ms * 1000;
}

} // namespace

class GeneralPlayerTests : public QObject {
    Q_OBJECT

    std::optional<MediaDescription> open(QtGeneralPlayer &player, const char *kind)
    {
        std::optional<std::expected<MediaDescription, PlayerError>> result;
        player.open(fixture(kind), [&](auto r) { result = std::move(r); });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, 10'000) || !*result)
            return std::nullopt;
        return **result;
    }

    // Seeks while paused and returns the result with the delivered frame's index.
    std::pair<std::optional<SeekResult>, int> seek(QtGeneralPlayer &player, std::int64_t us)
    {
        int index = -1;
        auto connection = connect(&player, &QtGeneralPlayer::frameDelivered, this,
                                  [&](const QVideoFrame &f) { if (index < 0) index = barcode(f); });
        std::optional<std::expected<SeekResult, PlayerError>> result;
        player.seek(us, [&](auto r) { result = std::move(r); });
        const bool arrived = QTest::qWaitFor([&] { return result.has_value() && index >= 0; }, 10'000);
        Q_UNUSED(arrived); // a missing result fails below
        disconnect(connection);
        if (!result || !*result)
            return {std::nullopt, index};
        return {**result, index};
    }

    void pauseOnFirstFrame(QtGeneralPlayer &player)
    {
        const auto before = player.deliveredFrames();
        player.pause();
        QVERIFY(QTest::qWaitFor([&] { return player.deliveredFrames() > before; }, 10'000));
    }

private slots:
    void opensWithDurationTracksAndSeekability()
    {
        QtGeneralPlayer player(false);
        const auto d = open(player, "cfr");
        QVERIFY(d);
        QVERIFY(d->durationUs);
        QVERIFY2(std::abs(*d->durationUs - 2'002'000) <= 42'000, qPrintable(QString::number(*d->durationUs)));
        QVERIFY(d->seekable);
        QCOMPARE(d->videoTracks.size(), 1u);
        QCOMPARE(d->audioTracks.size(), 0u);
        QVERIFY(!d->audioOutput);
        QCOMPARE(d->generation, player.generation());
    }

    void unknownDurationIsReported()
    {
        QtGeneralPlayer player(false);
        const auto d = open(player, "unknown");
        QVERIFY(d);
        QVERIFY2(!d->durationUs, qPrintable(QString::number(d->durationUs.value_or(0))));
    }

    void tracksAreListedAndSelectable()
    {
        QtGeneralPlayer player(false);
        const auto d = open(player, "tracks");
        QVERIFY(d);
        QCOMPARE(d->audioTracks.size(), 2u);
        QCOMPARE(d->audioTracks[0].language, std::string("eng"));
        QCOMPARE(d->audioTracks[1].language, std::string("jpn"));
        QCOMPARE(d->audioTracks[0].title, std::string("Main"));
        QCOMPARE(d->audioTracks[1].title, std::string("Commentary"));
        QCOMPARE(d->subtitleTracks.size(), 1u);
        QCOMPARE(d->subtitleTracks[0].language, std::string("eng"));
        QVERIFY(player.selectAudioTrack(1));
        QCOMPARE(player.description().activeAudio, 1);
        QVERIFY(!player.selectAudioTrack(2));
        QVERIFY(player.selectSubtitleTrack(-1));
        QCOMPARE(player.description().activeSubtitle, -1);
    }

    void subtitlesShowWhenSelectedAndStaySuppressedOtherwise()
    {
        for (const int track : {0, -1}) {
            QtGeneralPlayer player(false);
            QVERIFY(open(player, "tracks"));
            QVERIFY(player.selectSubtitleTrack(track));
            QStringList texts;
            connect(&player, &QtGeneralPlayer::subtitleTextChanged, this, [&](const QString &t) {
                if (!t.isEmpty())
                    texts << t;
            });
            player.play();
            QVERIFY(QTest::qWaitFor(
                [&] { return player.mediaStatus() == hikari::application::MediaStatus::EndOfMedia; }, 15'000));
            if (track == 0)
                QCOMPARE(texts, QStringList{QStringLiteral("Hello")});
            else
                QVERIFY2(texts.isEmpty(), qPrintable(texts.join(QLatin1Char('|'))));
        }
    }

    void seekReportsRequestedAndDeliveredFrames_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<qint64>("requestedUs");
        QTest::newRow("cfr 1000 ms") << QStringLiteral("cfr") << qint64(1'000'000);
        QTest::newRow("cfr 1500 ms") << QStringLiteral("cfr") << qint64(1'500'000);
        QTest::newRow("cfr back to 200 ms") << QStringLiteral("cfr") << qint64(200'000);
        QTest::newRow("vfr 1035 ms") << QStringLiteral("vfr") << qint64(1'035'000);
        QTest::newRow("bframes 1000 ms") << QStringLiteral("bframes") << qint64(1'000'000);
    }

    void seekReportsRequestedAndDeliveredFrames()
    {
        QFETCH(QString, kind);
        QFETCH(qint64, requestedUs);
        QtGeneralPlayer player(false);
        QVERIFY(open(player, kind.toUtf8().constData()));
        pauseOnFirstFrame(player);
        const auto [result, index] = seek(player, requestedUs);
        QVERIFY(result);
        QVERIFY(index >= 0);
        QCOMPARE(result->requestedUs, requestedUs);
        // The reported start time names the frame that actually arrived.
        const std::int64_t truth = kind == QLatin1String("vfr") ? vfrStartUs(index) : cfrStartUs(index);
        QVERIFY2(std::abs(result->deliveredStartUs - truth) <= 1000,
                 qPrintable(QStringLiteral("frame %1 reported at %2 us, starts at %3 us")
                                .arg(index)
                                .arg(result->deliveredStartUs)
                                .arg(truth)));
        // Observation, not a promise: how far the general player landed.
        // On stderr: the Windows test logs keep it, QtTest's own output not always.
        std::fprintf(stderr, "seek observation %s: requested %lld us, delivered frame %d at %lld us (error %lld us)\n",
                     qPrintable(kind), static_cast<long long>(requestedUs), index,
                     static_cast<long long>(result->deliveredStartUs), static_cast<long long>(result->errorUs()));
        std::fflush(stderr);
        QVERIFY2(std::abs(result->errorUs()) <= 100'000, "within 100 ms of the request");
    }

    void clockIsAnEstimateOnlyWhilePlaying()
    {
        QtGeneralPlayer player(false);
        QVERIFY(open(player, "cfr"));
        QVERIFY(!player.clock().valid);
        player.play();
        QVERIFY(QTest::qWaitFor([&] { return player.clock().valid; }, 10'000));
        const auto playing = player.clock();
        QVERIFY(playing.uncertaintyUs > 0);
        QCOMPARE(playing.rate, 1.0);
        player.pause();
        QVERIFY(!player.clock().valid);
        std::optional<std::expected<SeekResult, PlayerError>> seeked;
        player.seek(500'000, [&](auto r) { seeked = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return seeked.has_value(); }, 10'000));
        player.play();
        QVERIFY(QTest::qWaitFor([&] { return player.clock().valid; }, 10'000));
        QVERIFY(player.clock().epoch > playing.epoch);
    }

    void replacedRequestsResolveStale()
    {
        QtGeneralPlayer player(false);
        std::optional<std::expected<MediaDescription, PlayerError>> first, second;
        player.open(fixture("cfr"), [&](auto r) { first = std::move(r); });
        player.open(fixture("vfr"), [&](auto r) { second = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return first.has_value() && second.has_value(); }, 10'000));
        QCOMPARE(first->error(), PlayerError::Stale);
        QVERIFY(second->has_value());
        std::optional<std::expected<SeekResult, PlayerError>> a, b;
        pauseOnFirstFrame(player);
        player.seek(300'000, [&](auto r) { a = std::move(r); });
        player.seek(900'000, [&](auto r) { b = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return a.has_value() && b.has_value(); }, 10'000));
        QCOMPARE(a->error(), PlayerError::Stale);
        QVERIFY(b->has_value());
    }

    void missingFilesFailExplicitly()
    {
        QtGeneralPlayer player(false);
        std::optional<std::expected<MediaDescription, PlayerError>> result;
        player.open(fixture("no-such-file"), [&](auto r) { result = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return result.has_value(); }, 10'000));
        QVERIFY(!result->has_value());
        std::optional<std::expected<SeekResult, PlayerError>> seeked;
        player.seek(0, [&](auto r) { seeked = std::move(r); });
        QVERIFY(seeked && !seeked->has_value());
    }

    void chaptersComeFromTheMediaHelper()
    {
        FfmsIndexedSource source(QStringLiteral(HIKARI_MEDIA_HELPER));
        std::optional<std::expected<std::vector<Chapter>, PlayerError>> tracks, plain, missing;
        source.chapters(fixture("tracks"), [&](auto r) { tracks = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return tracks.has_value(); }, 10'000));
        QVERIFY(tracks->has_value());
        QCOMPARE((*tracks)->size(), 2u);
        QCOMPARE((**tracks)[0].title, std::string("Opening"));
        QCOMPARE((**tracks)[0].startUs, 0);
        QCOMPARE((**tracks)[0].endUs, 1'000'000);
        QCOMPARE((**tracks)[1].title, std::string("Second"));
        QCOMPARE((**tracks)[1].startUs, 1'000'000);
        source.chapters(fixture("cfr"), [&](auto r) { plain = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return plain.has_value(); }, 10'000));
        QVERIFY(plain->has_value() && (*plain)->empty());
        source.chapters(fixture("no-such-file"), [&](auto r) { missing = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return missing.has_value(); }, 10'000));
        QVERIFY(!missing->has_value());
    }
};

QTEST_MAIN(GeneralPlayerTests)
#include "general_player_tests.moc"
