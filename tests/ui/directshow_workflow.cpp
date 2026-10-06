// W1 workflow through the real UI (Spix, Windows): with DirectShow chosen in
// the settings (video.playbackPlayer), load the associated AVI video, Play
// through the DirectShow adapter (its frames are shown, with the overlay),
// Pause (the indexed frame whose interval holds the last DirectShow frame,
// the accepted I4 handoff, read from each fixture frame's barcode), step to
// the next frame and back, list the graph in the video's context menu
// ("Filters", legacy VideoBox::ContextMenu) and Stop back to the first frame.
// Then the transport's Next / Previous frame buttons step, and the Filters
// submenu, open beside the context menu, takes the Dark and Light themes'
// colours (saved for review when HIKARI_SCREENSHOT_DIR is set).

#include "hikari/app/application.h"
#include "docking.h"
#include "theme.h"

#include <QColor>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <Spix/QtQmlBot.h>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
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
    bool accepted = false;
    int playerFrames = 0; // DirectShow frames shown so far
    int lastPlayerBarcode = -1;
    bool directShow = false;
    QStringList filters; // the context menu's Filters entries
};

std::mutex g_mutex;
Shown g_shown;
hikari::app::Application *g_app = nullptr;
QQmlApplicationEngine *g_engine = nullptr;

Shown shown()
{
    std::lock_guard lock(g_mutex);
    return g_shown;
}

// The context menu's Filters submenu, read on the GUI thread.
QStringList filterEntries()
{
    QStringList out;
    QMetaObject::invokeMethod(
        g_app,
        [&] {
            auto *root = g_engine->rootObjects().first();
            auto *menu = root->findChild<QObject *>(QStringLiteral("videoMenuFilters"));
            if (!menu)
                return;
            const int count = menu->property("count").toInt();
            for (int i = 0; i < count; ++i) {
                QQuickItem *item = nullptr;
                QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
                if (item)
                    out << item->property("text").toString() +
                               (item->property("enabled").toBool() ? QString() : QStringLiteral(" (disabled)"));
            }
        },
        Qt::BlockingQueuedConnection);
    return out;
}

QObject *rootChild(const char *name)
{
    return g_engine->rootObjects().first()->findChild<QObject *>(QString::fromLatin1(name));
}

std::vector<QQuickItem *> menuItems(QObject *menu)
{
    std::vector<QQuickItem *> out;
    const int count = menu->property("count").toInt();
    for (int i = 0; i < count; ++i) {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
        if (item)
            out.push_back(item);
    }
    return out;
}

// Opens the context menu's Filters submenu as choosing its item does.
void openFilters()
{
    QMetaObject::invokeMethod(
        g_app,
        [] {
            auto *menu = rootChild("videoContextMenu");
            auto *filters = rootChild("videoMenuFilters");
            if (!menu || !filters)
                return;
            for (auto *item : menuItems(menu))
                if (item->property("subMenu").value<QObject *>() == filters)
                    QMetaObject::invokeMethod(item, "click");
        },
        Qt::BlockingQueuedConnection);
}

void setTheme(const char *code)
{
    QMetaObject::invokeMethod(
        g_app,
        [code] {
            g_app->settingsStore()->setValue(QString::fromLatin1(hikari::ui::theme::kFollowSystemSetting), false);
            g_app->settingsStore()->setValue(QString::fromLatin1(hikari::ui::theme::kThemeSetting),
                                             QString::fromLatin1(code));
        },
        Qt::BlockingQueuedConnection);
}

// With the Filters submenu open in the theme `code`: both menus' surface
// is the theme's (K2: Menu draws on palette.base, the field role), on the
// theme's side.
std::string filtersLook(const char *code, bool dark)
{
    std::string out;
    QMetaObject::invokeMethod(
        g_app,
        [&] {
            auto *menu = rootChild("videoContextMenu");
            auto *filters = rootChild("videoMenuFilters");
            if (!menu || !filters || !filters->property("opened").toBool()) {
                out = "filters not open";
                return;
            }
            const QColor expected = hikari::ui::theme::current().roles.field;
            auto *menuBack = menu->property("background").value<QQuickItem *>();
            auto *filtersBack = filters->property("background").value<QQuickItem *>();
            const QColor menuColour = menuBack ? menuBack->property("color").value<QColor>() : QColor();
            const QColor filtersColour = filtersBack ? filtersBack->property("color").value<QColor>() : QColor();
            // Labels cut off (elided): printed only, since the offscreen
            // platform here has no fonts of its own (the shell test
            // videoFiltersSubmenuShowsWholeNames measures them).
            const auto cutOff = [](QObject *m) {
                QStringList out;
                for (auto *item : menuItems(m)) {
                    auto *label = item->property("contentItem").value<QQuickItem *>();
                    if (label && label->implicitWidth() > label->width() + 0.5)
                        out << item->property("text").toString().replace(QLatin1Char('\t'), QLatin1Char(' '));
                }
                return out.isEmpty() ? QStringLiteral("none") : out.join(QStringLiteral(" | "));
            };
            std::printf("%s: context menu %s, Filters %s, theme field %s; labels cut off: Filters %s; context menu %s\n",
                        code, qPrintable(menuColour.name()), qPrintable(filtersColour.name()),
                        qPrintable(expected.name()), qPrintable(cutOff(filters)), qPrintable(cutOff(menu)));
            const QString dir = qEnvironmentVariable("HIKARI_SCREENSHOT_DIR");
            if (!dir.isEmpty() && filtersBack && filtersBack->window()) {
                QDir().mkpath(dir);
                filtersBack->window()->grabWindow().save(
                    QDir(dir).filePath(QStringLiteral("filters-menu-avi-%1.png").arg(QString::fromLatin1(code))));
            }
            if (filtersColour != expected || menuColour != expected)
                out = "not the theme's surface: " + filtersColour.name().toStdString();
            else if ((expected.lightness() < 128) != dark)
                out = "not on the theme's side";
            else
                out = code;
        },
        Qt::BlockingQueuedConnection);
    return out;
}

