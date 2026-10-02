// V2 workflow through the real UI (Spix): open a copied file, edit the first
// Line in the Line editor, commit with Enter, Undo and Redo, save through the
// File menu, then reopen it. Every unrelated byte of the file must survive.

#include "hikari/app/application.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QDeadlineTimer>
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
    std::atomic<bool> finished{false};

protected:
    std::string text() { return getStringProperty("mainWindow/lineText", "text"); }
    void key(int code, unsigned mods = spix::KeyModifiers::None)
    {
        enterKey("mainWindow/lineText", code, mods);
        wait(200ms);
    }

    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            mouseClick("mainWindow/editingGrid");
            enterKey("mainWindow/editingGrid", Qt::Key_Home, spix::KeyModifiers::None);
            wait(200ms);
            observed.push_back(text()); // 0: the first Line, shown with tags hidden
            mouseClick("mainWindow/lineText", spix::Point(0.98, 0.1)); // caret after the text
            inputText("mainWindow/lineText", " edited");
            wait(300ms);
            observed.push_back(text()); // 1: the draft
            key(Qt::Key_Return);
            observed.push_back(text()); // 2: committed and advanced to the next Line
            key(Qt::Key_Z, spix::KeyModifiers::Control);
            observed.push_back(text()); // 3: Undo restores the first Line's committed text
            key(Qt::Key_Y, spix::KeyModifiers::Control);
            observed.push_back(text()); // 4: Redo
            mouseClick("mainWindow/fileMenuBarItem");
            wait(300ms);
            mouseClick("mainWindow/saveMenuItem");
            wait(800ms);
            observed.push_back(getStringProperty("mainWindow/saveStatus", "text")); // 5
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/editor-workflow.png");
        finished = true;
        quit();
    }
};

QByteArray readAll(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("copy.ass"));
    QFile::copy(QStringLiteral(HIKARI_FIXTURE_DIR "/inputs/unknown-sections.ass"), path);
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    const QByteArray original = readAll(path);

    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    {
        hikari::app::Application application;
        expect(application.openFile(path), "open the copied file");
        QQmlApplicationEngine engine;
        engine.setInitialProperties(application.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        if (engine.rootObjects().isEmpty())
            return 2;
        // Under a real X server without a window manager the window is not
        // exposed or active at once; Spix posts input to it, so wait first.
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->requestActivate();
        QDeadlineTimer deadline(10'000);
        while (!(window->isExposed() && window->isActive()) && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expect(window->isExposed(), "the window is exposed");
        Workflow test;
        spix::QtQmlBot bot;
        bot.runTestServer(test);
        app.exec();
        application.waitForWrites();

        const auto &o = test.observed;
        for (std::size_t i = 0; i < o.size(); ++i)
            std::printf("observed[%zu] = %s\n", i, o[i].c_str());
        expect(o.size() == 6, "every step observed");
        if (o.size() == 6) {
            expect(o[0] == "Gate", "the first Line is shown");
            expect(o[1] == "Gate edited", "typing goes into the draft");
            expect(o[2] != "Gate edited", "Enter commits and advances");
            expect(o[3] == "Gate", "Undo reverts the committed edit");
            expect(o[4] == "Gate edited", "Redo restores it");
            expect(o[5] == "Saved", "Save is acknowledged");
        }
        expect(!application.editor().dirty(), "the Document is clean after saving");
    }

    QByteArray expected = original;
    expected.replace(",,Gate\r\n", ",,Gate edited\r\n"); // the fixture uses CRLF
    expect(readAll(path) == expected, "only the edited Line changed on disk");

    // Reopen in a fresh composition.
    hikari::app::Application reopened;
    expect(reopened.openFile(path), "reopen");
    const auto target = reopened.workspace().editingTarget();
    const auto lines = reopened.files().session(*target)->document().lines();
    expect(!lines.empty() && lines.front()->text == u8"Gate edited", "the reopened file has the edit");
    return failures == 0 ? 0 : 1;
}
