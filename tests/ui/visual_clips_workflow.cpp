// T4 workflow through the real UI (Spix): open a Document and its video,
// choose the vector clip on the rail, add three points with the Add line mode
// (the tool's own row), switch to Move points, select a point with a click
// and nudge it with the D key (one step on the key's release), invert the
// clip with its button; then the rectangle clip: a click and Invert clip.
// The written clip is compared with the shared view's legacy arithmetic for
// the clicked positions.

#include "hikari/app/application.h"
#include "hikari/application/visual_view.h"
#include "docking.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <Spix/QtQmlBot.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace std::chrono_literals;

namespace {

class Workflow : public spix::TestServer {
public:
    std::vector<std::string> observed;
    std::atomic<bool> finished{false};

protected:
    std::string prop(const char *path, const char *name) { return getStringProperty(path, name); }

    void executeTest() override
    {
        bool shown = false;
        for (int i = 0; i < 200 && !shown; ++i) {
            shown = prop("mainWindow/videoStatus", "text").rfind("Frame", 0) == 0;
            if (!shown)
                wait(100ms);
        }
        observed.push_back(shown ? "video shown" : "no video"); // 0
        // The vector clip, Add line on (VectorItem's default).
        mouseClick("mainWindow/visualTool7");
        wait(200ms);
        observed.push_back(prop("mainWindow/visualTool7", "checked"));        // 1
        observed.push_back(prop("mainWindow/visualOption_mode1", "checked")); // 2
        observed.push_back(prop("mainWindow/visualOption_mode1", "iconRole")); // 3
        // Three points: an "m" then two lines.
        mouseClick("mainWindow/visualOverlay", spix::Point(0.25, 0.25));
        wait(200ms);
        mouseClick("mainWindow/visualOverlay", spix::Point(0.75, 0.25));
        wait(200ms);
        mouseClick("mainWindow/visualOverlay", spix::Point(0.75, 0.75));
        wait(300ms);
        // Move points: a click selects the last point, D nudges it.
        mouseClick("mainWindow/visualOption_mode0");
        wait(100ms);
        observed.push_back(prop("mainWindow/visualOption_mode0", "checked")); // 4
        mouseClick("mainWindow/visualOverlay", spix::Point(0.75, 0.75));
        wait(200ms);
        enterKey("mainWindow/visualOverlay", Qt::Key_D, 0);
        wait(300ms);
        // Invert clip.
        mouseClick("mainWindow/visualOption_invert");
        wait(300ms);
        // The rectangle clip: a click at a point leaves a rectangle without
        // width, which writes nothing (legacy); Invert clip then has no clip.
        mouseClick("mainWindow/visualTool6");
        wait(200ms);
        observed.push_back(prop("mainWindow/visualTool6", "checked")); // 5
        observed.push_back(prop("mainWindow/visualOption_invert", "iconRole")); // 6
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/visual-clips-workflow.png");
        finished = true;
    }
};

constexpr char kSubtitles[] = "[Script Info]\r\n"
                              "ScriptType: v4.00+\r\n"
                              "PlayResX: 640\r\n"
                              "PlayResY: 480\r\n"
                              "\r\n"
                              "[Events]\r\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                              "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\an7}sign\r\n";

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString subtitles = dir.filePath(QStringLiteral("clips.ass"));
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    {
        QFile f(subtitles);
        expect(f.open(QIODevice::WriteOnly) && f.write(kSubtitles) > 0, "write the subtitles");
    }
    hikari::app::Application application({QStringLiteral(HIKARI_MEDIA_HELPER)});
    application.setAskForBadResolution(false); // the script is 640x480, the video 320x240
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
    expect(window->isExposed(), "the window is exposed");
    application.video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));

    Workflow test;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    QTimer done;
    QObject::connect(&done, &QTimer::timeout, &app, [&] {
        if (test.finished)
            QCoreApplication::exit(0);
    });
    done.start(100);
    app.exec();

    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 7, "every step observed");
    if (o.size() == 7) {
        expect(o[0] == "video shown", "the video is shown");
        expect(o[1] == "true", "the rail chooses the vector clip");
        expect(o[2] == "true", "Add line is the vector clip's first mode");
        expect(o[3] == "vector-line", "the mode shows its K1 icon");
        expect(o[4] == "true", "Move points chosen in the tool's row");
        expect(o[5] == "true", "the rail chooses the rectangle clip");
        expect(o[6] == "clip-invert", "the rectangle clip's Invert clip button");
    }
    // The points as the vector clip's arithmetic puts them (legacy
    // DrawingAndClip::AddMove / AddLine: (zoomed-out view x) * coeffW, written
    // with "%6.0f"), the last one nudged by D, then \iclip.
    auto &tools = application.visualTools();
    const auto &view = tools.videoView();
    auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("visualOverlay"));
    expect(overlay != nullptr, "the overlay exists");
    const auto target = application.workspace().editingTarget();
    const auto *session = target ? application.files().session(*target) : nullptr;
    expect(session != nullptr, "the Document is open");
    if (overlay && session) {
        const auto point = [&](double fx, double fy, int nudge) {
            // Spix clicks at the proportion of the item's size.
            const int x = view.toDevice(overlay->width() * fx), y = view.toDevice(overlay->height() * fy);
            const float px = ((x / view.zoomScale().x) + view.zoomMove().x) * view.coeffW() + nudge;
            const float py = ((y / view.zoomScale().y) + view.zoomMove().y) * view.coeffH();
            char buffer[64];
            std::snprintf(buffer, sizeof buffer, "%.0f %.0f", static_cast<double>(px), static_cast<double>(py));
            return std::string(buffer);
        };
        const std::string expected = "{\\iclip(m " + point(0.25, 0.25, 0) + " l " + point(0.75, 0.25, 0) + " " +
                                     point(0.75, 0.75, 1) + ")\\an7}sign";
        const auto &text = session->document().lines().front()->text;
        const std::string line(reinterpret_cast<const char *>(text.data()), text.size());
        std::printf("line %s, expected %s\n", line.c_str(), expected.c_str());
        expect(line == expected, "the clicks and the nudge write legacy's clip, inverted");
        // Three clicks (three steps), the nudge (one), the inversion (one).
        int clipSteps = 0;
        for (const auto &step : session->history())
            clipSteps += step.name == "Visual vector clipping tool";
        expect(clipSteps == 5, "one step per click, the nudge and the inversion");
    }
    return failures == 0 ? 0 : 1;
}
