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

QString g_second;

class Workflow : public spix::TestServer {
public:
    std::vector<std::string> observed;
    hikari::app::Application *application = nullptr;

protected:
    std::string targets() { return getStringProperty("mainWindow", "title"); }
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
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            typeAndCommit(" edited");
            enterKey("mainWindow/lineText", Qt::Key_W, spix::KeyModifiers::Control);
            observed.push_back(waitForItem("closeQuestion/messageBoxChoice_cancel", 5s) ? "review" : "no review");
            mouseClick("closeQuestion/messageBoxChoice_cancel");
            wait(300ms);
            observed.push_back(targets());
            enterKey("mainWindow/lineText", Qt::Key_W, spix::KeyModifiers::Control);
            wait(500ms);
            mouseClick("closeQuestion/messageBoxChoice_discard");
            wait(500ms);
            observed.push_back(targets());
            QMetaObject::invokeMethod(application, [this] { application->openFile(g_second); }, Qt::BlockingQueuedConnection);
            wait(300ms);
            typeAndCommit(" saved");
            invokeMethod("mainWindow", "close", {});
            observed.push_back(waitForItem("closeQuestion/messageBoxChoice_save", 5s) ? "quit review" : "no quit review");
            mouseClick("closeQuestion/messageBoxChoice_save");
            std::string visible = "true";
            for (int i = 0; i < 50 && visible != "false"; ++i) {
                wait(200ms);
                visible = getStringProperty("mainWindow", "visible");
            }
            observed.push_back(visible);
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        quit();
    }
};

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void write(const QString &path, const char *text)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QByteArray("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,") + text + "\n");
}

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString first = dir.filePath(QStringLiteral("first.ass"));
    g_second = dir.filePath(QStringLiteral("second.ass"));
    write(first, "Gate");
    write(g_second, "Door");
    const QByteArray firstBefore = readAll(first);
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    hikari::app::Application application;
    expect(application.openFile(first), "open");
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
    application.waitForWrites();
    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 5, "every step observed");
    if (o.size() == 5) {
        expect(o[0] == "review", "Ctrl+W with unsaved work shows the review");
        expect(o[1].rfind("first.ass - ", 0) == 0, "Cancel keeps the Document open");
        expect(o[2] == "HikariSub", "Discard all closes it (no Document left)");
        expect(o[3] == "quit review", "closing the window reviews the quit");
        expect(o[4] == "false", "the window closes after Save all");
    }
    expect(readAll(first) == firstBefore, "the discarded Document was not written");
    expect(readAll(g_second).contains(",,Door saved"), "Save all wrote the other Document before quitting");
    expect(application.quitApproved(), "the quit was approved after the write");
    return failures == 0 ? 0 : 1;
}
