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
    hikari::app::Application *application = nullptr;

protected:
    std::string status() { return getStringProperty("mainWindow/referenceMatchStatus", "text"); }
    std::string referenceRow()
    {
        // Spix runs commands in order on the GUI thread: this read waits for the earlier ones.
        (void)getStringProperty("mainWindow", "title");
        std::string out = "none";
        QMetaObject::invokeMethod(qApp, [&] {
            const auto reference = application->workspace().reference();
            if (!reference)
                return;
            const auto &session = *application->files().session(*reference);
            const auto lines = session.document().lines();
            for (std::size_t r = 0; r < lines.size(); ++r)
                if (session.selection().active == lines[r]->id)
                    out = std::to_string(r);
        }, Qt::BlockingQueuedConnection);
        return out;
    }
    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            enterKey("mainWindow/editingGrid", Qt::Key_Q, spix::KeyModifiers::Control);
            wait(500ms);
            observed.push_back(waitForItem("mainWindow/referenceMatchStatus", 5s) ? status() : "no tray");
            observed.push_back(referenceRow());
            mouseClick("mainWindow/referenceNextMatch");
            wait(300ms);
            observed.push_back(status());
            observed.push_back(referenceRow());
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            enterKey("mainWindow/editingGrid", Qt::Key_Down, spix::KeyModifiers::None);
            wait(300ms);
            observed.push_back(status());
            observed.push_back(referenceRow());
            mouseClick("mainWindow/referenceLinked");
            wait(300ms);
            observed.push_back(status());
            invokeMethod("mainWindow/referenceGrid", "forceActiveFocus", {});
            enterKey("mainWindow/referenceGrid", Qt::Key_Down, spix::KeyModifiers::None);
            wait(300ms);
            observed.push_back(referenceRow());
            enterKey("mainWindow/referenceGrid", Qt::Key_V, spix::KeyModifiers::Control);
            wait(300ms);
            observed.push_back(getStringProperty("mainWindow/referencePanel", "title"));
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            enterKey("mainWindow/editingGrid", Qt::Key_Down, spix::KeyModifiers::None);
            wait(300ms);
            observed.push_back(referenceRow());
            mouseClick("mainWindow/referenceMatchStatus", spix::MouseButtons::Right);
            wait(300ms);
            mouseClick("mainWindow/panelOptionsClose");
            wait(500ms);
            observed.push_back(referenceRow());
            observed.push_back(existsAndVisible("mainWindow/documentTab1") ? "tab kept" : "tab gone");
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        quit();
    }
};

void write(const QString &path, const char *events)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QByteArray("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                       "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n") +
            events);
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    const QString edit = dir.filePath(QStringLiteral("edit.ass")), ref = dir.filePath(QStringLiteral("ref.ass"));
    write(edit, "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,e1\n"
                "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,e2\n"
                "Dialogue: 0,0:00:08.00,0:00:09.00,Default,,0,0,0,,e3\n");
    write(ref, "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,r0\n"
               "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,r1\n"
               "Dialogue: 0,0:00:01.50,0:00:02.50,Default,,0,0,0,,r2\n"
               "Dialogue: 0,0:00:02.50,0:00:03.50,Default,,0,0,0,,r3\n"
               "Dialogue: 0,0:00:10.00,0:00:11.00,Default,,0,0,0,,r4\n");
    const QByteArray editBefore = read(edit), refBefore = read(ref);
    hikari::app::Application application;
    expect(application.openFile(edit), "open edit.ass");
    expect(application.openFile(ref), "open ref.ass");
    application.selectTab(0);
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
    Workflow test;
    test.application = &application;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    app.exec();
    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 12, "every step observed");
    if (o.size() == 12) {
        expect(o[0] == "Match 1 of 2", "Ctrl+Q shows the next tab, linked, at the first of two candidates");
        expect(o[1] == "0", "the reference shows the first candidate's Line");
        expect(o[2] == "Match 2 of 2" && o[3] == "2", "Next match shows the second candidate");
        expect(o[4] == "Match 1 of 1" && o[5] == "2", "the editing Grid's Down arrow moves the linked reference");
        expect(o[6] == "Independent navigation", "unlinking makes the tray independent");
        expect(o[7] == "3", "the tray's Down arrow moves the reference");
        expect(o[8].find("Reference (protected, read-only): ref.ass") != std::string::npos, "the tray names the reference");
        expect(o[9] == "3", "an independent reference does not follow the editing Line");
        expect(o[10] == "none", "Close reference ends the reference");
        expect(o[11] == "tab kept", "the previewed tab stays");
    }
    expect(read(edit) == editBefore && read(ref) == refBefore, "neither file was written");
    return failures == 0 ? 0 : 1;
}
