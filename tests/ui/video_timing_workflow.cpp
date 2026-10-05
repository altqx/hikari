// V6 workflow through the real UI (Spix): two Lines timed from the video.
// The Document's associated video (cfr.mkv, 24000/1001) is loaded, the Next
// frame button steps it, and the Global bindings time the active Line from
// the shown frame (GLOBAL_SET_START_TIME Ctrl+Left, GLOBAL_SET_END_TIME
// Ctrl+Right: legacy's midpoint representatives to centiseconds,
// HikariSubFrame.cpp:750-760), then F2 (GLOBAL_SELECT_FROM_VIDEO) selects the
// Line at the video's time and Undo takes one timing step back.

#include "hikari/app/application.h"
#include "docking.h"
#include "theme.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
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
    std::atomic<bool> done{false};

protected:
    std::string status() { return getStringProperty("mainWindow/videoStatus", "text"); }
    // `button` (Next or Previous frame) until the status names `frame`: one
    // click at a time, each waited for, so the step never overshoots.
    bool stepTo(int frame, const char *button = "mainWindow/nextFrame")
    {
        const std::string want = "Frame " + std::to_string(frame) + " of";
        for (int n = 0; n < 60; ++n) {
            const std::string before = status();
            if (before.rfind(want, 0) == 0)
                return true;
            mouseClick(button);
            for (int i = 0; i < 40 && status() == before; ++i)
                wait(50ms);
        }
        return status().rfind(want, 0) == 0;
    }
    void key(int code, unsigned modifiers = spix::KeyModifiers::None)
    {
        // the frame buttons take the focus: the keys go to the Grid again
        invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
        enterKey("mainWindow/editingGrid", code, modifiers);
        wait(400ms);
    }

    void executeTest() override
    {
        if (waitForItem("mainWindow/loadAssociated", 5s)) {
            mouseClick("mainWindow/loadAssociated");
            for (int i = 0; i < 100 && status().rfind("Frame", 0) != 0; ++i)
                wait(100ms);
            observed.push_back(status()); // 0: indexed at the first frame
            mouseClick("mainWindow/editingGrid");
            key(Qt::Key_Home); // the first Line
            observed.push_back(stepTo(12) ? "frame 12" : "no frame 12"); // 1
            key(Qt::Key_Left, spix::KeyModifiers::Control); // its start from frame 12
            observed.push_back(stepTo(24) ? "frame 24" : "no frame 24"); // 2
            key(Qt::Key_Right, spix::KeyModifiers::Control); // its end from frame 24
            key(Qt::Key_Down); // the second Line
            key(Qt::Key_Left, spix::KeyModifiers::Control); // its start from frame 24
            observed.push_back(stepTo(36) ? "frame 36" : "no frame 36"); // 3
            key(Qt::Key_Right, spix::KeyModifiers::Control); // its end from frame 36
            observed.push_back(stepTo(12, "mainWindow/previousFrame") ? "frame 12" : "no frame 12"); // 4
            key(Qt::Key_F2); // the Line at 500 ms: the first
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/video-timing-workflow.png");
        // the Document has unsaved changes, so quit() would wait for the close
        // review; main checks the result instead
        done = true;
    }
};

constexpr char kSubtitles[] =
    "[Script Info]\r\n"
    "ScriptType: v4.00+\r\n"
    "PlayResX: 320\r\n"
    "PlayResY: 240\r\n"
    "Video File: ep1.mkv\r\n"
    "\r\n"
    "[V4+ Styles]\r\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\r\n"
    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\r\n"
    "\r\n"
    "[Events]\r\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
    "Dialogue: 0,0:00:00.00,0:00:00.10,Default,,0,0,0,,first\r\n"
    "Dialogue: 0,0:00:00.20,0:00:00.30,Default,,0,0,0,,second\r\n";

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
    {
        QFile f(subtitles);
        if (!f.open(QIODevice::WriteOnly))
            return 2;
        f.write(kSubtitles);
    }

    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    hikari::app::Application application({QStringLiteral(HIKARI_MEDIA_HELPER)});
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
    Workflow test;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    QTimer finished;
    QObject::connect(&finished, &QTimer::timeout, &app, [&] {
        if (test.done)
            QCoreApplication::exit(0);
    });
    finished.start(100);
    app.exec();

    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 5, "every step observed");
    if (o.size() == 5) {
        expect(o[0].rfind("Frame 0 of 48", 0) == 0, "indexed; the video opens at its first frame");
        expect(o[1] == "frame 12" && o[2] == "frame 24" && o[3] == "frame 36" && o[4] == "frame 12",
               "Next and Previous frame reach the frames");
    }
    auto *session = application.files().session(*application.workspace().editingTarget());
    const auto lines = session->document().lines();
    auto ms = [](hikari::core::DocumentTime t) { return t.microseconds() / 1000; };
    std::printf("first %lld-%lld, second %lld-%lld\n", static_cast<long long>(ms(lines[0]->start.value)),
                static_cast<long long>(ms(lines[0]->end.value)), static_cast<long long>(ms(lines[1]->start.value)),
                static_cast<long long>(ms(lines[1]->end.value)));
    // StartTimeFor(12) = 458 + (500 - 458) / 2 + 5 = 484 -> 480; EndTimeFor(24) = 1026 -> 1020;
    // StartTimeFor(24) = 985 -> 980; EndTimeFor(36) = 1501 + (1543 - 1501) / 2 + 5 = 1527 -> 1520
    expect(ms(lines[0]->start.value) == 480 && ms(lines[0]->end.value) == 1020, "the first Line timed from frames 12 and 24");
    expect(ms(lines[1]->start.value) == 980 && ms(lines[1]->end.value) == 1520, "the second Line timed from frames 24 and 36");
    expect(session->selection().active == lines[0]->id, "F2 selects the Line at the video's time");
    std::vector<std::string> names;
    for (const auto &step : session->history())
        names.push_back(step.name);
    expect(names.size() == 5 && names[1] == "Setting start time" && names[2] == "Setting end time" &&
               names[3] == "Setting start time" && names[4] == "Setting end time",
           "each command is one step");
    // the second Line's start step had moved its End along (End < stime)
    expect(session->undo() && ms(session->document().lines()[1]->end.value) == 980, "Undo takes the last one back");
    return failures == 0 ? 0 : 1;
}
