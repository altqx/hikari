#include "hikari/app/application.h"

#include "docking.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace hikari;

namespace {

api::Caller assistant()
{
    return api::Caller{"test.ai", "Test AI", true};
}

}

class ApiHostTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    std::vector<std::string> heard;

    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    api::Api &hikariApi() const { return application->apiHost()->api(); }
    application::EditSession &session() const
    {
        return *application->files().session(*application->workspace().editingTarget());
    }
    api::Result call(std::string_view operation, const char *params)
    {
        std::optional<api::Result> answer;
        hikariApi().call(assistant(), operation, *api::Json::parse(params), [&](api::Result r) { answer = std::move(r); });
        if (!answer)
            return api::fail("test_no_reply", std::string(operation));
        return *answer;
    }
    QVariant shell(const QString &code)
    {
        QQmlExpression expression(qmlContext(window()), window(), code);
        const QVariant out = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return out;
    }
    bool heardEvent(const std::string &name) const { return std::ranges::find(heard, name) != heard.end(); }

private slots:
    void init()
    {
        app::Application::Options options;
        options.settingsFile = root.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());

        const QString path = root.filePath(QStringLiteral("episode.ass"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                   "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                   "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\n"
                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n");
        file.close();
        QVERIFY(application->openFile(path));

        heard.clear();
        QVERIFY(hikariApi().subscribe(assistant(),
                                      {"document.changed", "selection.changed", "command.ran", "panel.changed"},
                                      [this](const api::Event &event) { heard.push_back(event.name); }));
    }
    void cleanup()
    {
        engine.reset();
        application.reset();
    }

    void aWriteIsOneNamedUndoStepAndTheShellUndoesIt()
    {
        const auto first = session().document().lines().front()->id;
        const auto revision = session().revision();
        const std::size_t steps = session().historySize();
        const auto edit = call("document.apply", QStringLiteral(R"({"revision": %1, "name": "Rename", "edits": [
            {"op": "set_line", "line": %2, "fields": {"text": "changed"}}]})")
                                                     .arg(revision)
                                                     .arg(first.value)
                                                     .toUtf8()
                                                     .constData());
        QVERIFY2(edit, edit ? "" : edit.error().message.c_str());
        QCOMPARE(session().historySize(), steps + 1);
        QCOMPARE(session().history().back().name, std::string("Test AI: Rename"));
        QVERIFY(heardEvent("document.changed"));

        const auto undo = call("commands.list", R"({"query": "GLOBAL_UNDO"})");
        QVERIFY(undo);
        const api::Json &command = undo->find("commands")->asArray().at(0);
        QCOMPARE(*command.find("enabled"), api::Json(true));
        QCOMPARE(command.find("keys")->asArray().at(0).asString(), std::string("Ctrl-Z"));

        heard.clear();
        QVERIFY(call("commands.run", R"({"command": "GLOBAL_UNDO"})"));
        QCOMPARE(session().document().lines().front()->text, std::u8string(u8"first"));
        QVERIFY(heardEvent("command.ran"));
        QVERIFY(heardEvent("document.changed"));
    }

    void aStaleRevisionIsRefused()
    {
        const auto revision = session().revision();
        QVERIFY(call("document.apply", QStringLiteral(R"({"revision": %1, "name": "One", "edits": [
            {"op": "set_info", "key": "Title", "value": "one"}]})")
                                           .arg(revision)
                                           .toUtf8()
                                           .constData()));
        const auto stale = call("document.apply", QStringLiteral(R"({"revision": %1, "name": "Two", "edits": [
            {"op": "set_info", "key": "Title", "value": "two"}]})")
                                                      .arg(revision)
                                                      .toUtf8()
                                                      .constData());
        QVERIFY(!stale);
        QCOMPARE(stale.error().code, std::string(api::errors::kStaleRevision));
    }

    void selectionMovesTheGridAndTheGridIsHeard()
    {
        const auto second = (*std::next(session().document().lines().begin()))->id;
        QVERIFY(call("selection.set",
                     QStringLiteral(R"({"selected": [%1]})").arg(second.value).toUtf8().constData()));
        QCOMPARE(session().selection().active, std::optional(second));
        QVERIFY(heardEvent("selection.changed"));

        heard.clear();
        const auto first = session().document().lines().front()->id;
        application->moveActiveLine(first.value);
        QCOMPARE(session().selection().active, std::optional(first));
        QVERIFY(heardEvent("selection.changed"));
    }

    void panelsOpenAndCloseThroughTheShell()
    {
        const auto listed = call("panels.list", "{}");
        QVERIFY(listed);
        bool grid = false;
        for (const api::Json &panel : listed->find("panels")->asArray())
            grid = grid || panel.find("panel")->asString() == "Grid";
        QVERIFY(grid);

        QVERIFY(call("panels.open", R"({"panel": "Log"})"));
        QTRY_VERIFY(shell(QStringLiteral("logDock.isOpen")).toBool());
        QVERIFY(heardEvent("panel.changed"));
        QVERIFY(call("panels.close", R"({"panel": "Log"})"));
        QTRY_VERIFY(!shell(QStringLiteral("logDock.isOpen")).toBool());

        const auto missing = call("panels.open", R"({"panel": "Nowhere"})");
        QVERIFY(!missing);
        QCOMPARE(missing.error().code, std::string(api::errors::kNotFound));
    }

    void aUserCommandIsHeard()
    {
        shell(QStringLiteral("root.runItem(root.globalActions().GLOBAL_ABOUT)"));
        QVERIFY(heardEvent("command.ran"));
    }
};

QTEST_MAIN(ApiHostTest)
#include "api_host_tests.moc"
