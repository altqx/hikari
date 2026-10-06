// W1: the optional DirectShow playback adapter (Windows). Its frames are
// identified by the barcode each fixture frame carries, as the general
// player's are (general_player_tests.cpp): a seek's evidence is the requested
// position, the delivered frame's reported start and which frame arrived,
// and each is compared with the default players on the same media (the Qt
// general player for playback, the media helper's index for frame identity).
#include "directshow_frames.h"
#include "hikari/backends/directshow_player.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/qt_general_player.h"

#include <QCoreApplication>
#include <QImage>
#include <QVideoFrame>
#include <QtTest>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <optional>
#include <thread>

using hikari::application::IndexedFrame;
using hikari::application::MediaDescription;
using hikari::application::PlaybackState;
using hikari::application::PlayerError;
using hikari::application::SeekResult;
using hikari::backends::DirectShowPlayer;
using hikari::backends::FfmsIndexedSource;
using hikari::backends::QtGeneralPlayer;

namespace {

std::string fixture(const char *name)
{
    return std::string(HIKARI_MEDIA_FIXTURES) + "/" + name;
}

void post(std::function<void()> task)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(task), Qt::QueuedConnection);
}

// The fixtures' 4x4 barcode: block (bx, by) is bit by * 4 + bx, white = 1.
int barcode(const QImage &image)
{
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
    return barcode(QImage(reinterpret_cast<const uchar *>(f.bgra.data()), f.width, f.height, f.stride,
                          QImage::Format_RGB32));
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
std::int64_t startUs(bool vfr, int index)
{
    return vfr ? vfrStartUs(index) : cfrStartUs(index);
}
// The frame whose interval holds `us` (48 frames).
int frameContaining(bool vfr, std::int64_t us)
{
    int index = 0;
    while (index + 1 < 48 && startUs(vfr, index + 1) <= us)
        ++index;
    return index;
}

struct Delivered {
    int frames = 0;
    int lastBarcode = -1;
    std::int64_t lastStartUs = -1;
    std::vector<std::pair<int, std::int64_t>> all; // barcode, reported start
};

} // namespace

class DirectShowPlayerTests : public QObject {
    Q_OBJECT

    std::optional<MediaDescription> open(DirectShowPlayer &player, const char *name)
    {
        std::optional<std::expected<MediaDescription, PlayerError>> result;
        player.open(fixture(name), [&](auto r) { result = std::move(r); });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, 10'000) || !*result)
            return std::nullopt;
        return **result;
    }

    void watch(DirectShowPlayer &player, Delivered &delivered)
    {
        player.setFrameSink([&delivered](IndexedFrame frame, std::int64_t start) {
            ++delivered.frames;
            delivered.lastBarcode = barcode(frame);
            delivered.lastStartUs = start;
            delivered.all.emplace_back(delivered.lastBarcode, start);
        });
    }

