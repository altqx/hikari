// P10 workflow through the real UI (Spix): the status bar's fields without
// and with media. A subtitle Document (PlayRes 640 x 480) whose Script Info
// names its video opens: only its resolution shows, as legacy's
// SetSubsResolution left it. Accepting the "Associated files" offer opens the
// video: its scale, zoom, duration, FPS, resolution, aspect ratio and file
// name show (VideoBox::OpenVideo, SetScaleAndZoom, SetVideoResolution), and
// the two resolutions, which differ, show in the warning colour. The first
// field never names the editing target.

#include "hikari/app/application.h"
#include "docking.h"

#include <QColor>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <Spix/QtQmlBot.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace std::chrono_literals;

namespace {

const char *const kFields[] = {"statusText",          "statusVideoScale",      "statusVideoZoom",
                               "statusVideoDuration", "statusFramesPerSecond", "statusVideoResolution",
                               "statusAspectRatio",   "statusSubtitlesResolution", "statusVideoName"};

class Workflow : public spix::TestServer {
public:
    // Each field's text before and after the video opened ("-" when hidden).
    std::map<std::string, std::string> before, after;
    std::string resolutionColour, subtitlesColour, fpsColour, warningColour;
    std::atomic<bool> finished{false};

protected:
    std::string field(const char *name)
    {
        const std::string path = std::string("mainWindow/") + name;
        if (!existsAndVisible(path))
            return "-";
        return getStringProperty(path, "text");
    }
    void readFields(std::map<std::string, std::string> &out)
    {
        for (const char *name : kFields)
            out[name] = field(name);
    }

    void executeTest() override
    {
        if (waitForItem("mainWindow/statusSubtitlesResolution", 5s) && waitForItem("mainWindow/loadAssociated", 5s)) {
            readFields(before);
            mouseClick("mainWindow/loadAssociated");
            for (int i = 0; i < 200 && field("statusVideoResolution") == "-"; ++i)
                wait(100ms);
            // (Y4's resolution question comes a moment after the video)
            for (int i = 0; i < 30 && !existsAndVisible("mainWindow/mismatchDialog"); ++i)
                wait(100ms);
            if (existsAndVisible("mainWindow/mismatchDialog"))
                invokeMethod("mainWindow/mismatchDialog", "close", {});
            wait(300ms);
            readFields(after);
            resolutionColour = getStringProperty("mainWindow/statusVideoResolution", "color");
            subtitlesColour = getStringProperty("mainWindow/statusSubtitlesResolution", "color");
            fpsColour = getStringProperty("mainWindow/statusFramesPerSecond", "color");
            warningColour = getStringProperty("mainWindow/statusBar", "warningColour");
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            after.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/status-workflow.png");
        finished = true;
        quit();
    }
};

constexpr char kSubtitles[] =
    "[Script Info]\r\n"
    "ScriptType: v4.00+\r\n"
    "PlayResX: 640\r\n"
    "PlayResY: 480\r\n"
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
    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\r\n";

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
        expect(f.open(QIODevice::WriteOnly) &&
                   f.write(QString::fromLatin1(kSubtitles).arg(QStringLiteral("ep1.mkv")).toUtf8()) > 0,
               "write the subtitles");
    }
    hikari::app::Application::Options options;
    options.mediaHelper = QStringLiteral(HIKARI_MEDIA_HELPER);
    options.playbackAudio = false;
    hikari::app::Application application(options);
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
    app.exec();

    for (const char *name : kFields)
        std::printf("%s: before \"%s\" after \"%s\"\n", name, test.before[name].c_str(), test.after[name].c_str());
    std::printf("colours: resolution %s subtitles %s fps %s warning %s\n", test.resolutionColour.c_str(),
                test.subtitlesColour.c_str(), test.fpsColour.c_str(), test.warningColour.c_str());
    expect(!test.after.empty(), "every step observed");
    auto &b = test.before;
    auto &a = test.after;
    expect(b["statusSubtitlesResolution"] == "640 x 480", "without video: the subtitles' resolution");
    expect(b["statusVideoScale"] == "-" && b["statusVideoZoom"] == "-" && b["statusVideoDuration"] == "-" &&
               b["statusFramesPerSecond"] == "-" && b["statusVideoResolution"] == "-" && b["statusAspectRatio"] == "-",
           "without video: no video field takes space");
    expect(b["statusVideoName"].empty() && b["statusText"].find("ep1") == std::string::npos,
           "without video: the stretching fields are empty");
    expect(a["statusVideoScale"].size() > 1 && a["statusVideoScale"].back() == '%', "the video's scale");
    expect(a["statusVideoZoom"] == "100%", "the video's zoom");
    expect(a["statusVideoDuration"] == "00:00:01,960", "the video's duration (SubsTime::raw(SRT))");
    expect(a["statusFramesPerSecond"] == "23.976 FPS", "the video's FPS (getfloat)");
    expect(a["statusVideoResolution"] == "320 x 240", "the video's resolution");
    expect(a["statusAspectRatio"] == "4 : 3", "the video's aspect ratio");
    expect(a["statusSubtitlesResolution"] == "640 x 480", "the subtitles' resolution");
    expect(a["statusVideoName"] == "ep1.mkv", "the video's file name");
    expect(a["statusText"].find("ep1") == std::string::npos, "the first field never names the editing target");
    expect(!test.warningColour.empty() && QColor(QString::fromStdString(test.resolutionColour)) ==
                                              QColor(QString::fromStdString(test.warningColour)) &&
               QColor(QString::fromStdString(test.subtitlesColour)) == QColor(QString::fromStdString(test.warningColour)),
           "the differing resolutions in the warning colour");
    expect(QColor(QString::fromStdString(test.fpsColour)) != QColor(QString::fromStdString(test.warningColour)),
           "the other fields in the text colour");
    return failures == 0 ? 0 : 1;
}
