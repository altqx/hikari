#include "hikari/app/application.h"
#include "hikari/application/hotkeys.h"

#include "docking.h"
#include "line_table_model.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "theme.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QtQuickTest/quicktest.h>
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

api::Caller reviewedAi()
{
    return api::Caller{.id = "ai", .label = "AI", .reviewed = true};
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
    api::Result call(std::string_view operation, const char *params, const api::Caller &caller = assistant())
    {
        std::optional<api::Result> answer;
        hikariApi().call(caller, operation, *api::Json::parse(params), [&](api::Result r) { answer = std::move(r); });
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
    bool aiWrites(core::LineId line, const char *text)
    {
        const auto edit = call("document.apply",
                               QStringLiteral(R"({"revision": %1, "name": "Tightened", "edits": [
            {"op": "set_line", "line": %2, "fields": {"text": "%3"}}]})")
                                   .arg(session().revision())
                                   .arg(line.value)
                                   .arg(QString::fromUtf8(text))
                                   .toUtf8()
                                   .constData(),
                               reviewedAi());
        if (!edit)
            qWarning() << edit.error().message.c_str();
        return bool(edit);
    }
    bool select(core::LineId line)
    {
        return bool(call("selection.set", QStringLiteral(R"({"selected": [%1]})")
                                              .arg(line.value)
                                              .toUtf8()
                                              .constData()));
    }
    int reviewRoleAt(int row) const
    {
        QAbstractItemModel *lines = application->shell().lines();
        return lines->index(row, 0).data(ui::LineTableModel::ReviewRole).toInt();
    }
    std::u8string textAt(std::size_t row) const { return session().document().lines()[row]->text; }
    bool reviewMenuShown()
    {
        return shell(QStringLiteral("(() => { for (let i = 0; i < editMenu.count; ++i) { const it = editMenu.itemAt(i);"
                                    " if (it && it.subMenu && it.subMenu.objectName === 'reviewMenu') return true }"
                                    " return false })()"))
            .toBool();
    }
    void shoot(const QString &name)
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            return;
        QVERIFY(QDir().mkpath(out));
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        for (const auto code : ui::theme::kCodes) {
            ui::theme::preview({{QStringLiteral("appearance.theme"), ui::theme::codeName(code)},
                                {QStringLiteral("appearance.followSystem"), false}});
            QQuickTest::qWaitForPolish(window());
            QTest::qWait(200);
            QVERIFY(window()->grabWindow().save(out + QLatin1Char('/') + name + QLatin1Char('-') +
                                                ui::theme::codeName(code) + QStringLiteral(".png")));
        }
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

    void anAiWriteWaitsInTheGridAndTheLineEditorUntilKept()
    {
        const auto first = session().document().lines().front()->id;
        QVERIFY(aiWrites(first, "changed"));
        QCOMPARE(application->review()->waiting(), 1);
        QCOMPARE(reviewRoleAt(0), int(ui::LineTableModel::ReviewState::Waiting));
        QCOMPARE(reviewRoleAt(1), int(ui::LineTableModel::ReviewState::None));
        QVERIFY(application->review()->isAiStep(int(session().historySize()) - 1));

        QVERIFY(select(first));
        QTRY_VERIFY(shell(QStringLiteral("reviewStrip.visible")).toBool());
        const QVariantMap current = application->review()->current();
        QCOMPARE(current.value(QStringLiteral("caller")).toString(), QStringLiteral("AI"));
        QCOMPARE(current.value(QStringLiteral("name")).toString(), QStringLiteral("Tightened"));
        QCOMPARE(current.value(QStringLiteral("count")).toInt(), 1);
        QVERIFY(current.value(QStringLiteral("before")).toString().contains(QStringLiteral("<s ")));
        QCOMPARE(shell(QStringLiteral("statusStrip.reviewText")).toString(), QStringLiteral("1 AI change(s) to review"));
        QVERIFY(shell(QStringLiteral("root.showPanel(reviewDock), reviewDock.isOpen")).toBool());
        QTRY_VERIFY(shell(QStringLiteral("reviewLoader.item !== null && reviewLoader.item.here.length === 1")).toBool());
        shoot(QStringLiteral("ai2-review"));

        const std::size_t steps = session().historySize();
        QVERIFY(shell(QStringLiteral("keepAiChangeAction.trigger(), true")).toBool());
        QCOMPARE(application->review()->waiting(), 0);
        QCOMPARE(session().historySize(), steps);
        QCOMPARE(reviewRoleAt(0), int(ui::LineTableModel::ReviewState::None));
        QTRY_VERIFY(!shell(QStringLiteral("reviewStrip.visible")).toBool());
        QCOMPARE(textAt(0), std::u8string(u8"changed"));
    }

    void undoPutsTheLineBackAsOneStepAndAnEditSinceAsksFirst()
    {
        const auto first = session().document().lines().front()->id;
        QVERIFY(aiWrites(first, "changed"));
        QVERIFY(select(first));
        QVERIFY(application->review()->undoLine(false));
        QCOMPARE(textAt(0), std::u8string(u8"first"));
        QCOMPARE(session().history().back().name, std::string("Undo AI change to Line 1"));
        QCOMPARE(application->review()->waiting(), 0);

        QVERIFY(aiWrites(first, "again"));
        const auto revision = session().revision();
        QVERIFY(call("document.apply", QStringLiteral(R"({"revision": %1, "name": "Mine", "edits": [
            {"op": "set_line", "line": %2, "fields": {"text": "mine"}}]})")
                                           .arg(revision)
                                           .arg(first.value)
                                           .toUtf8()
                                           .constData()));
        QCOMPARE(reviewRoleAt(0), int(ui::LineTableModel::ReviewState::EditedSince));
        QCOMPARE(application->review()->lineEditedSince(), 1);
        QVERIFY(shell(QStringLiteral("undoAiChangeAction.trigger(), true")).toBool());
        QTRY_VERIFY(shell(QStringLiteral("reviewStrip.confirming")).toBool());
        QCOMPARE(textAt(0), std::u8string(u8"mine"));
        QVERIFY(application->review()->undoLine(true));
        QCOMPARE(textAt(0), std::u8string(u8"first"));
    }

    void restoringACheckpointUndoesTheTurnKeptChangesToo()
    {
        const auto lines = session().document().lines();
        const auto first = lines[0]->id, second = lines[1]->id;
        QVERIFY(aiWrites(first, "one"));
        QVERIFY(aiWrites(second, "two"));
        QVERIFY(select(first));
        application->review()->keepLine();
        QCOMPARE(application->review()->waiting(), 1);
        const QVariantList turns = application->review()->turns();
        QCOMPARE(turns.size(), 1);
        const qulonglong turn = turns.front().toMap().value(QStringLiteral("turn")).toULongLong();
        const QVariantList checkpoint = application->review()->checkpoint(turn);
        QCOMPARE(checkpoint.size(), 1);
        QCOMPARE(checkpoint.front().toMap().value(QStringLiteral("lines")).toInt(), 2);
        QVERIFY(application->review()->turnBefore(int(session().historySize()) - 2) == 1);

        QVERIFY(application->review()->restore(turn, false));
        QCOMPARE(textAt(0), std::u8string(u8"first"));
        QCOMPARE(textAt(1), std::u8string(u8"second"));
        QCOMPARE(session().history().back().name, std::string("Restore checkpoint before turn 1"));
    }

    void withAiOffTheReviewCommandsAndPanelAreGone()
    {
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_KEEP_AI_CHANGE"), application::GlobalHotkey),
                 QStringLiteral("Ctrl-Shift-Enter"));
        QVERIFY(reviewMenuShown());
        QVERIFY(shell(QStringLiteral("root.builtInDocks.indexOf(reviewDock) >= 0")).toBool());

        application->settingsStore()->set(app::AiFeatures::kOffSetting, true);
        QVERIFY(!application->review()->on());
        QVERIFY(application->hotkeys().accelOf(QStringLiteral("GLOBAL_KEEP_AI_CHANGE"), application::GlobalHotkey).isEmpty());
        for (const QVariant &shortcut : application->hotkeys().globalShortcuts()) {
            const QString symbol = shortcut.toMap().value(QStringLiteral("symbol")).toString();
            QVERIFY2(!application::aiCommand(application::hotkeyIdOf(symbol.toStdString())), qPrintable(symbol));
        }
        application->hotkeys().beginOptions();
        for (const QVariant &row : application->hotkeys().rows())
            QVERIFY(!row.toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("AI change")));
        QTRY_VERIFY(!reviewMenuShown());
        QVERIFY(shell(QStringLiteral("root.builtInDocks.indexOf(reviewDock) < 0")).toBool());

        const auto first = session().document().lines().front()->id;
        QVERIFY(aiWrites(first, "changed"));
        QCOMPARE(reviewRoleAt(0), int(ui::LineTableModel::ReviewState::None));

        application->settingsStore()->set(app::AiFeatures::kOffSetting, false);
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_KEEP_AI_CHANGE"), application::GlobalHotkey),
                 QStringLiteral("Ctrl-Shift-Enter"));
        QTRY_VERIFY(reviewMenuShown());
        QCOMPARE(reviewRoleAt(0), int(ui::LineTableModel::ReviewState::Waiting));
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

    void grantedFilesAreReadAndWrittenOnDisk()
    {
        const QString path = root.filePath(QStringLiteral("notes.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("before");
        file.close();
        QCOMPARE(hikariApi().grantFile("test.ai", path.toStdString(), true), std::string("f1"));
        const auto read = call("files.read", R"({"file": "f1"})");
        QVERIFY(read);
        QCOMPARE(read->find("data")->asString(), std::string("before"));
        QVERIFY(call("files.write", R"({"file": "f1", "data": "after"})"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("after"));
    }

    void withoutMediaVideoAndAudioAreClosed()
    {
        const auto video = call("video.info", "{}");
        QVERIFY(video);
        QVERIFY(*video == *api::Json::parse(R"({"open": false})"));
        const auto audio = call("audio.info", "{}");
        QVERIFY(audio);
        QVERIFY(*audio == *api::Json::parse(R"({"open": false})"));
    }

    void aUserCommandIsHeard()
    {
        shell(QStringLiteral("root.runItem(root.globalActions().GLOBAL_ABOUT)"));
        QVERIFY(heardEvent("command.ran"));
    }
};

QTEST_MAIN(ApiHostTest)
#include "api_host_tests.moc"