void closeContextMenu()
{
    QMetaObject::invokeMethod(
        g_app,
        [] {
            if (auto *menu = rootChild("videoContextMenu"))
                QMetaObject::invokeMethod(menu, "close");
        },
        Qt::BlockingQueuedConnection);
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
    std::string waitForLabel(const std::string &want)
    {
        std::string label;
        for (int i = 0; i < 50 && label != want; ++i) {
            wait(100ms);
            label = getStringProperty("mainWindow/playPause", "text");
        }
        return label;
    }

    void executeTest() override
    {
        if (waitForItem("mainWindow/loadAssociated", 5s)) {
            mouseClick("mainWindow/loadAssociated");
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 0 && s.accepted; }) ? "frame 0" : "no frame 0"); // 0
            observed.push_back(shown().directShow ? "directshow" : "general player"); // 1
            // The graph is built as the video loads (legacy), so the menu lists it.
            mouseClick("mainWindow/visualOverlay", spix::MouseButtons::Right);
            QStringList entries;
            for (int i = 0; i < 50 && entries.isEmpty(); ++i) {
                wait(100ms);
                entries = filterEntries();
            }
            {
                std::lock_guard lock(g_mutex);
                g_shown.filters = entries;
            }
            // Legacy's own filters among the system's splitter and decoder,
            // each disabled without property pages (EnumFilters).
            observed.push_back(entries.contains(QStringLiteral("HikariSub video Renderer (disabled)")) &&
                                       entries.contains(QStringLiteral("Source Filter (disabled)")) &&
                                       entries.size() >= 4
                                   ? "filters"
                                   : "no filters"); // 2
            closeContextMenu();
            wait(200ms);
            mouseClick("mainWindow/playPause");
            observed.push_back(waitForLabel("Pause")); // 3: Pause while playing
            observed.push_back(waitFor([](const Shown &s) { return s.playerFrames >= 5; }) ? "playing" : "no frames"); // 4
            mouseClick("mainWindow/playPause");
            const std::string label = waitForLabel("Play");
            const int expected = shown().lastPlayerBarcode;
            const bool identity = waitFor([expected](const Shown &s) {
                return s.frame >= 0 && s.accepted && s.frame == expected && s.barcode == expected;
            });
            observed.push_back(identity ? "identity" : "frame " + std::to_string(shown().frame) + " expected " +
                                                           std::to_string(expected)); // 5
            observed.push_back(label); // 6: Play again
            observed.push_back(expected > 0 ? "moved" : "did not move"); // 7
            // Stepping from the paused frame: the next frame, then back.
            // Stepping by the legacy hotkeys (GLOBAL_NEXT_FRAME Right,
            // GLOBAL_PREVIOUS_FRAME Left): at this window size the Video
            // panel's toolbar is wider than the panel, and its Next frame
            // button lies under the Line editor panel, where a click lands.
            enterKey("mainWindow/editingGrid", Qt::Key_Right, spix::KeyModifiers::None);
            observed.push_back(waitFor([expected](const Shown &s) {
                                   return s.frame == expected + 1 && s.barcode == expected + 1 && s.accepted;
                               }) ? "next"
                                  : "no next frame: " + std::to_string(shown().frame) + " expected " +
                                        std::to_string(expected + 1)); // 8
            enterKey("mainWindow/editingGrid", Qt::Key_Left, spix::KeyModifiers::None);
            observed.push_back(waitFor([expected](const Shown &s) {
                                   return s.frame == expected && s.barcode == expected && s.accepted;
                               }) ? "previous"
                                  : "no previous frame: " + std::to_string(shown().frame)); // 9
            // Playing again goes on from the stepped frame through DirectShow.
            const int before = shown().playerFrames;
            mouseClick("mainWindow/playPause");
            observed.push_back(waitFor([before](const Shown &s) { return s.playerFrames >= before + 3; })
                                   ? "playing again"
                                   : "no frames again"); // 10
            mouseClick("mainWindow/stopVideo");
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 0 && s.accepted; }) ? "frame 0" : "no frame 0"); // 11
            // The transport's Next / Previous frame buttons (legacy
            // VideoBox's frame buttons), pressed as a click does: at this
            // window size the row is wider than the Video panel and the Next
            // frame button lies under the Line editor panel (a layout outside
            // this card), so its own action is invoked.
            invokeMethod("mainWindow/nextFrame", "click", {});
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 1 && s.barcode == 1 && s.accepted; })
                                   ? "next button"
                                   : "no next frame: " + std::to_string(shown().frame)); // 12
            invokeMethod("mainWindow/previousFrame", "click", {});
            observed.push_back(waitFor([](const Shown &s) { return s.frame == 0 && s.barcode == 0 && s.accepted; })
                                   ? "previous button"
                                   : "no previous frame: " + std::to_string(shown().frame)); // 13
            // The Filters submenu in the Dark and Light themes, each chosen
            // before the menu opens (the Options dialog is modal: no menu
            // stays open while the theme changes).
            for (const auto &[code, dark] : {std::pair{"dark", true}, std::pair{"light", false}}) {
                setTheme(code);
                wait(300ms);
                mouseClick("mainWindow/visualOverlay", spix::MouseButtons::Right);
                for (int i = 0; i < 50 && filterEntries().isEmpty(); ++i)
                    wait(100ms);
                openFilters();
                wait(500ms);
                status();
                observed.push_back(filtersLook(code, dark)); // 14, 15
                closeContextMenu();
                wait(300ms);
            }
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
    QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/dshow-cfr.avi"), dir.filePath(QStringLiteral("ep1.avi")));
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    {
        QFile f(subtitles);
        expect(f.open(QIODevice::WriteOnly) &&
                   f.write(QString::fromLatin1(kSubtitles).arg(QStringLiteral("ep1.avi")).toUtf8()) > 0,
               "write the subtitles");
    }
    hikari::app::Application::Options options;
    options.mediaHelper = QStringLiteral(HIKARI_MEDIA_HELPER);
    options.playbackAudio = false; // the general player opens no device; DirectShow has its own renderer
    hikari::app::Application application(options);
    g_app = &application;
    application.settingsStore()->setValue(QStringLiteral("video.playbackPlayer"), 1);
    expect(application.directShowPlayback(), "DirectShow is the chosen player");
    auto &video = application.video();
    QObject::connect(&video, &hikari::ui::VideoController::changed, [&video, &application] {
        const auto &session = video.session();
        std::lock_guard lock(g_mutex);
        if (const auto frame = session.lastFrame()) {
            if (frame->index < 0) {
                ++g_shown.playerFrames;
                g_shown.lastPlayerBarcode = barcode(*frame);
            }
            g_shown.frame = frame->index;
            g_shown.barcode = barcode(*frame);
        }
        g_shown.accepted = session.lastPresent() &&
                           session.lastPresent()->outcome == hikari::application::PresentOutcome::Accepted;
        g_shown.directShow = application.directShowPlayback() &&
                             session.generalPlayer() == static_cast<hikari::application::GeneralPlayerPort *>(
                                                            application.directShowPlayer());
    });
    expect(application.openFile(subtitles), "open the subtitles");
    // The app's controls style (composition.cpp): the platform default on
    // Windows is the native style, which divided by zero offscreen once the
    // Filters submenu was shown (qtquickcontrols2nativestyleplugin, Qt 6.11.2).
    hikari::ui::theme::chooseControlsStyle();
    QQmlApplicationEngine engine;
    g_engine = &engine;
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
    std::printf("filters: %s\n", qPrintable(shown().filters.join(QStringLiteral(", "))));
    expect(o.size() == 16, "every step observed");
    if (o.size() == 16) {
        expect(o[0] == "frame 0", "the first frame is shown");
        expect(o[1] == "directshow", "the session plays through the DirectShow adapter");
        expect(o[2] == "filters", "the context menu's Filters lists the graph");
        expect(o[3] == "Pause", "Play starts playback");
        expect(o[4] == "playing", "DirectShow's frames are shown");
        expect(o[5] == "identity", "Pause shows the exact indexed frame of the last DirectShow frame");
        expect(o[6] == "Play", "paused");
        expect(o[7] == "moved", "playback advanced");
        expect(o[8] == "next", "Next frame steps one indexed frame");
        expect(o[9] == "previous", "Previous frame steps back");
        expect(o[10] == "playing again", "Play goes on through DirectShow");
        expect(o[11] == "frame 0", "Stop returns to the first frame");
        expect(o[12] == "next button", "the Next frame button steps one indexed frame");
        expect(o[13] == "previous button", "the Previous frame button steps back");
        expect(o[14] == "dark", "the Filters submenu takes the Dark theme");
        expect(o[15] == "light", "the Filters submenu takes the Light theme");
    }
    return failures == 0 ? 0 : 1;
}
