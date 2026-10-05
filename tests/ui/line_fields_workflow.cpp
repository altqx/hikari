// E4 workflow through the real UI (Spix): the Line editor's metadata fields.
// A Layer typed and sent with Enter, Undo and Redo of it, an Actor typed in
// its box and sent with Enter, the Comment box clicked (sent at once), then
// Save through the File menu and a reopen: the file holds each field.

#include "hikari/app/application.h"
#include "docking.h"
#include "theme.h"

#include <QDeadlineTimer>
#include <QDir>
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
    std::atomic<bool> finished{false};

protected:
    std::string property(const char *item, const char *name) { return getStringProperty(std::string("mainWindow/") + item, name); }
    void key(const char *item, int code, unsigned mods = spix::KeyModifiers::None)
    {
        enterKey(std::string("mainWindow/") + item, code, mods);
        wait(200ms);
    }
    // Clicks a field, selects its text and types over it.
    void typeInto(const char *item, const std::string &text)
    {
        mouseClick(std::string("mainWindow/") + item);
        wait(200ms);
        key(item, Qt::Key_A, spix::KeyModifiers::Control);
        inputText(std::string("mainWindow/") + item, text);
        wait(300ms);
    }

    void executeTest() override
    {
        if (waitForItem("mainWindow/editingGrid", 5s)) {
            mouseClick("mainWindow/editingGrid");
            enterKey("mainWindow/editingGrid", Qt::Key_Home, spix::KeyModifiers::None);
            wait(300ms);
            observed.push_back(property("layerField", "text")); // 0: the first Line's Layer
            typeInto("layerField", "7");
            key("layerField", Qt::Key_Return); // sent; the next Line is shown
            observed.push_back(property("lineText", "text")); // 1
            observed.push_back(property("layerField", "text")); // 2: the second Line's Layer
            invokeMethod("mainWindow/lineText", "forceActiveFocus", {});
            key("lineText", Qt::Key_Z, spix::KeyModifiers::Control);
            observed.push_back(property("lineText", "text")); // 3: Undo shows the first Line again
            observed.push_back(property("layerField", "text")); // 4: with its old Layer
            key("lineText", Qt::Key_Y, spix::KeyModifiers::Control);
            observed.push_back(property("layerField", "text")); // 5: Redo
            mouseClick("mainWindow/editingGrid");
            enterKey("mainWindow/editingGrid", Qt::Key_Down, spix::KeyModifiers::None);
            wait(300ms);
            typeInto("actorBoxText", "Narrator");
            key("actorBoxText", Qt::Key_Return); // sent; the third Line is shown
            observed.push_back(property("lineText", "text")); // 6
            mouseClick("mainWindow/commentBox"); // sent at once
            wait(300ms);
            observed.push_back(property("commentBox", "checked")); // 7
            observed.push_back(property("charsCounter", "text")); // 8: no counters on a comment
            mouseClick("mainWindow/fileMenuBarItem");
            wait(300ms);
            mouseClick("mainWindow/saveMenuItem");
            std::string status;
            for (int i = 0; i < 100; ++i) {
                wait(100ms);
                status = property("saveStatus", "text");
                if (status == "Saved")
                    break;
            }
            observed.push_back(status); // 9
        }
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        if (!getErrors().empty())
            observed.clear();
        QDir().mkpath(HIKARI_TEST_ARTIFACT_DIR);
        takeScreenshot("mainWindow", std::string(HIKARI_TEST_ARTIFACT_DIR) + "/line-fields-workflow.png");
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
    // The application's controls style (composition.cpp chooses it), not
    // the platform's: Qt's native Windows style, which the application never
    // shows, divides by zero painting offscreen.
    hikari::ui::theme::chooseControlsStyle();
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("fields.ass"));
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                "MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n"
                "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n");
    }

    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
        failures += ok ? 0 : 1;
    };
    {
        hikari::app::Application application;
        expect(application.openFile(path), "open the file");
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
        expect(window->isExposed(), "the window is exposed");
        Workflow test;
        spix::QtQmlBot bot;
        bot.runTestServer(test);
        app.exec();
        application.waitForWrites();

        const auto &o = test.observed;
        for (std::size_t i = 0; i < o.size(); ++i)
            std::printf("observed[%zu] = %s\n", i, o[i].c_str());
        expect(o.size() == 10, "every step observed");
        if (o.size() == 10) {
            expect(o[0] == "0", "the first Line's Layer is shown");
            expect(o[1] == "two", "Enter in the Layer field sends and goes on");
            expect(o[2] == "0", "the second Line's own Layer");
            expect(o[3] == "one", "Undo shows the first Line");
            expect(o[4] == "0", "Undo restores its Layer");
            expect(o[5] == "7", "Redo brings it back");
            expect(o[6] == "three", "Enter in the Actor box sends and goes on");
            expect(o[7] == "true", "the Comment box is checked");
            expect(o[8].empty(), "a comment has no counters");
            expect(o[9] == "Saved", "Save is acknowledged");
        }
    }

    const QByteArray saved = readAll(path);
    std::printf("saved:\n%s\n", saved.constData());
    expect(saved.contains("Dialogue: 7,0:00:01.00,0:00:02.00,Default,,0,0,0,,one"), "the Layer is saved");
    expect(saved.contains("Dialogue: 0,0:00:03.00,0:00:04.00,Default,Narrator,0,0,0,,two"), "the Actor is saved");
    expect(saved.contains("Comment: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three"), "the comment is saved");

    hikari::app::Application reopened;
    expect(reopened.openFile(path), "reopen");
    const auto target = reopened.workspace().editingTarget();
    const auto lines = reopened.files().session(*target)->document().lines();
    expect(lines.size() == 3 && lines[0]->layer.value == 7 && lines[1]->actor == u8"Narrator" && lines[2]->comment,
           "the reopened file has every field");
    return failures == 0 ? 0 : 1;
}
