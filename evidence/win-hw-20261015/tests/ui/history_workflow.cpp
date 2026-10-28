// G10 workflow through the real UI (Spix): two committed edits, the History
// window (Ctrl+Shift+H) lists them with the current step, and Set returns the
// Line to the opened text.

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
    std::string text() { return getStringProperty("mainWindow/lineText", "text"); }
    void edit(const std::string &suffix)
    {
        invokeMethod("mainWindow/lineText", "forceActiveFocus", {});
        enterKey("mainWindow/lineText", Qt::Key_End, spix::KeyModifiers::None);
        inputText("mainWindow/lineText", suffix);
        wait(300ms);
        enterKey("mainWindow/lineText", Qt::Key_Return, spix::KeyModifiers::Control);
        wait(300ms);
    }
    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            enterKey("mainWindow/editingGrid", Qt::Key_Home, spix::KeyModifiers::None);
            wait(200ms);
            edit(" one");
            edit(" two");
            observed.push_back(text()); // 0
            enterKey("mainWindow/lineText", Qt::Key_H, spix::KeyModifiers::Control | spix::KeyModifiers::Shift);
            if (waitForItem("historyWindow/historySet", 5s)) {
                observed.push_back(getStringProperty("historyWindow", "title")); // 1
                observed.push_back(getStringProperty("historyWindow/historyList", "currentIndex")); // 2
                invokeMethod("historyWindow/historyList", "forceActiveFocus", {});
                enterKey("historyWindow/historyList", Qt::Key_Up, spix::KeyModifiers::None);
                enterKey("historyWindow/historyList", Qt::Key_Up, spix::KeyModifiers::None);
                wait(200ms);
                mouseClick("historyWindow/historyOk");
                wait(500ms);
                observed.push_back(text()); // 3
            }
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        quit();
    }
};

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("history.ass"));
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n"
                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Door\n");
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
    Workflow test;
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();
    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 4, "every step observed");
    if (o.size() == 4) {
        expect(o[0] == "Gate one two", "two committed edits");
        expect(o[1] == "History (3 elements)", "the window counts the steps");
        expect(o[2] == "2", "the current step is selected");
        expect(o[3] == "Gate", "OK on the first step returns to the opened text");
    }
    return failures == 0 ? 0 : 1;
}
