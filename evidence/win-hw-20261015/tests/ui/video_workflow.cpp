// I1 workflow through the real UI (Spix): open a subtitle Document whose
// Script Info names its video, accept the "Associated files" offer, select
// the second Line ("Every line change", V6) and see that Line's start frame
// with its overlay, then step
// one frame. Frame identity is read from the barcode each fixture frame
// carries. A Document whose video is missing offers nothing and stays
// editable.

#include "hikari/app/application.h"
#include "docking.h"

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
    std::vector<Shown> frames;
    std::atomic<bool> finished{false};

protected:
    std::string status() { return getStringProperty("mainWindow/videoStatus", "text"); }
    // Waits until the presenter accepted `frame`.
    bool waitForFrame(int frame)
    {
        for (int i = 0; i < 100; ++i) {
            const Shown s = shown();
            if (s.frame == frame && s.accepted)
                return true;
            wait(100ms);
            status(); // Spix commands are queued: a synchronous read lets the wait run
        }
        return false;
    }

    void executeTest() override
    {
        if (waitForItem("mainWindow/loadAssociated", 5s)) {
            observed.push_back(getStringProperty("mainWindow/associationText", "text")); // 0
            mouseClick("mainWindow/loadAssociated");
            for (int i = 0; i < 100 && status().rfind("Frame", 0) != 0; ++i)
                wait(100ms);
            observed.push_back(status()); // 1: indexed, showing the active Line's frame
            mouseClick("mainWindow/editingGrid");
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            enterKey("mainWindow/editingGrid", Qt::Key_Home, spix::KeyModifiers::None);
            wait(200ms);
            enterKey("mainWindow/editingGrid", Qt::Key_Down, spix::KeyModifiers::None);
            observed.push_back(waitForFrame(24) ? "frame 24" : "no frame 24"); // 2
            frames.push_back(shown());
            mouseClick("mainWindow/nextFrame");
            observed.push_back(waitForFrame(25) ? "frame 25" : "no frame 25"); // 3
            frames.push_back(shown());
            observed.push_back(status()); // 4
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/video-workflow.png");
        finished = true;
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
    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\p1}m 0 0 l 320 0 320 40 0 40{\\p0}\r\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an1\\pos(0,240)\\p1}m 0 0 l 320 0 320 40 0 40{\\p0}\r\n";

bool write(const QString &path, const QString &videoFile)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QString::fromLatin1(kSubtitles).arg(videoFile).toUtf8());
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString subtitles = dir.filePath(QStringLiteral("ep1.ass"));
    const QString orphan = dir.filePath(QStringLiteral("orphan.ass"));
    QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), dir.filePath(QStringLiteral("ep1.mkv")));

    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    expect(write(subtitles, QStringLiteral("ep1.mkv")) && write(orphan, QStringLiteral("missing.mkv")),
           "write the subtitle files");
    {
        hikari::app::Application application({QStringLiteral(HIKARI_MEDIA_HELPER)});
        auto &video = application.video();
        QObject::connect(&video, &hikari::ui::VideoController::changed, [&video] {
            const auto &session = video.session();
            Shown s;
            if (const auto frame = session.lastFrame()) {
                s.frame = frame->index;
                s.barcode = barcode(*frame);
            }
            s.overlay = session.lastOverlay() && !session.lastOverlay()->empty;
            s.accepted = session.lastPresent() &&
                         session.lastPresent()->outcome == hikari::application::PresentOutcome::Accepted;
            std::lock_guard lock(g_mutex);
            g_shown = s;
        });
        expect(application.openFile(subtitles), "open the subtitles");
        // V6: the video follows a selected Line with the video toolbar's
        // "Every line change" (legacy MOVE_VIDEO_TO_ACTIVE_LINE 1; its default
        // moves the video only on a double click).
        application.settingsStore()->set("video.moveToActiveLine", 1);
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

        const auto &o = test.observed;
        for (std::size_t i = 0; i < o.size(); ++i)
            std::printf("observed[%zu] = %s\n", i, o[i].c_str());
        expect(o.size() == 5, "every step observed");
        if (o.size() == 5) {
            expect(o[0].find("ep1.mkv") != std::string::npos, "the association is offered");
            expect(o[1].rfind("Frame 0 of 48", 0) == 0, "indexed; the first Line's frame is shown");
            expect(o[2] == "frame 24", "the second Line shows its start frame (first at or after 1.0 s)");
            expect(o[3] == "frame 25", "Next frame steps one frame");
            expect(o[4].rfind("Frame 25 of 48", 0) == 0, "the status names the frame");
        }
        for (std::size_t i = 0; i < test.frames.size(); ++i)
            std::printf("frame %d barcode %d overlay %d accepted %d\n", test.frames[i].frame, test.frames[i].barcode,
                        test.frames[i].overlay, test.frames[i].accepted);
        expect(test.frames.size() == 2 && test.frames[0].barcode == 24 && test.frames[1].barcode == 25,
               "the presented frames carry their own identity");
        expect(test.frames.size() == 2 && test.frames[0].overlay && test.frames[1].overlay,
               "the second Line's overlay is drawn on its frames");
    }

    // Missing video: nothing is offered, and the Document stays editable.
    hikari::app::Application missing({QStringLiteral(HIKARI_MEDIA_HELPER)});
    expect(missing.openFile(orphan), "open the subtitles whose video is missing");
    expect(!missing.video().offering(), "nothing is offered for a missing video");
    expect(!missing.video().hasVideo(), "no video is open");
    auto &editor = missing.editor();
    const auto target = missing.workspace().editingTarget();
    const auto first = missing.files().session(*target)->document().lines().front()->id;
    expect(editor.showLine(first.value), "a Line can be shown");
    editor.setEndText(QStringLiteral("0:00:01.50"));
    expect(editor.commit(), "an edit commits without video");
    expect(missing.files().session(*target)->document().lines().front()->end.value.microseconds() == 1'500'000,
           "the edit is in the Document");
    return failures == 0 ? 0 : 1;
}
