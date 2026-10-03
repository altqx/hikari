// G2 workflow through the real UI (Spix): Ctrl+C and Ctrl+V in the Grid paste
// a copy of the active Line before the next one, Ctrl+X cuts it again, and
// Paste columns with only Text chosen replaces the selected Line's text.

#include "hikari/app/application.h"
#include "docking.h"

#include <QClipboard>
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
    void key(int code, unsigned mods = spix::KeyModifiers::None)
    {
        enterKey("mainWindow/editingGrid", code, mods);
        wait(200ms);
    }
    std::string texts()
    {
        // A Spix getter first: it runs after the queued input, so the read
        // below sees its effects.
        (void)getStringProperty("mainWindow/selectionStatus", "text");
        std::string out;
        QMetaObject::invokeMethod(
            application,
            [&] {
                const auto target = application->workspace().editingTarget();
                for (const auto *l : application->files().session(*target)->document().lines())
                    out += std::string(l->text.begin(), l->text.end()) + ";";
            },
            Qt::BlockingQueuedConnection);
        return out;
    }
    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            mouseClick("mainWindow/editingGrid");
            invokeMethod("mainWindow/editingGrid", "forceActiveFocus", {});
            key(Qt::Key_Home);
            key(Qt::Key_C, spix::KeyModifiers::Control);
            key(Qt::Key_Down);
            key(Qt::Key_V, spix::KeyModifiers::Control);
            observed.push_back(texts()); // 0
            observed.push_back(getStringProperty("mainWindow/selectionStatus", "text")); // 1
            key(Qt::Key_X, spix::KeyModifiers::Control);
            observed.push_back(texts()); // 2
            key(Qt::Key_End);
            QMetaObject::invokeMethod(
                application,
                [] {
                    QGuiApplication::clipboard()->setText(
                        QStringLiteral("Dialogue: 9,0:00:09.00,0:00:10.00,Other,,0,0,0,,pasted text"));
                },
                Qt::BlockingQueuedConnection);
            (void)getStringProperty("mainWindow/selectionStatus", "text");
            invokeMethod("columnsWindow", "choose", {true});
            observed.push_back(waitForItem("columnsWindow/column9", 5s) ? "columns" : "no columns"); // 3
            mouseClick("columnsWindow/column9"); // Text
            mouseClick("columnsWindow/columnsOk");
            wait(300ms);
            observed.push_back(texts()); // 4
            observed.push_back(getStringProperty("mainWindow/lineText", "text")); // 5
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        // Discard the work through the quit review so the window can close.
        QMetaObject::invokeMethod(
            application,
            [this] {
                QVariantList choices;
                for (const QVariant &row : application->reviewClose(QStringLiteral("quit")))
                    choices << QVariantMap{{QStringLiteral("id"), row.toMap().value(QStringLiteral("id"))},
                                           {QStringLiteral("save"), false}};
                application->resolveClose(choices);
            },
            Qt::BlockingQueuedConnection);
        quit();
    }
};

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("three.ass"));
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return 2;
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        for (const char *t : {"a", "b", "c"})
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
        expect(o[0] == "a;a;b;c;", "Ctrl+C then Ctrl+V pastes the copy before the next Line");
        expect(o[1] == "1 Line selected", "the pasted Line is selected");
        expect(o[2] == "a;b;c;", "Ctrl+X cuts it");
        expect(o[3] == "columns", "Paste columns asks which columns");
        expect(o[4] == "a;b;pasted text;", "only the Text column is pasted");
        expect(o[5] == "pasted text", "the editor shows the pasted text");
    }
    return failures == 0 ? 0 : 1;
}
