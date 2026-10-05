// V1 workflow through the real UI (Spix): load the associated video, Play
// through the general player (its frames are shown, with the overlay), then
// Pause: the indexed frame shown is the one whose interval holds the last
// delivered frame (the accepted I4 handoff), read from the barcode each
// fixture frame carries. Stop returns to the first frame.

#include "hikari/app/application.h"
#include "docking.h"
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QDeadlineTimer>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <Spix/QtQmlBot.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace std::chrono_literals;

namespace {

int barcode(const hikari::application::IndexedFrame &f)
{
    const QImage image =
        QImage(reinterpret_cast<const uchar *>(f.bgra.data()), f.width, f.height, f.stride, QImage::Format_RGB32)
            .copy();
    int value = 0;
    for (int by = 0; by < 4; ++by)
        for (int bx = 0; bx < 4; ++bx) {
            const QRgb p = image.pixel((2 * bx + 1) * image.width() / 8, (2 * by + 1) * image.height() / 8);
            if ((qRed(p) + qGreen(p) + qBlue(p)) / 3 > 128)
                value |= 1 << (by * 4 + bx);
        }
    return value;
}

// What the Video panel last presented, recorded on the GUI thread.
struct Shown {
    int frame = -1;
    int barcode = -1;
    bool overlay = false;
    bool accepted = false;
    int generalFrames = 0;   // player frames shown so far
    int lastGeneralBarcode = -1;
};

std::mutex g_mutex;
Shown g_shown;

Shown shown()
{
    std::lock_guard lock(g_mutex);
    return g_shown;
}

class Workflow : public spix::TestServer {
public:
    std::vector<std::string> observed;

protected:
    std::string status() { return getStringProperty("mainWindow/videoStatus", "text"); }
    template <typename Pred> bool waitFor(Pred pred)
    {
        for (int i = 0; i < 150; ++i) {
            if (pred(shown()))
                return true;
            wait(100ms);
            status(); // Spix commands are queued: a synchronous read lets the wait run
        }
        return false;
    }

    void executeTest() override
    {
        if (waitForItem("mainWindow/loadAssociated", 5s)) {
            mouseClick("mainWindow/loadAssociated");
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 0 && s.accepted; }) ? "frame 0" : "no frame 0"); // 0
            mouseClick("mainWindow/playPause");
            std::string label;
            for (int i = 0; i < 50 && label != "Pause"; ++i) {
                wait(100ms);
                label = getStringProperty("mainWindow/playPause", "text");
            }
            observed.push_back(label); // 1: Pause while playing
            observed.push_back(waitFor([](const Shown &s) { return s.generalFrames >= 5; }) ? "playing" : "no frames"); // 2
            mouseClick("mainWindow/playPause");
            // Once paused, late player frames are ignored: the last one is final.
            label.clear();
            for (int i = 0; i < 50 && label != "Play"; ++i) {
                wait(100ms);
                label = getStringProperty("mainWindow/playPause", "text");
            }
            const int expected = shown().lastGeneralBarcode;
            // The paused indexed frame: its index and barcode match the last player frame.
            const bool identity = waitFor([expected](const Shown &s) {
                return s.frame >= 0 && s.accepted && s.frame == expected && s.barcode == expected;
            });
            observed.push_back(identity ? "identity" : "frame " + std::to_string(shown().frame) + " expected " +
                                                           std::to_string(expected)); // 3
            observed.push_back(label); // 4: Play again
            observed.push_back(expected > 0 ? "moved" : "did not move"); // 5
            mouseClick("mainWindow/stopVideo");
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 0 && s.accepted; }) ? "frame 0" : "no frame 0"); // 6
            // V2: L is 5 s forward (past this 2 s video: its last frame), ; 5 s back.
            invokeMethod("mainWindow/videoPanel", "forceActiveFocus", {});
            enterKey("mainWindow/videoPanel", Qt::Key_L, spix::KeyModifiers::None);
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 47 && s.accepted; }) ? "frame 47" : "no frame 47"); // 7
            enterKey("mainWindow/videoPanel", Qt::Key_Semicolon, spix::KeyModifiers::None);
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 0 && s.accepted; }) ? "frame 0" : "no frame 0"); // 8
            observed.push_back(getStringProperty("mainWindow/videoTimes", "text")); // 9
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        quit();
    }
};

