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
    std::string title() { return getStringProperty("mainWindow", "title"); }
    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            observed.push_back(title());
            mouseClick("mainWindow/fileMenuBarItem");
            wait(300ms);
            mouseClick("mainWindow/settingsMenuItem");
            if (waitForItem("settingsDialog/settingsPages", 5s)) {
                // MUST KILL: magic page index 10 is the Hotkeys page
                setStringProperty("settingsDialog/settingsPages", "currentIndex", "10");
                wait(300ms);
                observed.push_back(getStringProperty("settingsDialog/hotkeyList", "selectedRow"));
                mouseClick("settingsDialog/hotkeyMap");
                for (int i = 0; i < 100 && getStringProperty("settingsDialog/settingsHotkeyMapping", "opened") != "true"; ++i)
                    wait(100ms);
                if (waitForItem("settingsDialog/hotkeyMappingText", 5s)) {
                    observed.push_back(getStringProperty("settingsDialog/settingsHotkeyMapping", "title"));
                    enterKey("settingsDialog/hotkeyMappingKeys", Qt::Key_J,
                             spix::KeyModifiers::Control | spix::KeyModifiers::Shift);
                    wait(300ms);
                    observed.push_back(getStringProperty("settingsDialog/settingsHotkeyMapping", "opened"));
                    mouseClick("settingsDialog/settingsOk");
                    wait(500ms);
                    mouseClick("mainWindow/editingGrid");
                    enterKey("mainWindow/editingGrid", Qt::Key_W, spix::KeyModifiers::Control);
                    wait(500ms);
                    observed.push_back(title());
                    enterKey("mainWindow/editingGrid", Qt::Key_J, spix::KeyModifiers::Control | spix::KeyModifiers::Shift);
                    for (int i = 0; i < 50 && title() != "HikariSub"; ++i)
                        wait(100ms);
                    observed.push_back(title());
                }
            }
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        quit();
    }
};

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("shortcuts.ass"));
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return 2;
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n");
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
    expect(o.size() == 6, "every step observed");
    if (o.size() == 6) {
        expect(o[0] == "shortcuts.ass - HikariSub", "the file is the editing target");
        expect(o[1] == "0", "the list starts on its first row");
        expect(o[2] == "Keys for \"Close current tab\"", "the capture names the action");
        expect(o[3] == "false", "a key with modifiers closes the capture");
        expect(o[4] == "shortcuts.ass - HikariSub", "the old keys no longer close the tab");
        expect(o[5] == "HikariSub", "the new keys close it");
    }
    expect(application.hotkeys().accelOf(QStringLiteral("GLOBAL_CLOSE_PAGE"), 0) == QStringLiteral("Ctrl-Shift-J"),
           "the binding is installed");
    return failures == 0 ? 0 : 1;
}
