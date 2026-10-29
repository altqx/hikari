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

protected:
    std::string targets() { return getStringProperty("mainWindow", "title"); }
    void key(int key)
    {
        enterKey("mainWindow/editingGrid", key, spix::KeyModifiers::Control);
        wait(300ms);
    }
    void typeAndCommit(const std::string &text)
    {
        invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
        enterKey("mainWindow/editingGrid", Qt::Key_Home, spix::KeyModifiers::None);
        wait(200ms);
        invokeMethod("mainWindow/lineText", "forceActiveFocus", {});
        enterKey("mainWindow/lineText", Qt::Key_End, spix::KeyModifiers::None);
        inputText("mainWindow/lineText", text);
        wait(300ms);
        enterKey("mainWindow/lineText", Qt::Key_Return, spix::KeyModifiers::Control);
        wait(300ms);
    }
    void executeTest() override
    {
        if (waitForItem("mainWindow/documentTab2", 5s)) {
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            observed.push_back(targets());
            key(Qt::Key_PageDown);
            observed.push_back(targets());
            key(Qt::Key_PageDown);
            key(Qt::Key_PageDown);
            observed.push_back(targets());
            key(Qt::Key_PageUp);
            observed.push_back(targets());
            mouseClick("mainWindow/documentTab1");
            wait(300ms);
            observed.push_back(targets());
            typeAndCommit(" edited");
            observed.push_back(getStringProperty("mainWindow/documentTab1", "text"));
            // Not mouseClick: under Xvfb the close question opens under spix's pointer and takes the same click.
            invokeMethod("mainWindow/documentTabClose1", "clicked", {});
            observed.push_back(waitForItem("closeQuestion/messageBoxChoice_cancel", 5s) ? "review" : "no review");
            mouseClick("closeQuestion/messageBoxChoice_cancel");
            wait(300ms);
            observed.push_back(targets());
            enterKey("mainWindow/lineText", Qt::Key_W, spix::KeyModifiers::Control);
            waitForItem("closeQuestion/messageBoxChoice_discard", 5s);
            mouseClick("closeQuestion/messageBoxChoice_discard");
            wait(500ms);
            observed.push_back(targets());
            observed.push_back(existsAndVisible("mainWindow/documentTab2") ? "3 tabs" : "2 tabs");
            // Under Xvfb no window manager reactivates the main window after the review window.
            invokeMethod("mainWindow", "requestActivate", {});
            wait(300ms);
            key(Qt::Key_T);
            observed.push_back(targets());
            observed.push_back(getStringProperty("mainWindow/documentTab2", "text"));
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        quit();
    }
};

void write(const QString &path, const char *text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QByteArray("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,") + text + "\n");
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    QByteArray before;
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    hikari::app::Application application;
    for (const char *name : {"one.ass", "two.ass", "three.ass"}) {
        write(dir.filePath(QLatin1String(name)), name);
        expect(application.openFile(dir.filePath(QLatin1String(name))), "open");
    }
    {
        QFile f(dir.filePath(QStringLiteral("two.ass")));
        if (f.open(QIODevice::ReadOnly))
            before = f.readAll();
    }
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
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    app.exec();
    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 12, "every step observed");
    if (o.size() == 12) {
        auto editing = [](const std::string &s, const char *name) { return s.rfind(name, 0) == 0; };
        expect(editing(o[0], "one.ass"), "the first tab is active");
        expect(editing(o[1], "two.ass"), "Ctrl+PgDown shows the next tab");
        expect(editing(o[2], "one.ass"), "past the last it wraps to the first");
        expect(editing(o[3], "three.ass"), "Ctrl+PgUp before the first wraps to the last");
        expect(editing(o[4], "two.ass"), "a click shows a tab");
        expect(o[5] == "1*two.ass", "a modified tab shows its history step and *");
        expect(o[6] == "review", "the close mark of a modified tab reviews it");
        expect(editing(o[7], "two.ass"), "Cancel keeps the tab");
        expect(editing(o[8], "three.ass"), "after Ctrl+W and Discard all the next tab shows");
        expect(o[9] == "2 tabs", "the closed tab is gone");
        expect(editing(o[10], "Untitled"), "Ctrl+T shows a new Untitled tab");
        expect(o[11] == "Untitled", "the new tab is the last");
    }
    QFile f(dir.filePath(QStringLiteral("two.ass")));
    expect(f.open(QIODevice::ReadOnly) && f.readAll() == before, "the discarded tab was not written");
    return failures == 0 ? 0 : 1;
}
