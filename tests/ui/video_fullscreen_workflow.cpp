#include "hikari/app/application.h"
#include "docking.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QDeadlineTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <Spix/QtQmlBot.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace std::chrono_literals;

namespace {

struct Seen {
    bool active = false;
    int monitor = -1;
    QString screen;
    qreal dpr = 0;
    QRect fullscreenGeometry;
    QRect screenGeometry;
    QRect mainGeometry;
    int mainVisibility = 0;
    bool presenterInFullscreen = false;
    bool videoDockOpen = false;
    bool focusInFullscreen = false;
    QString focusItem;
};

std::mutex g_mutex;
std::vector<Seen> g_seen;

std::vector<Seen> seen()
{
    std::lock_guard lock(g_mutex);
    return g_seen;
}

class Workflow : public spix::TestServer {
public:
    int monitors = 1;
    std::vector<std::string> observed;
    void note(std::string what)
    {
        std::printf("step %zu: %s\n", observed.size(), what.c_str());
        observed.push_back(std::move(what));
    }
    std::atomic<bool> finished{false};

protected:
    std::string status() { return getStringProperty("mainWindow/videoStatus", "text"); }
    // A synchronous Spix read, so the queued commands run first.
    void sync() { getStringProperty("mainWindow", "title"); }
    bool waitFor(const std::function<bool()> &done, int tenths = 100)
    {
        for (int i = 0; i < tenths; ++i) {
            if (done())
                return true;
            wait(100ms);
            sync();
        }
        return done();
    }
    std::size_t changes() { return seen().size(); }

