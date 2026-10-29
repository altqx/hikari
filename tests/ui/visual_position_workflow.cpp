#include "hikari/app/application.h"
#include "hikari/application/grid_split.h"
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
#include <functional>
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
    std::function<std::string()> documentText;

protected:
    std::string prop(const char *path, const char *name) { return getStringProperty(path, name); }
    std::string lineText()
    {
        std::string text;
        QMetaObject::invokeMethod(qApp, documentText, Qt::BlockingQueuedConnection, &text);
        return text;
    }

    std::string changedFrom(const std::string &before)
    {
        // A Spix property read runs after every command queued before it.
        (void)prop("mainWindow/videoPanel", "visible");
        std::string now = lineText();
        for (int i = 0; i < 100 && now == before; ++i) {
            wait(100ms);
            now = lineText();
        }
        return now;
    }

    void type(const char *path, const std::string &text)
    {
        invokeMethod(path, "forceActiveFocus", {});
        setStringProperty(path, "text", text);
        enterKey(path, Qt::Key_Return, spix::KeyModifiers::None);
    }

    void executeTest() override
    {
        bool shown = false;
        for (int i = 0; i < 200 && !shown; ++i) {
            shown = prop("mainWindow/videoStatus", "text").rfind("Frame", 0) == 0;
            if (!shown)
                wait(100ms);
        }
        observed.push_back(shown ? "video shown" : "no video");
        mouseClick("mainWindow/visualTool1");
        wait(200ms);
        observed.push_back(prop("mainWindow/visualTool1", "checked"));
        observed.push_back(prop("mainWindow/visualOption_byRectangle", "iconRole"));
        observed.push_back(prop("mainWindow/visualValue_x", "text"));
        const std::string initial = lineText();
        type("mainWindow/visualValue_x", "150");
        observed.push_back(changedFrom(initial));
        invokeMethod("mainWindow/videoPanel", "forceActiveFocus", {});
        enterKey("mainWindow/videoPanel", Qt::Key_Right, spix::KeyModifiers::None);
        observed.push_back(changedFrom(observed[4]));
        enterKey("mainWindow/videoPanel", Qt::Key_Down, spix::KeyModifiers::Shift);
        observed.push_back(changedFrom(observed[5]));
        mouseClick("mainWindow/visualTool2");
        wait(200ms);
        observed.push_back(prop("mainWindow/visualTool2", "checked"));
        const bool folded = prop("mainWindow/visualToolMore", "visible") == "true";
        if (folded) {
            mouseClick("mainWindow/visualToolMore");
            wait(300ms);
        }
        type("mainWindow/visualValue_x2", "200");
        observed.push_back(changedFrom(observed[6]));
        if (folded) {
            mouseClick("mainWindow/visualToolMore");
            wait(300ms);
        }
        invokeMethod("mainWindow/videoPanel", "forceActiveFocus", {});
        enterKey("mainWindow/videoPanel", Qt::Key_Down, spix::KeyModifiers::Alt);
        observed.push_back(changedFrom(observed[8]));
        mouseClick("mainWindow/visualOption_twoPoints");
        wait(200ms);
        observed.push_back(prop("mainWindow/visualOption_twoPoints", "checked"));
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/visual-position-workflow.png");
        finished = true;
    }
};

constexpr char kSubtitles[] = "[Script Info]\r\n"
                              "ScriptType: v4.00+\r\n"
                              "PlayResX: 320\r\n"
                              "PlayResY: 240\r\n"
                              "\r\n"
                              "[Events]\r\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                              "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\an5\\pos(100,80)}sign\r\n";

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString subtitles = dir.filePath(QStringLiteral("position.ass"));
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
    test.documentText = [&application] {
        const auto target = application.workspace().editingTarget();
        const auto *session = target ? application.files().session(*target) : nullptr;
        if (!session)
            return std::string();
        const auto &text = session->document().lines().front()->text;
        return std::string(text.begin(), text.end());
    };
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
    expect(o.size() == 11, "every step observed");
    if (o.size() != 11)
        return 1;
    expect(o[0] == "video shown", "the video is shown");
    expect(o[1] == "true", "Position is the rail's tool");
    expect(o[2] == "frame-to-scale", "the rectangle option shows its icon of the set");
    expect(o[3] == "100", "the values row shows the active Line's X");
    const auto &view = application.visualTools().videoView();
    using hikari::application::visual::PointF;
    auto ft = [](float v) {
        const auto t = hikari::application::legacy::floatText(v);
        return std::string(t.begin(), t.end());
    };
    auto step = [&](float x, float y, float dx, float dy, float &outX, float &outY) {
        PointF p = view.scriptToView({x, y});
        p.x += (dx / view.coeffW()) * view.zoomScale().x;
        p.y += (dy / view.coeffH()) * view.zoomScale().y;
        const PointF q = view.viewToScript(p);
        outX = QString::fromStdString(ft(q.x)).toFloat();
        outY = QString::fromStdString(ft(q.y)).toFloat();
        return "{\\an5\\pos(" + ft(q.x) + "," + ft(q.y) + ")}sign";
    };
    float x = 0, y = 0;
    const std::string typed = step(150, 80, 0, 0, x, y);
    std::printf("expected %s\n", typed.c_str());
    expect(o[4] == typed, "a typed X writes the active Line's \\pos");
    const std::string afterD = step(x, y, 1, 0, x, y);
    std::printf("expected %s\n", afterD.c_str());
    expect(o[5] == afterD, "D nudges the Line one script pixel right");
    const std::string afterS = step(x, y, 0, 0.1f, x, y);
    std::printf("expected %s\n", afterS.c_str());
    expect(o[6] == afterS, "Shift+S nudges it a tenth of a pixel down");
    expect(o[7] == "true", "Move is the rail's tool");
    const auto timebase = application.video().session().legacyTimebase();
    const int startRel = std::abs(timebase.msAt(timebase.frameAt(0)) - 0);
    const int endRel = 1000 - std::abs(1000 - timebase.msAt(timebase.frameAt(1000) - 1));
    const std::string head = "{\\an5\\move(" + ft(x) + "," + ft(y) + ",";
    const std::string times = "," + ft(float(startRel)) + "," + ft(float(endRel)) + ")}sign";
    const std::string typedEnd = head + "200," + ft(y) + times;
    std::printf("expected %s\n", typedEnd.c_str());
    expect(o[8] == typedEnd, "a typed X2 writes \\move with the Line's frame times");
    PointF end = view.scriptToView({200, y});
    end.y += 1.f / view.coeffH();
    end = view.viewToScript(end);
    const std::string nudged = head + ft(end.x) + "," + ft(end.y) + times;
    std::printf("expected %s\n", nudged.c_str());
    expect(o[9] == nudged, "K nudges the \\move's end");
    expect(o[10] == "true", "the two-point option toggles");
    return failures == 0 ? 0 : 1;
}
