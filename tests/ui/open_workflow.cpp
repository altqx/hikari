// P2 workflow through the real UI (Spix): opening other subtitles over
// unsaved work shows the close review, Cancel keeps the work and Discard all
// opens the file; the recent list then names both files; a file changed by
// another program offers a reload, and Yes reloads it.

#include "hikari/app/application.h"
#include "docking.h"

#include <QDateTime>
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

void write(const QString &path, const char *text)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QByteArray("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,") + text + "\n");
    f.close();
    // Newer than anything recorded at open, whatever the filesystem's resolution.
    QFile touch(path);
    touch.open(QIODevice::ReadWrite);
    touch.setFileTime(QDateTime::currentDateTime().addSecs(30), QFileDevice::FileModificationTime);
}

class Workflow : public spix::TestServer {
public:
    std::vector<std::string> observed;
    hikari::app::Application *application = nullptr;

protected:
    // The window title names the editing target ("<name> - HikariSub").
    std::string targets() { return getStringProperty("mainWindow", "title"); }
    std::string lineText() { return getStringProperty("mainWindow/lineText", "text"); }
    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            invokeMethod("mainWindow/lineText", "forceActiveFocus", {});
            enterKey("mainWindow/lineText", Qt::Key_End, spix::KeyModifiers::None);
            inputText("mainWindow/lineText", " edited");
            wait(300ms);
            enterKey("mainWindow/lineText", Qt::Key_Return, spix::KeyModifiers::Control);
            wait(300ms);
            invokeMethod("mainWindow", "openSubtitles", {g_second.toStdString()});
            observed.push_back(waitForItem("closeReview/closeCancel", 5s) ? "review" : "no review"); // 0
            mouseClick("closeReview/closeCancel");
            wait(300ms);
            observed.push_back(targets()); // 1: unchanged
            invokeMethod("mainWindow", "openSubtitles", {g_second.toStdString()});
            wait(500ms);
            mouseClick("closeReview/closeDiscardAll");
            wait(500ms);
            observed.push_back(targets()); // 2: the other file
            std::string recent;
            QMetaObject::invokeMethod(
                application,
                [&] {
                    for (const auto &row : application->recentSubtitles())
                        recent += row.toMap().value(QStringLiteral("label")).toString().toStdString() + ";";
                },
                Qt::BlockingQueuedConnection);
            observed.push_back(recent); // 3
            write(g_second, "Changed");
            invokeMethod("mainWindow", "checkExternalChange", {});
            observed.push_back(waitForItem("reloadPrompt/reloadYes", 5s) ? "prompt" : "no prompt"); // 4
            mouseClick("reloadPrompt/reloadYes");
            wait(500ms);
            observed.push_back(lineText()); // 5
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
    const QString first = dir.filePath(QStringLiteral("first.ass"));
    g_second = dir.filePath(QStringLiteral("second.ass"));
    write(first, "Gate");
    write(g_second, "Door");
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
    const auto &o = test.observed;
    for (std::size_t i = 0; i < o.size(); ++i)
        std::printf("observed[%zu] = %s\n", i, o[i].c_str());
    expect(o.size() == 6, "every step observed");
    if (o.size() == 6) {
        expect(o[0] == "review", "opening over unsaved work shows the review");
        expect(o[1].rfind("first.ass - ", 0) == 0, "Cancel keeps the Document");
        expect(o[2].rfind("second.ass - ", 0) == 0, "Discard all opens the other file");
        expect(o[3] == "1 second.ass;2 first.ass;", "the recent list names both files, latest first");
        expect(o[4] == "prompt", "a file changed by another program offers a reload");
        expect(o[5] == "Changed", "Yes reloads it");
    }
    return failures == 0 ? 0 : 1;
}