    void executeTest() override
    {
        note(waitFor([&] { return status().rfind("Frame", 0) == 0; }, 200) ? "indexed" : "not indexed");
        invokeMethod("mainWindow/videoPanel", "forceActiveFocus", {});
        enterKey("mainWindow/videoPanel", Qt::Key_F, spix::KeyModifiers::None);
        note(waitFor([&] { return changes() >= 1; }) ? "entered" : "not entered");
        wait(500ms);
        note(existsAndVisible("videoFullscreen/videoFullscreenPanel") ? "panel" : "no panel");
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("videoFullscreen", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/video-fullscreen.png");
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Space, spix::KeyModifiers::None);
        note(waitFor([&] { return getStringProperty("videoFullscreen/fullscreenPlayPause", "text") == "Pause"; })
                               ? "playing" : "not playing");
        wait(300ms);
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Space, spix::KeyModifiers::None);
        note(waitFor([&] { return getStringProperty("videoFullscreen/fullscreenPlayPause", "text") == "Play"; })
                               ? "paused" : "not paused");
        const std::string before = getStringProperty("videoFullscreen/fullscreenTimes", "text");
        wait(200ms);
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Right, spix::KeyModifiers::None);
        note(waitFor([&] { return getStringProperty("videoFullscreen/fullscreenTimes", "text") != before; })
                               ? "stepped" : "not stepped");
        const auto mode = [&] { return getStringProperty("videoFullscreen/fullscreenBarMode", "text"); };
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Tab, spix::KeyModifiers::None);
        note(waitFor([&] { return mode() == "Always"; }) ? "always" : "not always: " + mode());
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Tab, spix::KeyModifiers::None);
        note(waitFor([&] { return mode() == "Never" && !existsAndVisible("videoFullscreen/videoFullscreenPanel"); })
                 ? "never" : "not never: " + mode());
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Tab, spix::KeyModifiers::None);
        note(waitFor([&] { return mode() == "Auto"; }) ? "auto" : "not auto: " + mode());
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Space, spix::KeyModifiers::None);
        const std::string blank = std::to_string(int(Qt::BlankCursor));
        note(waitFor([&] {
            return !existsAndVisible("videoFullscreen/videoFullscreenPanel")
                   && getStringProperty("videoFullscreen/visualOverlay", "cursorOverride") == blank;
        }, 15) ? "hidden"
               : "not hidden: panel " + std::to_string(existsAndVisible("videoFullscreen/videoFullscreenPanel"))
                     + ", cursor " + getStringProperty("videoFullscreen/visualOverlay", "cursorOverride") + ", "
                     + getStringProperty("videoFullscreen/fullscreenPlayPause", "text"));
        takeScreenshot("videoFullscreen", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/video-fullscreen-idle.png");
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Space, spix::KeyModifiers::None);
        note(waitFor([&] { return existsAndVisible("videoFullscreen/videoFullscreenPanel"); }) ? "paused, shown"
                                                                                               : "paused, not shown");
        enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Escape, spix::KeyModifiers::None);
        note(waitFor([&] { return changes() >= 2; }) ? "left" : "not left");
        wait(500ms);
        if (monitors > 1) {
            mouseClick("mainWindow/visualOverlay", spix::MouseButtons::Right);
            note(waitForItem("mainWindow/videoMenuMonitor1", 3s)
                                   ? getStringProperty("mainWindow/videoMenuMonitor1", "text") : "no monitor entry");
            invokeMethod("mainWindow/videoMenuMonitor1", "triggered", {});
            note(waitFor([&] { return changes() >= 3; }) ? "on monitor 2" : "not on monitor 2");
            wait(800ms);
            takeScreenshot("videoFullscreen", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/video-fullscreen-monitor2.png");
            mouseClick("videoFullscreen/visualOverlay", spix::MouseButtons::Right);
            note(waitForItem("videoFullscreen/fullscreenMenuMonitor1", 3s)
                                   ? getStringProperty("videoFullscreen/fullscreenMenuMonitor1", "text")
                                   : "no monitor entry");
            enterKey("videoFullscreen/fullscreenMenuMonitor1", Qt::Key_Escape, spix::KeyModifiers::None);
            wait(300ms);
            enterKey("videoFullscreen/videoFullscreenKeys", Qt::Key_Escape, spix::KeyModifiers::None);
            note(waitFor([&] { return changes() >= 4; }) ? "left monitor 2" : "not left");
            wait(500ms);
        }
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/video-fullscreen-after.png");
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        finished = true;
        quit();
    }
};

}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QGuiApplication app(argc, argv);
    int monitors = 1;
    bool primarySecond = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--monitors") == 0 && i + 1 < argc)
            monitors = std::atoi(argv[i + 1]);
        if (std::strcmp(argv[i], "--primary-second") == 0)
            primarySecond = true;
    }
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    const auto systemScreens = QGuiApplication::screens();
    if (primarySecond) {
        expect(systemScreens.size() >= 2, "two screens for --primary-second");
        if (systemScreens.size() >= 2)
            QWindowSystemInterface::handlePrimaryScreenChanged(systemScreens[1]->handle());
        expect(QGuiApplication::primaryScreen() == systemScreens.value(1), "the second screen is the primary");
    }
    {
        const auto ordered = hikari::ui::VideoFullscreenController::monitors();
        QList<QScreen *> expected{QGuiApplication::primaryScreen()};
        for (QScreen *s : systemScreens)
            if (s != QGuiApplication::primaryScreen())
                expected.push_back(s);
        expect(ordered == expected, "the monitors: the primary first, then the other screen");
        for (QScreen *s : ordered)
            std::printf("monitor %s%s\n", qPrintable(s->name()), s == QGuiApplication::primaryScreen() ? " primary" : "");
    }
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens)
        std::printf("screen %s %dx%d+%d+%d dpr %.2f%s\n", qPrintable(s->name()), s->geometry().width(),
                    s->geometry().height(), s->geometry().x(), s->geometry().y(), s->devicePixelRatio(),
                    s == QGuiApplication::primaryScreen() ? " primary" : "");
    expect(screens.size() >= monitors, "the monitors asked for are there");

    QTemporaryDir dir;
    QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), dir.filePath(QStringLiteral("ep1.mkv")));
    const QString subtitles = dir.filePath(QStringLiteral("ep1.ass"));
    {
        QFile f(subtitles);
        expect(f.open(QIODevice::WriteOnly), "write the subtitles");
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 240\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\n");
    }
    hikari::app::Application::Options options;
    options.mediaHelper = QStringLiteral(HIKARI_MEDIA_HELPER);
    options.playbackAudio = false;
    hikari::app::Application application(options);
    expect(application.openFile(subtitles), "open the subtitles");
    application.video().openVideo(dir.filePath(QStringLiteral("ep1.mkv")));
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
    expect(window->isExposed(), "the window is exposed");
    const QRect mainGeometry = window->geometry();
    const int mainVisibility = window->visibility();

    auto &fs = application.videoFullscreen();
    fs.setIdleInterval(400);
    // Found while docked: a closed dock's content leaves the window's object tree.
    QPointer<QQuickItem> presenter = window->findChild<QQuickItem *>(QStringLiteral("videoPresenter"));
    expect(presenter != nullptr, "the presenter is there");
    // The window manager's placement, the keyboard and the screen's scale come a moment later (Wayland).
    auto record = [&](bool active) {
        Seen s;
        s.active = active;
        s.monitor = fs.monitor();
        QQuickWindow *w = fs.window();
        if (w && w->screen()) {
            s.screen = w->screen()->name();
            s.dpr = w->devicePixelRatio();
            s.screenGeometry = w->screen()->geometry();
        }
        s.fullscreenGeometry = w ? w->geometry() : QRect();
        s.mainGeometry = window->geometry();
        s.mainVisibility = window->visibility();
        s.presenterInFullscreen = presenter && presenter->window() == w;
        auto *dock = window->findChild<QObject *>(QStringLiteral("videoDock"));
        s.videoDockOpen = dock && dock->property("isOpen").toBool();
        s.focusInFullscreen = QGuiApplication::focusWindow() == w;
        if (auto *focus = window->activeFocusItem())
            s.focusItem = focus->objectName();
        std::lock_guard lock(g_mutex);
        g_seen.push_back(s);
    };
    auto settled = [&](bool active) {
        QQuickWindow *w = fs.window();
        if (active)
            return QGuiApplication::focusWindow() == w && w->screen()
                   && w->devicePixelRatio() == w->screen()->devicePixelRatio();
        return QGuiApplication::focusWindow() == window && window->activeFocusItem() != nullptr;
    };
    QObject::connect(&fs, &hikari::ui::VideoFullscreenController::activeChanged, [&] {
        const bool active = fs.active();
        auto *poll = new QTimer(&app);
        poll->setInterval(100);
        QObject::connect(poll, &QTimer::timeout, [&, poll, active, ticks = 0]() mutable {
            if ((++ticks >= 4 && settled(active)) || ticks >= 40) {
                poll->stop();
                poll->deleteLater();
                record(active);
            }
        });
        poll->start();
    });

    Workflow test;
    test.monitors = monitors;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();

    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    const auto changes = seen();
    for (const auto &s : changes)
        std::printf("change: active %d monitor %d screen %s dpr %.2f window %dx%d+%d+%d screen %dx%d+%d+%d main "
                    "%dx%d+%d+%d visibility %d presenter %s dock %s focus window %s focus item %s\n",
                    s.active, s.monitor, qPrintable(s.screen), s.dpr, s.fullscreenGeometry.width(),
                    s.fullscreenGeometry.height(), s.fullscreenGeometry.x(), s.fullscreenGeometry.y(),
                    s.screenGeometry.width(), s.screenGeometry.height(), s.screenGeometry.x(), s.screenGeometry.y(),
                    s.mainGeometry.width(), s.mainGeometry.height(), s.mainGeometry.x(), s.mainGeometry.y(),
                    s.mainVisibility, s.presenterInFullscreen ? "fullscreen" : "panel", s.videoDockOpen ? "open" : "closed",
                    s.focusInFullscreen ? "fullscreen" : "other", qPrintable(s.focusItem));
    {
        const bool wayland = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
        QQuickWindow *w = fs.window();
        expect(w && w->transientParent() == (wayland ? nullptr : static_cast<QWindow *>(window)),
               wayland ? "the fullscreen window has no parent (Wayland)"
                       : "the fullscreen window is owned by the main window");
    }
    const std::size_t steps = monitors > 1 ? 16 : 12;
    expect(o.size() == steps, "every step observed");
    if (o.size() == steps) {
        expect(o[0] == "indexed", "the video is indexed");
        expect(o[1] == "entered", "F shows the video fullscreen");
        expect(o[2] == "panel", "the bar is shown on entering (Auto)");
        expect(o[3] == "playing" && o[4] == "paused", "Space plays and pauses in fullscreen");
        expect(o[5] == "stepped", "Right steps a frame in fullscreen");
        expect(o[6] == "always" && o[7] == "never" && o[8] == "auto", "Tab cycles Always, Never and Auto");
        expect(o[9] == "hidden", "playing, the bar and the pointer hide when idle");
        expect(o[10] == "paused, shown", "paused, the bar is back");
        expect(o[11] == "left", "Esc leaves");
        if (monitors > 1) {
            expect(o[12] == "Open in full screen on monitor 2", "the second monitor is offered");
            expect(o[13] == "on monitor 2", "fullscreen on the second monitor");
            expect(o[14] == "Switch full screen to monitor 2", "in fullscreen the entry switches the monitor");
            expect(o[15] == "left monitor 2", "Esc leaves the second monitor");
        }
    }
    expect(changes.size() == (monitors > 1 ? 4u : 2u), "each change seen");
    if (changes.size() >= 2) {
        const auto &in = changes[0], &out = changes[1];
        expect(in.active && window->screen() && in.screen == window->screen()->name(),
               "fullscreen on the main window's monitor");
        expect(in.fullscreenGeometry == in.screenGeometry, "the window covers its monitor");
        expect(in.presenterInFullscreen, "the picture is in the fullscreen window");
        expect(in.focusInFullscreen, "the fullscreen window has the keyboard");
        expect(in.mainGeometry == mainGeometry && in.mainVisibility == mainVisibility,
               "the main window is untouched meanwhile");
        expect(in.videoDockOpen, "the docked video stays (monitor 0)");
        expect(!out.active && !out.presenterInFullscreen, "the picture is back in the panel");
        expect(out.mainGeometry == mainGeometry && out.mainVisibility == mainVisibility,
               "the main window's geometry and state are as before");
        expect(!out.focusInFullscreen && out.focusItem == QStringLiteral("videoPanel"),
               "the keyboard is back on the Video panel");
    }
    if (monitors > 1 && changes.size() == 4) {
        const auto &in = changes[2], &out = changes[3];
        const auto ordered = hikari::ui::VideoFullscreenController::monitors();
        const QScreen *second = ordered.size() > 1 ? ordered[1] : nullptr;
        if (primarySecond)
            expect(second == systemScreens.value(0), "monitor 2 is the screen the system lists first");
        expect(in.active && in.monitor == 1, "the context menu's monitor 2");
        expect(second && in.screen == second->name(), "on the second monitor");
        expect(second && in.fullscreenGeometry == second->geometry(), "covering the second monitor");
        expect(second && in.dpr == second->devicePixelRatio(), "at the second monitor's scale");
        expect(in.presenterInFullscreen, "the picture is in the fullscreen window");
        expect(in.focusInFullscreen, "the fullscreen window has the keyboard");
        expect(!in.videoDockOpen, "the docked video is hidden meanwhile (m_IsOnAnotherMonitor)");
        expect(!out.active && out.videoDockOpen && !out.presenterInFullscreen, "the docked video is back after leaving");
        expect(!out.focusInFullscreen && out.focusItem == QStringLiteral("videoPanel"),
               "the keyboard is back on the Video panel");
        expect(out.mainGeometry == mainGeometry && out.mainVisibility == mainVisibility,
               "the main window's geometry and state are as before");
    }
    return failures == 0 ? 0 : 1;
}
