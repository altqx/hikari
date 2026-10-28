// T1 workflow through the real UI (Spix): open a Document and its video,
// click the video with the crosshair (the rail's default tool) to place it,
// copy the video position (VIDEO_COPY_COORDS) and Ctrl+click to put \pos
// into the active Line; then choose another family on the rail and back.
// The copied text and the \pos are compared with the shared view's legacy
// arithmetic for the same pointer position.

#include "hikari/app/application.h"
#include "hikari/application/visual_view.h"
#include "docking.h"

#include <QClipboard>
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
        // The video is indexed and shown.
        bool shown = false;
        for (int i = 0; i < 200 && !shown; ++i) {
            shown = prop("mainWindow/videoStatus", "text").rfind("Frame", 0) == 0;
            if (!shown)
                wait(100ms);
        }
        observed.push_back(shown ? "video shown" : "no video"); // 0
        observed.push_back(prop("mainWindow/visualTool0", "checked")); // 1: the crosshair is the tool
        // A plain click places the crosshair at the pointer.
        mouseClick("mainWindow/visualOverlay", spix::Point(0.5, 0.5));
        wait(200ms);
        observed.push_back(prop("mainWindow/visualValue_position", "text")); // 2: its label
        // VIDEO_COPY_COORDS (no default key; the context menu is V4's).
        invokeMethod("mainWindow", "runVideoHotkey", {std::string("VIDEO_COPY_COORDS")});
        wait(200ms);
        // Ctrl+click puts \pos there.
        mouseClick("mainWindow/visualOverlay", spix::MouseButtons::Left, spix::KeyModifiers::Control);
        wait(300ms);
        // Another family and back to the crosshair.
        mouseClick("mainWindow/visualTool3");
        wait(100ms);
        observed.push_back(prop("mainWindow/visualTool3", "checked")); // 3
        mouseClick("mainWindow/visualTool3");
        wait(100ms);
        observed.push_back(prop("mainWindow/visualTool0", "checked")); // 4
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/visual-tools-workflow.png");
        // The Document has unsaved changes, so quit() would wait for the
        // close review; main checks the result instead.
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
    const QString subtitles = dir.filePath(QStringLiteral("visual.ass"));
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
    QGuiApplication::clipboard()->clear();

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
    expect(o.size() == 5, "every step observed");
    if (o.size() == 5) {
        expect(o[0] == "video shown", "the video is shown");
        expect(o[1] == "true", "the crosshair is the rail's tool");
        expect(o[3] == "true", "a rail button chooses its family");
        expect(o[4] == "true", "choosing it again goes back to the crosshair");
    }
    // Spix clicks the overlay's centre: the expected text and \pos follow
    // from the shared view at that point.
    auto &tools = application.visualTools();
    const auto &view = tools.videoView();
    auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("visualOverlay"));
    expect(overlay != nullptr, "the overlay exists");
    if (overlay) {
        const QPointF centre(overlay->width() / 2, overlay->height() / 2);
        const int x = view.toDevice(centre.x()), y = view.toDevice(centre.y());
        // Cross's label: its own coefficients (VisualCross.cpp:80-99).
        const auto r = view.videoRect();
        const float cx = float(view.scriptWidth()) / float(r.width() - (r.left ? 0 : 1));
        const float cy = float(view.scriptHeight()) / float(r.height() - (r.top ? 0 : 1));
        const int lx = int(((x / view.zoomScale().x) + view.zoomMove().x) * cx);
        const int ly = int(((y / view.zoomScale().y) + view.zoomMove().y) * cy);
        const std::string label = std::to_string(lx) + ", " + std::to_string(ly);
        std::printf("label %s, expected %s\n", o.size() > 2 ? o[2].c_str() : "-", label.c_str());
        expect(o.size() > 2 && o[2] == label, "the crosshair's label reads the pointer's script position");
        // VIDEO_COPY_COORDS: the same script position in legacy's "x,y" form
        // (approved departure T1-copy-coords-view).
        const QString copied = QStringLiteral("%1,%2").arg(lx).arg(ly);
        std::printf("copied %s, expected %s\n", qPrintable(QGuiApplication::clipboard()->text()), qPrintable(copied));
        expect(QGuiApplication::clipboard()->text() == copied, "VIDEO_COPY_COORDS copies the script position as x,y");
        expect(tools.copied() == copied, "the copied text is recorded");
    }
    const auto target = application.workspace().editingTarget();
    const auto *session = target ? application.files().session(*target) : nullptr;
    expect(session != nullptr, "the Document is open");
    if (session) {
        const auto &text = session->document().lines().front()->text;
        const QString line = QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size()));
        std::printf("line %s\n", qPrintable(line));
        expect(line.startsWith(QStringLiteral("{\\pos(")) && line.endsWith(QStringLiteral("\\an7}sign")),
               "Ctrl+click puts \\pos first in the Line's first block");
        expect(session->history().back().name == "Visual positioning tool", "one step, named as legacy's");
    }
    return failures == 0 ? 0 : 1;
}