constexpr char kSubtitles[] =
    "[Script Info]\r\n"
    "ScriptType: v4.00+\r\n"
    "PlayResX: 320\r\n"
    "PlayResY: 240\r\n"
    "Video File: %1\r\n"
    "\r\n"
    "[V4+ Styles]\r\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\r\n"
    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\r\n"
    "\r\n"
    "[Events]\r\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
    "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,{\\an1\\pos(0,240)\\p1}m 0 0 l 320 0 320 20 0 20{\\p0}\r\n";

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // The application's controls style (composition.cpp chooses it), not
    // the platform's: Qt's native Windows style, which the application never
    // shows, divides by zero painting offscreen.
    hikari::ui::theme::chooseControlsStyle();
    QTemporaryDir dir;
    const QString subtitles = dir.filePath(QStringLiteral("ep1.ass"));
    QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), dir.filePath(QStringLiteral("ep1.mkv")));
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    {
        QFile f(subtitles);
        expect(f.open(QIODevice::WriteOnly) && f.write(QString::fromLatin1(kSubtitles).arg(QStringLiteral("ep1.mkv")).toUtf8()) > 0,
               "write the subtitles");
    }
    hikari::app::Application::Options options;
    options.mediaHelper = QStringLiteral(HIKARI_MEDIA_HELPER);
    options.playbackAudio = false; // never a desktop's speakers in tests
    hikari::app::Application application(options);
    auto &video = application.video();
    QObject::connect(&video, &hikari::ui::VideoController::changed, [&video] {
        const auto &session = video.session();
        std::lock_guard lock(g_mutex);
        if (const auto frame = session.lastFrame()) {
            if (frame->index < 0 && frame.get() != nullptr) {
                ++g_shown.generalFrames;
                g_shown.lastGeneralBarcode = barcode(*frame);
            }
            g_shown.frame = frame->index;
            g_shown.barcode = barcode(*frame);
        }
        g_shown.overlay = session.lastOverlay() && !session.lastOverlay()->empty;
        g_shown.accepted = session.lastPresent() &&
                           session.lastPresent()->outcome == hikari::application::PresentOutcome::Accepted;
    });
    expect(application.openFile(subtitles), "open the subtitles");
    QQmlApplicationEngine engine;
    hikari::ui::attachDocking(engine);
    engine.setInitialProperties(application.qmlProperties());
    engine.loadFromModule("Hikari.Ui", "Main");
    if (engine.rootObjects().isEmpty())
        return 2;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->requestActivate();
    QDeadlineTimer deadline(10'000);
    while (!(window->isExposed() && window->isActive()) && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    Workflow test;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();
    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 10, "every step observed");
    if (o.size() == 10) {
        expect(o[0] == "frame 0", "the first frame is shown");
        expect(o[1] == "Pause", "Play starts playback");
        expect(o[2] == "playing", "the player's frames are shown");
        expect(o[3] == "identity", "Pause shows the exact indexed frame of the last player frame");
        expect(o[4] == "Play", "paused");
        expect(o[5] == "moved", "playback advanced");
        expect(o[6] == "frame 0", "Stop returns to the first frame");
        expect(o[7] == "frame 47", "L seeks 5 s forward, clamped to the last frame");
        expect(o[8] == "frame 0", "; seeks 5 s back, clamped to the first frame");
        expect(o[9] == "00:00:00,000;  0;  0;  0 ms, -2000 ms", "the times field");
    }
    return failures == 0 ? 0 : 1;
}
