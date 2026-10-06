// G1 workflow through the real UI (Spix): range selection in the Grid with
// Shift and the keyboard, Select all, and back to one Line, read from the
// footer's selection status.

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

protected:
    std::string status() { return getStringProperty("mainWindow/selectionStatus", "text"); }
    void key(int code, unsigned mods = spix::KeyModifiers::None)
    {
        enterKey("mainWindow/editingGrid", code, mods);
        wait(200ms);
    }
    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            mouseClick("mainWindow/editingGrid");
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            key(Qt::Key_Home);
            observed.push_back(status()); // 0
            key(Qt::Key_Down, spix::KeyModifiers::Shift);
            key(Qt::Key_Down, spix::KeyModifiers::Shift);
            observed.push_back(status()); // 1
            observed.push_back(getStringProperty("mainWindow/lineText", "text")); // 2: the active Line moved
            key(Qt::Key_A, spix::KeyModifiers::Control);
            observed.push_back(status()); // 3
            key(Qt::Key_Down);
            observed.push_back(status()); // 4
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
    const QString path = dir.filePath(QStringLiteral("five.ass"));
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        for (const char *t : {"a", "b", "c", "d", "e"})
            f.write(QByteArray("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,") + t + "\n");
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
    expect(o.size() == 5, "every step observed");
    if (o.size() == 5) {
        expect(o[0] == "1 Line selected", "Home selects one Line");
        expect(o[1] == "3 Lines selected", "Shift+Down twice extends to three");
        expect(o[2] == "c", "the active Line moved with the range");
        expect(o[3] == "5 Lines selected", "Ctrl+A selects all");
        expect(o[4] == "1 Line selected", "a plain arrow returns to one Line");
    }
    return failures == 0 ? 0 : 1;
}
