// V1 workflow through the real UI (Spix): load the associated video, Play
// through the general player (its frames are shown, with the overlay), then
// Pause: the indexed frame shown is the one whose interval holds the last
// delivered frame (the accepted I4 handoff), read from the barcode each
// fixture frame carries. Stop returns to the first frame. While playing
// there is one change notification per frame shown, and the texts update at
// about 10 Hz (V1); the status, the times and the seek bar follow the
// player's frame, and a click on the seek bar seeks the player, which goes
// on from there without ever stepping back (the bar's following never
// seeks). The GUI thread's CPU time while playing is reported.

#include "hikari/app/application.h"
#include "docking.h"
#include "status_bar_controller.h"
#include "visual_tools_controller.h"

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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

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

// The GUI thread's CPU time, read from the test server's thread.
class GuiThreadClock {
public:
    GuiThreadClock() // on the GUI thread
    {
#ifdef _WIN32
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &m_thread,
                        THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
#else
        m_ok = pthread_getcpuclockid(pthread_self(), &m_clock) == 0;
#endif
    }
    ~GuiThreadClock()
    {
#ifdef _WIN32
        if (m_thread)
            CloseHandle(m_thread);
#endif
    }
    GuiThreadClock(const GuiThreadClock &) = delete;
    GuiThreadClock &operator=(const GuiThreadClock &) = delete;
    double ms() const
    {
#ifdef _WIN32
        FILETIME created, exited, kernel, user;
        if (!m_thread || !GetThreadTimes(m_thread, &created, &exited, &kernel, &user))
            return -1;
        auto ticks = [](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        return static_cast<double>(ticks(kernel) + ticks(user)) / 10'000; // 100 ns units
#else
        timespec t{};
        if (!m_ok || clock_gettime(m_clock, &t) != 0)
            return -1;
        return static_cast<double>(t.tv_sec) * 1000 + static_cast<double>(t.tv_nsec) / 1e6;
#endif
    }

private:
#ifdef _WIN32
    HANDLE m_thread = nullptr;
#else
    clockid_t m_clock{};
    bool m_ok = false;
#endif
};

GuiThreadClock *g_guiClock = nullptr;

// What the Video panel last presented, recorded on the GUI thread.
struct Shown {
    int frame = -1;
    int barcode = -1;
    bool overlay = false;
    bool accepted = false;
    int generalFrames = 0;   // player frames shown so far
    int lastGeneralBarcode = -1;
    int changes = 0; // V1: the panel's change notifications
    int texts = 0;   // and its texts' (times, status)
    int tools = 0;   // the visual tools' change notifications
    int statusBar = 0; // the status bar's
    std::vector<int> generalBarcodes; // each player frame shown, in order
    // Held, so a later frame can never reuse its address and pass as it.
    std::shared_ptr<const hikari::application::IndexedFrame> lastGeneral;
};

std::mutex g_mutex;
Shown g_shown;

Shown shown()
{
    std::lock_guard lock(g_mutex);
    return g_shown;
}

// No player frame shows an earlier frame than the one before it.
bool forward(const std::vector<int> &barcodes, std::size_t from = 0)
{
    for (std::size_t i = from + 1; i < barcodes.size(); ++i)
        if (barcodes[i] < barcodes[i - 1])
            return false;
    return true;
}

class Workflow : public spix::TestServer {
public:
    std::vector<std::string> observed;
    bool rate = false;
    double guiCpuMs = -1, playingMs = 0;
    std::string follows; // the panel's frame while playing
    std::string seekBar;  // a click on the seek bar while playing

protected:
    std::string status() { return getStringProperty("mainWindow/videoStatus", "text"); }
    int sliderFrame()
    {
        return static_cast<int>(std::lround(std::atof(getStringProperty("mainWindow/videoSlider", "value").c_str())));
    }
    // The status's, the times field's and the seek bar's frames; -1 when unreadable.
    std::vector<int> panelFrames()
    {
        int label = -1, of = 0, times = -1;
        std::sscanf(status().c_str(), "Frame %d of %d", &label, &of);
        std::sscanf(getStringProperty("mainWindow/videoTimes", "text").c_str(), "%*[^;];  %d", &times);
        return {label, times, sliderFrame()};
    }
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
            const Shown before = shown();
            const auto from = std::chrono::steady_clock::now();
            const double cpuFrom = g_guiClock->ms();
            observed.push_back(waitFor([](const Shown &s) { return s.generalFrames >= 5; }) ? "playing" : "no frames"); // 2
            wait(500ms);
            status();
            const Shown after = shown();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
            const double cpuTo = g_guiClock->ms();
            if (cpuFrom >= 0 && cpuTo >= 0) {
                guiCpuMs = cpuTo - cpuFrom;
                playingMs = ms;
            }
            // V1: one notification per frame shown, the texts at about 10 Hz.
            const int frames = after.generalFrames - before.generalFrames, changes = after.changes - before.changes,
                      texts = after.texts - before.texts, tools = after.tools - before.tools,
                      bar = after.statusBar - before.statusBar;
            std::printf("while playing: %d frames, %d notifications, %d text updates, %d visual tools and %d status "
                        "bar notifications in %.0f ms\n",
                        frames, changes, texts, tools, bar, ms);
            // The visual tools and the status bar notify only when what they
            // show changes (the shown frame's warning, the fields), not per frame.
            rate = frames > 0 && changes <= frames + 1 && texts >= 2 && texts <= static_cast<int>(ms / 100) + 2 &&
                   tools <= 1 && bar <= texts;
            // The status, the times and the seek bar show the player's frame
            // (legacy VideoBox::RefreshTime), at most 100 ms behind it. Each
            // read lies between the player frames shown just before and just
            // after it, so the reads' own latency does not count; the bound is
            // 250 ms of this 24 fps video: the 100 ms pace, a frame's own
            // duration and timer slack (Windows' clock steps 15.6 ms).
            {
                static constexpr int kLagFrames = 6;
                struct Read {
                    std::vector<int> panel;
                    int from = -1, to = -1; // the player's frame around the read
                };
                auto read = [&] {
                    Read r;
                    r.from = shown().lastGeneralBarcode;
                    r.panel = panelFrames();
                    r.to = shown().lastGeneralBarcode;
                    return r;
                };
                auto inStep = [](const Read &r) {
                    for (const int v : r.panel)
                        if (v <= 0 || v > r.to || v < r.from - kLagFrames)
                            return false;
                    return true;
                };
                const Read first = read();
                // Far enough on that the bar must have moved, whatever its lag.
                const int target = first.to + kLagFrames + 2;
                waitFor([target](const Shown &s) { return s.lastGeneralBarcode >= target; });
                const Read second = read();
                const bool ok = inStep(first) && inStep(second) && second.panel[2] > first.panel[2];
                char text[200];
                std::snprintf(text, sizeof text,
                              "status %d, times %d, bar %d at player %d-%d; then %d, %d, %d at %d-%d",
                              first.panel[0], first.panel[1], first.panel[2], first.from, first.to,
                              second.panel[0], second.panel[1], second.panel[2], second.from, second.to);
                follows = ok ? "follows" : text;
            }
            // A click at 90% of the seek bar while playing: the player jumps
            // there (about frame 42; playing on it would come a frame or two
            // at a time) and goes on from it, never stepping back.
            {
                const std::size_t clickedAt = shown().generalBarcodes.size();
                mouseClick("mainWindow/videoSlider", spix::Point(0.9, 0.5));
                auto jumpIn = [clickedAt](const std::vector<int> &b) {
                    for (std::size_t i = std::max<std::size_t>(clickedAt, 1); i < b.size(); ++i)
                        if (b[i] >= 38 && b[i] - b[i - 1] >= 6)
                            return i;
                    return std::size_t{0};
                };
                const bool jumped = waitFor([&](const Shown &s) { return jumpIn(s.generalBarcodes) != 0; });
                const std::string playing = getStringProperty("mainWindow/playPause", "text");
                wait(200ms);
                const int bar = sliderFrame();
                const Shown s = shown();
                // Forward up to the click, and from the jump on.
                const std::vector<int> beforeClick(s.generalBarcodes.begin(),
                                                   s.generalBarcodes.begin() + static_cast<std::ptrdiff_t>(clickedAt));
                const bool steady = forward(beforeClick) && forward(s.generalBarcodes, jumpIn(s.generalBarcodes));
                seekBar = !jumped ? "no jump" : playing != "Pause" ? "paused" : bar < 38 ? "bar at " + std::to_string(bar)
                          : !steady ? "stepped back" : "sought";
            }
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
    QObject::connect(&video, &hikari::ui::VideoController::textsChanged, [] {
        std::lock_guard lock(g_mutex);
        ++g_shown.texts;
    });
    QObject::connect(&application.visualTools(), &hikari::ui::VisualToolsController::changed, [] {
        std::lock_guard lock(g_mutex);
        ++g_shown.tools;
    });
    QObject::connect(&application.statusBar(), &hikari::ui::StatusBarController::changed, [] {
        std::lock_guard lock(g_mutex);
        ++g_shown.statusBar;
    });
    QObject::connect(&video, &hikari::ui::VideoController::changed, [&video] {
        const auto &session = video.session();
        std::lock_guard lock(g_mutex);
        ++g_shown.changes;
        if (const auto frame = session.lastFrame()) {
            if (frame->index < 0 && frame.get() != nullptr) {
                ++g_shown.generalFrames;
                g_shown.lastGeneralBarcode = barcode(*frame);
                if (frame != g_shown.lastGeneral) {
                    g_shown.lastGeneral = frame;
                    g_shown.generalBarcodes.push_back(g_shown.lastGeneralBarcode);
                }
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
    GuiThreadClock guiClock;
    g_guiClock = &guiClock;
    Workflow test;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();
    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    if (test.guiCpuMs >= 0)
        std::printf("GUI thread CPU while playing: %.0f ms of %.0f ms (%.1f%%)\n", test.guiCpuMs, test.playingMs,
                    100 * test.guiCpuMs / test.playingMs);
    std::printf("panel while playing: %s; seek bar click: %s\n", test.follows.c_str(), test.seekBar.c_str());
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
        expect(test.rate, "one notification per frame, the texts at about 10 Hz and the tools and status bar only "
                          "on a change while playing");
        expect(test.follows == "follows", "the status, the times and the seek bar follow the player's frame");
        expect(test.seekBar == "sought", "a click on the seek bar while playing seeks, and playback goes on forward");
    }
    return failures == 0 ? 0 : 1;
}
