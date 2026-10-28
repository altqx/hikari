// S1 workflow through the real UI (Spix): macros run from the shell on the
// editing target. The bundled edgeblur applies as one undo step; a macro's
// dialog is answered in the fixed QML window while the macro reads the open
// video through host services; Cancel in the progress window and a lost
// helper leave the Document unchanged and editable.

#include "hikari/app/application.h"
#include "docking.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <Spix/QtQmlBot.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace std::chrono_literals;

namespace {

hikari::app::Application *g_app = nullptr;
std::mutex g_mutex;
std::vector<std::pair<bool, std::string>> g_completed;

// Runs on the GUI thread and waits for it.
template <typename F> void onGui(F f)
{
    QMetaObject::invokeMethod(g_app, f, Qt::BlockingQueuedConnection);
}

std::string firstText()
{
    std::string out;
    onGui([&] {
        auto *s = g_app->files().session(*g_app->workspace().editingTarget());
        const auto &t = s->document().lines().front()->text;
        out.assign(t.begin(), t.end());
    });
    return out;
}

class Workflow : public spix::TestServer {
public:
    std::vector<std::string> observed;

protected:
    std::size_t completions()
    {
        std::lock_guard lock(g_mutex);
        return g_completed.size();
    }
    bool waitForCompletion(std::size_t count)
    {
        for (int i = 0; i < 150 && completions() < count; ++i) {
            wait(100ms);
            getStringProperty("mainWindow", "title"); // let queued Spix commands run
        }
        return completions() >= count;
    }
    void runMacro(const std::string &script, int ordinal)
    {
        bool started = false;
        QString message;
        onGui([&] {
            started = g_app->automation().run(script, ordinal);
            message = g_app->automation().lastMessage();
        });
        std::fprintf(stderr, "run %s #%d: %s %s\n", script.c_str(), ordinal, started ? "started" : "refused",
                     qPrintable(message));
    }
    void executeTest() override
    {
        const std::string edgeblur = HIKARI_AUTOLOAD_DIR "/macro-1-edgeblur.lua";
        const std::string fixture = HIKARI_LUA_FIXTURES "/shell-fixture.lua";
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            enterKey("mainWindow/editingGrid", Qt::Key_Home, spix::KeyModifiers::None);
            wait(300ms);
            // 1. The bundled edgeblur on the selected first Line, as one step.
            runMacro(edgeblur, 0);
            observed.push_back(waitForCompletion(1) ? firstText() : "edgeblur did not finish"); // 0
            // 2. A dialog macro: the fixed window, answered with OK.
            runMacro(fixture, 0);
            if (waitForItem("automationDialog/dialogButton0", 10s)) {
                mouseClick("automationDialog/dialogButton0");
                observed.push_back(waitForCompletion(2) ? firstText() : "dialog macro did not finish"); // 1
                observed.push_back(getStringProperty("automationProgress/automationLog", "text")); // 2
            }
            // 3. Cancel from the progress window.
            runMacro(fixture, 1);
            if (waitForItem("automationProgress/automationCancel", 10s)) {
                wait(500ms);
                mouseClick("automationProgress/automationCancel");
                observed.push_back(waitForCompletion(3) ? "cancelled" : "cancel did not finish"); // 3
            }
            // 4. The helper is lost while the macro runs.
            runMacro(fixture, 1);
            wait(500ms);
            qint64 pid = 0;
            onGui([&] { pid = g_app->automation().manager().host(fixture)->processId(); });
#ifdef _WIN32
            if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
                TerminateProcess(h, 9);
                CloseHandle(h);
            }
#else
            ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
            observed.push_back(waitForCompletion(4) ? "lost" : "loss did not finish"); // 4
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        // The macros left unsaved work: approve the quit by discarding it
        // through the review (quit() closes the window, which reviews first).
        onGui([] {
            QVariantList choices;
            for (const QVariant &row : g_app->reviewClose(QStringLiteral("quit")))
                choices << QVariantMap{{QStringLiteral("id"), row.toMap().value(QStringLiteral("id"))},
                                       {QStringLiteral("save"), false}};
            g_app->resolveClose(choices);
        });
        quit();
    }
};

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ep1.ass"));
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 240\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n"
                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Door\n");
    }
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    hikari::app::Application application({QStringLiteral(HIKARI_MEDIA_HELPER)});
    g_app = &application;
    expect(application.openFile(path), "open");
    application.video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
    auto &automation = application.automation();
    QObject::connect(&automation, &hikari::app::AutomationShell::runCompleted, [](bool ok, const QString &message) {
        std::fprintf(stderr, "completed %d: %s\n", ok, qPrintable(message));
        std::lock_guard lock(g_mutex);
        g_completed.emplace_back(ok, message.toStdString());
    });
    automation.load(HIKARI_AUTOLOAD_DIR "/macro-1-edgeblur.lua");
    automation.load(HIKARI_LUA_FIXTURES "/shell-fixture.lua");
    QDeadlineTimer loading(20'000);
    while ((automation.scripts().size() < 2 || std::ranges::any_of(automation.scripts(), [](const auto &s) {
                return s.state == hikari::application::ScriptStatus::State::Loading;
            }) || application.video().session().state() != hikari::application::VideoSession::State::Ready) &&
           !loading.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expect(application.video().hasVideo(), "the video is open");

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
    const auto steps = application.files().session(*application.workspace().editingTarget())->historySize();
    Workflow test;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();

    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    for (const auto &[ok, message] : g_completed)
        std::printf("completed %d: %s\n", ok, message.c_str());
    auto *session = application.files().session(*application.workspace().editingTarget());
    expect(o.size() == 5, "every step observed");
    if (o.size() == 5) {
        expect(o[0] == "{\\be1}Gate", "edgeblur prefixed the selected Line");
        expect(o[1] == "P:{\\be1}Gate", "the dialog's value reached the macro");
        expect(o[2].find("frame 24, 320x240, video 320x240") != std::string::npos,
               "the macro read the open video (frame_from_ms, get_frame, video_size)");
        expect(o[3] == "cancelled", "Cancel ended the macro");
        expect(o[4] == "lost", "a lost helper ended the macro");
    }
    expect(g_completed.size() == 4 && g_completed[0].first && g_completed[1].first && !g_completed[2].first &&
               !g_completed[3].first,
           "two applied, cancel and loss changed nothing");
    expect(session->historySize() == steps + 2, "each applied macro is one undo step");
    expect(!session->isReadOnly(), "the Document is editable again");
    expect(session->undo() && session->undo(), "Undo reverts both macros");
    expect(session->document().lines().front()->text == u8"Gate", "back to the opened text");
    return failures == 0 ? 0 : 1;
}