    // Seeks and returns the result with the delivered frame's barcode: the
    // frame the sink receives right after the answer (the same delivery).
    std::pair<std::optional<SeekResult>, int> seek(DirectShowPlayer &player, Delivered &delivered, std::int64_t us)
    {
        std::optional<std::expected<SeekResult, PlayerError>> result;
        std::size_t at = 0;
        player.seek(us, [&](auto r) {
            result = std::move(r);
            at = delivered.all.size();
        });
        QTest::qWaitFor([&] { return result.has_value() && (!*result || delivered.all.size() > at); }, 10'000);
        if (!result || !*result || delivered.all.size() <= at)
            return {std::nullopt, -1};
        return {**result, delivered.all[at].first};
    }

    // From stopped, a pause shows the first frame (legacy's paused video).
    void pauseOnFirstFrame(DirectShowPlayer &player, Delivered &delivered)
    {
        const int before = delivered.frames;
        player.pause();
        QVERIFY(QTest::qWaitFor([&] { return delivered.frames > before; }, 10'000));
    }

    // The Qt general player on the same file: the frame its seek delivered.
    std::pair<std::optional<SeekResult>, int> qtSeek(const char *name, std::int64_t us)
    {
        QtGeneralPlayer player(false);
        std::optional<std::expected<MediaDescription, PlayerError>> opened;
        player.open(fixture(name), [&](auto r) { opened = std::move(r); });
        if (!QTest::qWaitFor([&] { return opened.has_value(); }, 10'000) || !*opened)
            return {std::nullopt, -1};
        const auto first = player.deliveredFrames();
        player.pause();
        QTest::qWaitFor([&] { return player.deliveredFrames() > first; }, 10'000);
        int index = -1;
        auto connection = connect(&player, &QtGeneralPlayer::frameDelivered, this, [&](const QVideoFrame &f) {
            if (index < 0)
                index = barcode(f.toImage().convertToFormat(QImage::Format_RGB32));
        });
        std::optional<std::expected<SeekResult, PlayerError>> result;
        player.seek(us, [&](auto r) { result = std::move(r); });
        QTest::qWaitFor([&] { return result.has_value() && index >= 0; }, 10'000);
        disconnect(connection);
        if (!result || !*result)
            return {std::nullopt, index};
        return {**result, index};
    }

private slots:
    void opensWithTheLegacyGraph_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<qint64>("durationUs");
        QTest::addColumn<double>("fps");
        QTest::newRow("cfr avi") << QStringLiteral("dshow-cfr.avi") << qint64(2'002'000) << 23.976;
        QTest::newRow("vfr wmv") << QStringLiteral("dshow-vfr.wmv") << qint64(2'400'000) << 0.0;
    }

    void opensWithTheLegacyGraph()
    {
        QFETCH(QString, name);
        QFETCH(qint64, durationUs);
        QFETCH(double, fps);
        DirectShowPlayer player(post);
        const auto d = open(player, name.toUtf8().constData());
        QVERIFY(d);
        QCOMPARE(d->generation, player.generation());
        QVERIFY(d->seekable);
        QVERIFY(d->durationUs);
        std::fprintf(stderr, "%s: duration %lld us\n", qPrintable(name), static_cast<long long>(*d->durationUs));
        QVERIFY2(std::abs(*d->durationUs - durationUs) <= 150'000, qPrintable(QString::number(*d->durationUs)));
        QCOMPARE(d->videoTracks.size(), 1u);
        QVERIFY(d->audioTracks.empty());
        // Legacy's own filters by their names, among the graph's (the
        // splitter and decoder are the system's).
        QStringList names;
        for (const auto &f : player.filters()) {
            names << QString::fromStdString(f.name) + (f.hasPropertyPages ? QStringLiteral(" [pages]") : QString());
        }
        std::fprintf(stderr, "%s filters: %s\n", qPrintable(name), qPrintable(names.join(QStringLiteral(", "))));
        const auto has = [&](const QString &filter) {
            return std::ranges::any_of(names, [&](const QString &n) { return n.startsWith(filter); });
        };
        QVERIFY(has(QStringLiteral("HikariSub video Renderer")));
        QVERIFY(has(QStringLiteral("Direct Sound Renderer")));
        QVERIFY(has(QStringLiteral("Source Filter")));
        QVERIFY(names.size() >= 4);
        // The renderer's format, as legacy's SetMediaType records it.
        const auto format = player.videoFormat();
        std::fprintf(stderr, "%s format: %dx%d %.3f fps %s\n", qPrintable(name), format.width, format.height,
                     format.fps, format.subtype.c_str());
        QCOMPARE(format.width, 320);
        QCOMPARE(format.height, 240);
        if (fps > 0)
            QVERIFY2(std::abs(format.fps - fps) < 0.01, qPrintable(QString::number(format.fps)));
        QCOMPARE(player.playbackState(), PlaybackState::Stopped);
        QCOMPARE(player.mediaStatus(), hikari::application::MediaStatus::Loaded);
    }

    // The frame/seek fixtures: the adapter against the default players on
    // the same media (CFR and VFR).
    void seekDeliversTheRequestedFrame_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<qint64>("requestedUs");
        QTest::newRow("cfr 1000 ms") << QStringLiteral("dshow-cfr.avi") << qint64(1'000'000);
        QTest::newRow("cfr 1500 ms") << QStringLiteral("dshow-cfr.avi") << qint64(1'500'000);
        QTest::newRow("cfr back to 200 ms") << QStringLiteral("dshow-cfr.avi") << qint64(200'000);
        QTest::newRow("cfr frame 30 start") << QStringLiteral("dshow-cfr.avi") << qint64(cfrStartUs(30));
        QTest::newRow("vfr 1035 ms") << QStringLiteral("dshow-vfr.wmv") << qint64(1'035'000);
        QTest::newRow("vfr 500 ms") << QStringLiteral("dshow-vfr.wmv") << qint64(500'000);
        QTest::newRow("vfr frame 20 start") << QStringLiteral("dshow-vfr.wmv") << qint64(vfrStartUs(20));
    }

    void seekDeliversTheRequestedFrame()
    {
        QFETCH(QString, name);
        QFETCH(qint64, requestedUs);
        const bool vfr = name.contains(QLatin1String("vfr"));
        const QByteArray file = name.toUtf8();
        DirectShowPlayer player(post);
        Delivered delivered;
        watch(player, delivered);
        QVERIFY(open(player, file.constData()));
        // Paused first (legacy shows the video paused), then sought there.
        pauseOnFirstFrame(player, delivered);
        const auto [result, index] = seek(player, delivered, requestedUs);
        QVERIFY(result);
        QVERIFY(index >= 0);
        QCOMPARE(result->requestedUs, requestedUs);
        QCOMPARE(result->generation, player.generation());
        // The reported start names the frame that actually arrived.
        QVERIFY2(std::abs(result->deliveredStartUs - startUs(vfr, index)) <= 1000,
                 qPrintable(QStringLiteral("frame %1 reported at %2 us, starts at %3 us")
                                .arg(index)
                                .arg(result->deliveredStartUs)
                                .arg(startUs(vfr, index))));
        // The index's answer for the same time: the frame whose interval holds it.
        const int indexed = frameContaining(vfr, requestedUs);
        const auto [qt, qtIndex] = qtSeek(file.constData(), requestedUs);
        std::fprintf(stderr,
                     "seek comparison %s: requested %lld us; DirectShow frame %d at %lld us; indexed frame %d; "
                     "Qt general player frame %d at %lld us\n",
                     qPrintable(name), static_cast<long long>(requestedUs), index,
                     static_cast<long long>(result->deliveredStartUs), indexed, qtIndex,
                     static_cast<long long>(qt ? qt->deliveredStartUs : -1));
        std::fflush(stderr);
        QCOMPARE(index, indexed);
        QVERIFY2(qt && std::abs(qtIndex - indexed) <= 1, "the Qt general player lands on the same frame or next to it");
    }

    void playbackDeliversFramesInOrderAndAnchorsTheClock()
    {
        for (const char *name : {"dshow-cfr.avi", "dshow-vfr.wmv"}) {
            const bool vfr = std::string_view(name).find("vfr") != std::string_view::npos;
            DirectShowPlayer player(post);
            Delivered delivered;
            watch(player, delivered);
            QVERIFY(open(player, name));
            QVERIFY(!player.clock().valid);
            pauseOnFirstFrame(player, delivered);
            const auto [sought, first] = seek(player, delivered, 0);
            QVERIFY(sought);
            QCOMPARE(first, 0);
            const auto before = delivered.all.size();
            player.play();
            QCOMPARE(player.playbackState(), PlaybackState::Playing);
            QVERIFY(QTest::qWaitFor([&] { return delivered.all.size() >= before + 10; }, 10'000));
            QVERIFY(player.clock().valid);
            QVERIFY(player.clock().uncertaintyUs > 0);
            player.pause();
            QVERIFY(!player.clock().valid);
            QCOMPARE(player.playbackState(), PlaybackState::Paused);
            // Each frame once, in order, at its own time.
            int previous = -1;
            for (std::size_t i = before; i < delivered.all.size(); ++i) {
                const auto [code, start] = delivered.all[i];
                QVERIFY2(code > previous, qPrintable(QStringLiteral("%1 after %2").arg(code).arg(previous)));
                QVERIFY2(std::abs(start - startUs(vfr, code)) <= 1000,
                         qPrintable(QStringLiteral("frame %1 at %2 us").arg(code).arg(start)));
                previous = code;
            }
            std::fprintf(stderr, "%s: played frames %d..%d\n", name, delivered.all[before].first, previous);
        }
    }

    void playsToTheEnd()
    {
        DirectShowPlayer player(post);
        Delivered delivered;
        watch(player, delivered);
        int changes = 0;
        player.setStateObserver([&] { ++changes; });
        QVERIFY(open(player, "dshow-cfr.avi"));
        pauseOnFirstFrame(player, delivered);
        QVERIFY(seek(player, delivered, 1'800'000).first);
        player.play();
        QVERIFY(QTest::qWaitFor([&] { return player.mediaStatus() == hikari::application::MediaStatus::EndOfMedia; },
                                10'000));
        QCOMPARE(delivered.lastBarcode, 47);
        QVERIFY(changes > 0);
        // A seek after the end plays on from there.
        QVERIFY(seek(player, delivered, 0).first);
        QCOMPARE(player.mediaStatus(), hikari::application::MediaStatus::Loaded);
    }

    void replacedRequestsResolveStaleAndFailuresAreExplicit()
    {
        DirectShowPlayer player(post);
        std::optional<std::expected<SeekResult, PlayerError>> early;
        player.seek(0, [&](auto r) { early = std::move(r); });
        QVERIFY(early && early->error() == PlayerError::NotOpen);
        std::string failed;
        player.setOpenFailed([&](const std::string &message) { failed = message; });
        std::optional<std::expected<MediaDescription, PlayerError>> missing;
        player.open(fixture("no-such-file.avi"), [&](auto r) { missing = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return missing.has_value(); }, 10'000));
        QVERIFY(!missing->has_value());
        QCOMPARE(failed, std::string("Source filter not added")); // legacy's HR message
        QCOMPARE(player.mediaStatus(), hikari::application::MediaStatus::Invalid);
        QVERIFY(player.filters().empty());
        // Two seeks: the first is replaced.
        Delivered delivered;
        watch(player, delivered);
        QVERIFY(open(player, "dshow-cfr.avi"));
        pauseOnFirstFrame(player, delivered);
        std::optional<std::expected<SeekResult, PlayerError>> a, b;
        player.seek(300'000, [&](auto r) { a = std::move(r); });
        player.seek(900'000, [&](auto r) { b = std::move(r); });
        QVERIFY(QTest::qWaitFor([&] { return a.has_value() && b.has_value(); }, 10'000));
        QCOMPARE(a->error(), PlayerError::Stale);
        QVERIFY(b->has_value());
        QVERIFY(std::abs((*b)->deliveredStartUs - 900'000) <= 42'000);
        // An open replaces the media: a pending seek goes stale.
        std::optional<std::expected<SeekResult, PlayerError>> c;
        player.seek(1'200'000, [&](auto r) { c = std::move(r); });
        QVERIFY(open(player, "dshow-vfr.wmv"));
        QVERIFY(c && c->error() == PlayerError::Stale);
        c.reset();
        player.seek(-1, [&](auto r) { c = std::move(r); });
        QVERIFY(c && c->error() == PlayerError::InvalidInput);
    }

    // Legacy SetVolume(-(pos * pos)) through the volume gain: hundredths of
    // a dB on IBasicAudio.
    void volumeIsLegacysHundredthsOfADecibel()
    {
        DirectShowPlayer player(post);
        player.setVolume(std::pow(10.0, -1.0 / 20.0)); // -1 dB: legacy pos 10
        if (!open(player, "dshow-cfr.avi"))
            QFAIL("open");
        if (player.appliedVolume() == 0)
            QSKIP("IBasicAudio takes no volume without an audio output");
        QCOMPARE(player.appliedVolume(), -100L);
        player.setVolume(0);
        QCOMPARE(player.appliedVolume(), -10000L);
    }

    // Legacy FilterConfig: a filter's property pages in a modal frame. A
    // watcher records the frame and closes it. The MPEG-1 graph holds a stock
    // filter with property pages (the MPEG video decoder); the AVI and ASF
    // graphs' DMO wrappers have none, so the menu lists them disabled.
    void filterPropertyPagesOpen()
    {
        {
            DirectShowPlayer avi(post);
            QVERIFY(open(avi, "dshow-cfr.avi"));
            for (const auto &f : avi.filters())
                if (f.name == "HikariSub video Renderer" || f.name == "Source Filter")
                    QVERIFY(!f.hasPropertyPages);
        }
        DirectShowPlayer player(post);
        QVERIFY(open(player, "dshow-mpg.mpg"));
        std::string name;
        QStringList all;
        for (const auto &f : player.filters()) {
            all << QString::fromStdString(f.name) + (f.hasPropertyPages ? QStringLiteral(" [pages]") : QString());
            if (f.hasPropertyPages && name.empty())
                name = f.name;
        }
        std::fprintf(stderr, "dshow-mpg.mpg filters: %s\n", qPrintable(all.join(QStringLiteral(", "))));
        QVERIFY2(!name.empty(), "a filter of the MPEG-1 graph has property pages");
        std::atomic<bool> stop{false};
        std::wstring seen;
        std::thread watcher([&] {
            const DWORD process = GetCurrentProcessId();
            while (!stop) {
                struct Search {
                    DWORD process;
                    HWND found = nullptr;
                } search{process};
                EnumWindows(
                    [](HWND hwnd, LPARAM lp) -> BOOL {
                        auto *s = reinterpret_cast<Search *>(lp);
                        DWORD owner = 0;
                        GetWindowThreadProcessId(hwnd, &owner);
                        wchar_t cls[64] = {};
                        GetClassNameW(hwnd, cls, 64);
                        if (owner == s->process && IsWindowVisible(hwnd) && std::wstring(cls) == L"#32770") {
                            s->found = hwnd;
                            return FALSE;
                        }
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&search));
                if (search.found) {
                    wchar_t title[256] = {};
                    GetWindowTextW(search.found, title, 256);
                    seen = title;
                    PostMessageW(search.found, WM_COMMAND, IDCANCEL, 0);
                    return;
                }
                Sleep(50);
            }
        });
        const bool shown = player.showFilterProperties(name, 0, 10, 10);
        stop = true;
        watcher.join();
        std::fwprintf(stderr, L"property frame for %hs: \"%ls\"\n", name.c_str(), seen.c_str());
        QVERIFY(shown);
        QVERIFY(!seen.empty());
        QVERIFY(seen.find(std::wstring(name.begin(), name.end())) != std::wstring::npos);
        QVERIFY(!player.showFilterProperties("No such filter", 0, 0, 0));
    }

    // Legacy's DXVA conversion on the CPU: limited range in, BT.601 unless
    // the script says TV.709.
    void framesConvertWithTheScriptsMatrix()
    {
        using namespace hikari::backends::dshow;
        // One YUY2 pixel pair: Y 81, Cb 90, Cr 240 (BT.601 pure red, 255/0/0).
        const std::uint8_t yuy2[4] = {81, 90, 81, 240};
        IndexedFrame out;
        SampleFormat format{Subtype::YUY2, 2, 1, false};
        QVERIFY(toBgra(format, yuy2, sizeof yuy2, false, out));
        const auto *p = reinterpret_cast<const std::uint8_t *>(out.bgra.data());
        QVERIFY(std::abs(p[2] - 255) <= 1 && p[1] <= 1 && p[0] <= 1);
        QVERIFY(toBgra(format, yuy2, sizeof yuy2, true, out));
        p = reinterpret_cast<const std::uint8_t *>(out.bgra.data());
        QVERIFY(p[2] == 255 && p[1] > 10); // BT.709 reads the same code values differently
        // A short sample is refused; an RGB32 DIB is turned upright.
        QVERIFY(!toBgra(format, yuy2, 3, false, out));
        const std::uint8_t rgb[8] = {1, 2, 3, 0, 4, 5, 6, 0};
        QVERIFY(toBgra(SampleFormat{Subtype::RGB32, 1, 2, true}, rgb, sizeof rgb, false, out));
        p = reinterpret_cast<const std::uint8_t *>(out.bgra.data());
        QCOMPARE(int(p[0]), 4);
        QCOMPARE(int(p[3]), 255);
    }
};

QTEST_MAIN(DirectShowPlayerTests)
#include "directshow_player_tests.moc"
