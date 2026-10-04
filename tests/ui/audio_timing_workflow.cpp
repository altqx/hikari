// A3 workflow through the real UI (Spix): three Lines timed by keyboard in
// the audio box over blank audio, as legacy AudioBox's accelerators do it
// with AUDIO_AUTO_COMMIT on (the default): AUDIO_LEAD_IN (C) and
// AUDIO_LEAD_OUT (V) each commit one "Changing time on audio spectrum" step,
// AUDIO_COMMIT (Enter) goes to the next Line, and on the last Line appends
// one ("Adding a new line"). AUDIO_PREVIOUS_ALT (Z) and AUDIO_NEXT (Right)
// move between Lines; AUDIO_GOTO (B) only shows the selection.

#include "hikari/app/application.h"
#include "docking.h"

#include <QDeadlineTimer>
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
    std::atomic<bool> ran = false, done = false;

protected:
    void key(int code)
    {
        enterKey("mainWindow/audioDisplay", code, spix::KeyModifiers::None);
        wait(150ms);
    }
    void executeTest() override
    {
        if (waitForItem("mainWindow/audioDisplay", 5s)) {
            invokeMethod("mainWindow/audioDisplay", "forceActiveFocus", {});
            wait(200ms);
            for (int line = 0; line < 3; ++line) {
                key(Qt::Key_C);
                key(Qt::Key_V);
                key(Qt::Key_Return);
            }
            key(Qt::Key_Z);     // back to the third Line
            key(Qt::Key_Z);     // the second
            key(Qt::Key_Right); // the third again
            key(Qt::Key_B);
            ran = true;
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            ran = false;
        // the Document has unsaved changes, so quit() would wait for the close
        // review; main checks the result instead
        done = true;
    }
};

std::int64_t ms(hikari::core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("timing.ass"));
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,One\n"
                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Two\n"
                "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Three\n");
    }
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    hikari::app::Application application;
    expect(application.openFile(path), "open");
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
    // GLOBAL_OPEN_DUMMY_AUDIO: blank audio, ready at once (nothing plays)
    application.audio().openDummy();
    expect(application.audio().ready(), "blank audio");
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
    expect(test.ran, "the keys were sent");

    const auto target = application.workspace().editingTarget();
    auto *session = target ? application.files().session(*target) : nullptr;
    expect(session != nullptr, "an editing target");
    if (!session)
        return 1;
    const auto lines = session->document().lines();
    for (const auto *l : lines)
        std::printf("line %lld-%lld %s\n", static_cast<long long>(ms(l->start.value)),
                    static_cast<long long>(ms(l->end.value)), reinterpret_cast<const char *>(l->text.c_str()));
    const auto history = application.editor().history();
    for (const auto &step : history)
        std::printf("step %s\n", qPrintable(step));
    expect(lines.size() == 4, "the last Enter appended a Line");
    if (lines.size() == 4) {
        expect(ms(lines[0]->start.value) == 800 && ms(lines[0]->end.value) == 2300, "first Line timed");
        expect(ms(lines[1]->start.value) == 2800 && ms(lines[1]->end.value) == 4300, "second Line timed");
        expect(ms(lines[2]->start.value) == 4800 && ms(lines[2]->end.value) == 6300, "third Line timed");
        expect(ms(lines[3]->start.value) == 6300 && ms(lines[3]->end.value) == 11300 && lines[3]->text.empty(),
               "the appended Line starts at the third's end");
    }
    // the opened file, then two steps a Line and the append
    expect(history.size() == 8, "one step per lead and the append");
    if (history.size() == 8) {
        expect(history[1] == QStringLiteral("Changing time on audio spectrum, active line 1"), "lead-in, Line 1");
        expect(history[2] == QStringLiteral("Changing time on audio spectrum, active line 1"), "lead-out, Line 1");
        expect(history[5] == QStringLiteral("Changing time on audio spectrum, active line 3"), "lead-in, Line 3");
        expect(history[7] == QStringLiteral("Adding a new line, active line 3"), "Enter on the last Line");
    }
    expect(application.editor().text() == QStringLiteral("Three"), "Z, Z and Right end on the third Line");
    expect(application.audio().selectionStart() == 4800, "the box follows the active Line");
    expect(!application.audio().modified(), "nothing left to commit");
    return failures == 0 ? 0 : 1;
}
