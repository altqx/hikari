// S4 through the composition with real Lua helpers and media: the scripts a
// Document's Script Info names ("Automation Scripts", legacy
// Automation::AddFromSubs) load when the Automation menu opens; "Run the last
// loaded script" (GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT, HikariSubFrame.cpp:916-936
// at 20d647c4) reloads a changed file and runs its first macro after
// validation, or says why not; Load script names the script in Script Info;
// keyframes and get_audio_selection answer from the video and the audio box;
// a macro's Style and Script Info edits are one undo step.

#include "hikari/app/application.h"
#include "desktop_guard.h"
#include "settings_store.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <map>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

// A script whose first macro prefixes the selected Line with `tag`.
QByteArray prefixScript(const QByteArray &tag)
{
    return "script_name = \"" + tag + "\"\n"
           "aegisub.register_macro(\"Prefix " + tag + "\", \"\", function(subs, sel)\n"
           "    local l = subs[sel[1]]\n"
           "    l.text = \"" + tag + ":\" .. l.text\n"
           "    subs[sel[1]] = l\n"
           "end)\n";
}

QByteArray assWith(const QByteArray &scripts)
{
    QByteArray out = "[Script Info]\nTitle: s4\nScriptType: v4.00+\n";
    if (!scripts.isNull())
        out += "Automation Scripts: " + scripts + "\n";
    return out + "\n[V4+ Styles]\n"
                 "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                 "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                 "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                 "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                 "10,10,10,1\n\n"
                 "[Events]\n"
                 "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                 "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n"
                 "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Second\n";
}

} // namespace

class AutomationDocumentScriptsTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString previousDir;
    std::unique_ptr<app::Application> app;

    app::Application::Options options()
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("hikari.ini"));
        o.playbackAudio = false;
        return o;
    }
    application::EditSession &session() { return *app->files().session(*app->workspace().editingTarget()); }
    std::string firstText()
    {
        const auto &t = session().document().lines().front()->text;
        return std::string(t.begin(), t.end());
    }
    QString open(const char *name, const QByteArray &scripts = QByteArray())
    {
        const QString path = dir.filePath(QLatin1String(name));
        writeFile(path, assWith(scripts));
        if (!app->openFile(path))
            return {};
        return path;
    }
    // Every loaded script has finished loading.
    bool settled()
    {
        QDeadlineTimer deadline(30'000);
        while (!deadline.hasExpired()) {
            const auto scripts = app->automation().scripts();
            if (std::ranges::none_of(scripts, [](const auto &s) {
                    return s.state == application::ScriptStatus::State::Loading;
                }))
                return true;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        return false;
    }
    std::uint64_t generation(const QString &path)
    {
        for (const auto &s : app->automation().scripts())
            if (s.path == path.toStdString())
                return s.generation;
        return 0;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        previousDir = QDir::currentPath();
        // Legacy reads a relative path with wxFileExists, against the working directory.
        QVERIFY(QDir::setCurrent(dir.path()));
    }
    void cleanupTestCase() { QDir::setCurrent(previousDir); }
    void init() { app = std::make_unique<app::Application>(options()); }
    void cleanup() { app.reset(); }

    // Relative, absolute and missing paths; the menu opening loads them
    // (BuildMenu's AddFromSubs); the last one's first macro runs, validated,
    // as one step; a changed file reloads first (CheckLastModified).
    void documentScriptsLoadAndTheLastRuns()
    {
        const QString absolute = QDir::toNativeSeparators(dir.filePath(QStringLiteral("abs/first.lua")));
        writeFile(absolute, prefixScript("first"));
        const QString relative = QDir::toNativeSeparators(QStringLiteral("rel/last.lua"));
        writeFile(dir.filePath(relative), prefixScript("last"));
        QVERIFY(!open("named.ass", "|" + absolute.toUtf8() + "|missing.lua|" + relative.toUtf8()).isEmpty());
        auto &automation = app->automation();
        QVERIFY(automation.documentScripts().empty()); // nothing until the menu opens or a command asks
        automation.menuOpened();
        QCOMPARE(automation.documentScripts().scripts(),
                 (std::vector<std::string>{absolute.toStdString(), relative.toStdString()}));
        QVERIFY(settled());
        QCOMPARE(automation.scripts().size(), std::size_t(2));

        QSignalSpy done(&automation, &app::AutomationShell::runCompleted);
        QSignalSpy notices(&automation, &app::AutomationShell::notice);
        const auto steps = session().historySize();
        automation.runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(done.first().at(0).toBool());
        QCOMPARE(firstText(), std::string("last:Gate"));
        QCOMPARE(session().historySize(), steps + 1);
        QCOMPARE(session().history().back().name, std::string("Automation: Prefix last"));
        QVERIFY(notices.isEmpty());

        // The file changes: the next run reloads it (a new helper generation).
        writeFile(dir.filePath(relative), prefixScript("edited"));
        QFile changed(dir.filePath(relative));
        QVERIFY(changed.open(QIODevice::ReadWrite));
        QVERIFY(changed.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
        changed.close();
        const auto before = generation(relative);
        automation.runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QVERIFY(done.last().at(0).toBool());
        QCOMPARE(generation(relative), before + 1);
        QCOMPARE(firstText(), std::string("edited:last:Gate"));
        // Unchanged: no reload.
        automation.runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 30'000);
        QCOMPARE(generation(relative), before + 1);
        // The menu opening reloads a changed autoload or Document script too.
        QFile first(absolute);
        QVERIFY(first.open(QIODevice::ReadWrite));
        QVERIFY(first.setFileTime(QDateTime::currentDateTime().addSecs(120), QFileDevice::FileModificationTime));
        first.close();
        const auto firstBefore = generation(absolute);
        automation.menuOpened();
        QCOMPARE(generation(absolute), firstBefore + 1);
        QVERIFY(settled());
    }

    // HikariSubFrame.cpp:920: no Document scripts.
    void noScriptsSaySo()
    {
        QVERIFY(!open("plain.ass").isEmpty());
        QSignalSpy notices(&app->automation(), &app::AutomationShell::notice);
        app->automation().runLastLoadedScript();
        QCOMPARE(notices.size(), 1);
        // NT1: a toast, titled with the command.
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("info"));
        QCOMPARE(notices.first().at(1).toString(), QStringLiteral("Run the last loaded script"));
        QCOMPARE(notices.first().at(2).toString(), QStringLiteral("This subtitle file does not have any scripts added"));
        // Every script missing: the same.
        QVERIFY(!open("gone.ass", "|nothing.lua").isEmpty());
        app->automation().runLastLoadedScript();
        QCOMPARE(notices.size(), 2);
    }

    // HikariSubFrame.cpp:926-930: validation false says so and changes
    // nothing; a runtime error in validation goes to the log window first
    // (Automation.cpp:953).
    void validationFailureIsReported()
    {
        const QString script = dir.filePath(QStringLiteral("invalid.lua"));
        writeFile(script, "aegisub.register_macro(\"Guarded\", \"\", function(subs, sel)\n"
                          "    local l = subs[sel[1]]; l.text = \"ran\"; subs[sel[1]] = l\n"
                          "end, function() error(\"no way\") end)\n");
        QVERIFY(!open("invalid.ass", "|" + QDir::toNativeSeparators(script).toUtf8()).isEmpty());
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        QSignalSpy notices(&app->automation(), &app::AutomationShell::notice);
        QSignalSpy toasts(&app->log(), &ui::LogController::noticed);
        const int errorsBefore = app->log().errorCount();
        const auto steps = session().historySize();
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(notices.size(), 1);
        // NT1: a message box, the runtime error its Details (and in the Log).
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("error"));
        QCOMPARE(notices.first().at(1).toString(), QStringLiteral("Run the last loaded script"));
        QCOMPARE(notices.first().at(2).toString(), QStringLiteral("Validation Lua script 'invalid.lua' failed"));
        QVERIFY(notices.first().at(3).toString().contains(QStringLiteral("no way")));
        QVERIFY(app->log().history().contains(QStringLiteral("Runtime error in Lua macro validation function:")));
        QVERIFY(app->log().history().contains(QStringLiteral("no way")));
        // An error of the Log (its filter finds it), with no toast: the box says it.
        QCOMPARE(app->log().errorCount(), errorsBefore + 1);
        QVERIFY(app->log().history().contains(QStringLiteral("Automation: Runtime error in Lua macro validation function:")));
        QCOMPARE(toasts.size(), 0);
        QCOMPARE(firstText(), std::string("Gate"));
        QCOMPARE(session().historySize(), steps);
        QVERIFY(!session().isReadOnly());
        // From the menu legacy says nothing (LuaCommand::RunScript) but logs
        // the error (NT1: an error toast, from the Log).
        const int errors = app->log().errorCount();
        app->automation().run(QDir::toNativeSeparators(script).toStdString(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QCOMPARE(notices.size(), 1);
        QCOMPARE(app->log().errorCount(), errors + 1);
        // AM1: the Automation panel's run status still says it failed.
        QCOMPARE(app->automation().lastOutcome(), QStringLiteral("invalid"));
        // A script hotkey says so (HikariSubFrame::OnRunScript,
        // HikariSubFrame.cpp:2636-2642 at 20d647c4).
        auto &hotkeys = app->automationHotkeys();
        hotkeys.replaceCommitted({{"Script invalid.lua-0", "Ctrl+Shift+F9"}});
        hotkeys.install();
        QVERIFY(hotkeys.run(QStringLiteral("Script invalid.lua-0")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 30'000);
        QVERIFY(!done.last().at(0).toBool());
        QCOMPARE(notices.size(), 2);
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("error"));
        QCOMPARE(notices.last().at(1).toString(), QStringLiteral("Automation"));
        QCOMPARE(notices.last().at(2).toString(), QStringLiteral("Validation Lua script 'invalid.lua' failed"));
        QCOMPARE(firstText(), std::string("Gate"));
        QCOMPARE(session().historySize(), steps);
    }

    // S4-validation-edits: a validation function that edits the subtitles
    // and then answers false leaves the Document as it was: no text or
    // Script Info change, no undo step, not modified (legacy AutoToFile kept
    // the edits in the file without an undo step, Automation.cpp:936-969).
    void validationEditsAreDroppedWhenItAnswersFalse()
    {
        const QString script = dir.filePath(QStringLiteral("editing.lua"));
        writeFile(script, "aegisub.register_macro(\"Guarded\", \"\", function(subs, sel)\n"
                          "    local l = subs[sel[1]]; l.text = \"ran\"; subs[sel[1]] = l\n"
                          "end, function(subs, sel)\n"
                          "    local l = subs[sel[1]]; l.text = \"edited while validating\"; subs[sel[1]] = l\n"
                          "    subs.insert(3, { class = \"info\", section = \"[Script Info]\",\n"
                          "                     key = \"Validated\", value = \"yes\" })\n"
                          "    return false\n"
                          "end)\n");
        QVERIFY(!open("editing.ass", "|" + QDir::toNativeSeparators(script).toUtf8()).isEmpty());
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        QSignalSpy notices(&app->automation(), &app::AutomationShell::notice);
        const auto steps = session().historySize();
        const bool dirty = session().isDirty();
        // "Run the last loaded script" and the menu both validate first.
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().at(1).toString(), QStringLiteral("Run the last loaded script")); // NT1: the title
        QCOMPARE(notices.first().at(2).toString(), QStringLiteral("Validation Lua script 'editing.lua' failed"));
        app->automation().run(QDir::toNativeSeparators(script).toStdString(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QVERIFY(!done.last().at(0).toBool());
        QCOMPARE(firstText(), std::string("Gate"));
        QVERIFY(!session().document().scriptInfo(u8"Validated"));
        QCOMPARE(session().historySize(), steps);
        QCOMPARE(session().isDirty(), dirty);
        QVERIFY(!session().isReadOnly());
    }

    // S4-run-validation: a run validates its macro and, before it, the
    // macros of the scripts it depends on (main.lua requires dep.lua, an
    // autoload-style script, and dep2.lua, another Document script); an
    // unrelated script's validation and the script's own other macros' do not
    // run (legacy's menu build validated every Document-script macro). A
    // dependency answering false or raising, the first or a later one, stops
    // the run as the macro's own validation does; a dependency unloaded while
    // an earlier one validates is skipped.
    void runValidatesItsDependenciesFirst()
    {
        const QString deps = dir.filePath(QStringLiteral("deps"));
        const QString records = deps + QStringLiteral("/records.txt");
        const QString go = deps + QStringLiteral("/go.txt");
        auto answerFile = [&](const char *tag) { return deps + QStringLiteral("/%1-answer.txt").arg(QLatin1String(tag)); };
        // record(event) appends "<tag> <event>"; answer() reads the tag's
        // answer file: true (the default), false, error, or wait (for go.txt,
        // then true).
        auto head = [&](const char *tag) {
            return "local function record(event)\n"
                   "    local f = assert(io.open(\"" + records.toUtf8() + "\", \"a\"))\n"
                   "    f:write(\"" + QByteArray(tag) + " \" .. event .. \"\\n\")\n"
                   "    f:close()\n"
                   "end\n"
                   "local function answer()\n"
                   "    local f = io.open(\"" + answerFile(tag).toUtf8() + "\", \"r\")\n"
                   "    local a = f and f:read(\"*l\") or \"true\"\n"
                   "    if f then f:close() end\n"
                   "    if a == \"error\" then error(\"" + QByteArray(tag) + " says no\") end\n"
                   "    if a == \"wait\" then\n"
                   "        local deadline = os.time() + 30\n"
                   "        while os.time() < deadline do\n"
                   "            local g = io.open(\"" + go.toUtf8() + "\", \"r\")\n"
                   "            if g then g:close(); break end\n"
                   "        end\n"
                   "        return true\n"
                   "    end\n"
                   "    return a == \"true\"\n"
                   "end\n";
        };
        const QString dep = QDir::toNativeSeparators(deps + QStringLiteral("/dep.lua"));
        writeFile(dep, head("dep") +
                           "aegisub.register_macro(\"Dep guarded\", \"\", function() record(\"run\") end,\n"
                           "    function() record(\"validate guarded\"); return answer() end)\n"
                           "aegisub.register_macro(\"Dep plain\", \"\", function() record(\"run plain\") end)\n"
                           "aegisub.register_macro(\"Dep second\", \"\", function() end,\n"
                           "    function() record(\"validate second\"); return true end)\n"
                           "return {}\n");
        const QString dep2 = QDir::toNativeSeparators(deps + QStringLiteral("/dep2.lua"));
        writeFile(dep2, head("dep2") +
                            "aegisub.register_macro(\"Dep2\", \"\", function() record(\"run\") end,\n"
                            "    function() record(\"validate\"); return answer() end)\n"
                            "return {}\n");
        const QString main = QDir::toNativeSeparators(deps + QStringLiteral("/main.lua"));
        writeFile(main, head("main") +
                            "aegisub.register_macro(\"Main\", \"\", function(subs, sel)\n"
                            "    record(\"run\")\n"
                            "    local l = subs[sel[1]]; l.text = \"main:\" .. l.text; subs[sel[1]] = l\n"
                            "end, function() record(\"validate\"); return answer() end)\n"
                            "aegisub.register_macro(\"Main other\", \"\", function() record(\"run other\") end,\n"
                            "    function() record(\"validate other\"); return true end)\n"
                            "require \"dep\"\n"
                            "require \"dep2\"\n");
        const QString other = QDir::toNativeSeparators(dir.filePath(QStringLiteral("unrelated/other.lua")));
        writeFile(other, head("other") + "aegisub.register_macro(\"Other\", \"\", function() end,\n"
                                         "    function() record(\"validate\"); return true end)\n");
        auto &automation = app->automation();
        automation.load(dep.toStdString());
        automation.load(other.toStdString());
        // Two Document scripts; main.lua, the last, is the one "Run the last
        // loaded script" runs.
        QVERIFY(!open("deps.ass", "|" + dep2.toUtf8() + "|" + main.toUtf8()).isEmpty());
        automation.menuOpened();
        QVERIFY(settled());
        QCOMPARE(automation.scripts().size(), std::size_t(4));
        auto taken = [&] {
            QFile f(records);
            QStringList got;
            // Text: Lua's io.open writes "\r\n" on Windows.
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                got = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            f.close();
            QFile::remove(records);
            return got;
        };
        auto answer = [&](const char *tag, const QByteArray &a) { writeFile(answerFile(tag), a + "\n"); };
        taken();
        QSignalSpy done(&automation, &app::AutomationShell::runCompleted);
        QSignalSpy notices(&automation, &app::AutomationShell::notice);
        const auto steps = session().historySize();
        const QStringList dependencies{QStringLiteral("dep validate guarded"), QStringLiteral("dep validate second"),
                                       QStringLiteral("dep2 validate")};

        automation.runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(done.last().at(0).toBool());
        QCOMPARE(taken(), (dependencies + QStringList{QStringLiteral("main validate"), QStringLiteral("main run")}));
        QCOMPARE(firstText(), std::string("main:Gate"));
        QCOMPARE(session().historySize(), steps + 1);
        QVERIFY(notices.isEmpty());

        // The script's other macro, from the menu: the same dependencies.
        QVERIFY(automation.run(main.toStdString(), 1));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QCOMPARE(taken(), (dependencies + QStringList{QStringLiteral("main validate other"),
                                                      QStringLiteral("main run other")}));
        // The dependency's own macro depends on nothing.
        QVERIFY(automation.run(dep.toStdString(), 0));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 30'000);
        QCOMPARE(taken(), (QStringList{QStringLiteral("dep validate guarded"), QStringLiteral("dep run")}));

        // A failing validation: nothing more validates or runs, the Document
        // is unchanged, the box names the script whose validation failed,
        // and the error (when it raised) goes to the log window first.
        const auto changed = session().historySize();
        int runs = 3;
        int boxes = 0;
        auto refused = [&](const QStringList &expected, const QString &file) {
            automation.runLastLoadedScript();
            ++runs;
            ++boxes;
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), runs, 30'000);
            QVERIFY(!done.last().at(0).toBool());
            QCOMPARE(taken(), expected);
            QCOMPARE(notices.size(), boxes);
            QCOMPARE(notices.last().at(0).toString(), QStringLiteral("error"));
            QCOMPARE(notices.last().at(2).toString(), QStringLiteral("Validation Lua script '%1' failed").arg(file));
            QCOMPARE(firstText(), std::string("main:Gate"));
            QCOMPARE(session().historySize(), changed);
            QVERIFY(!session().isReadOnly());
            QVERIFY(!automation.running());
        };
        // The first dependency answers false, then raises.
        answer("dep", "false");
        refused({QStringLiteral("dep validate guarded")}, QStringLiteral("dep.lua"));
        if (QTest::currentTestFailed())
            return;
        answer("dep", "error");
        refused({QStringLiteral("dep validate guarded")}, QStringLiteral("dep.lua"));
        if (QTest::currentTestFailed())
            return;
        QVERIFY(app->log().history().contains(QStringLiteral("Runtime error in Lua macro validation function:")));
        QVERIFY(app->log().history().contains(QStringLiteral("dep says no")));
        answer("dep", "true");
        // A later dependency, after the first passed.
        answer("dep2", "false");
        refused(dependencies, QStringLiteral("dep2.lua"));
        if (QTest::currentTestFailed())
            return;
        answer("dep2", "error");
        refused(dependencies, QStringLiteral("dep2.lua"));
        if (QTest::currentTestFailed())
            return;
        QVERIFY(app->log().history().contains(QStringLiteral("dep2 says no")));
        answer("dep2", "true");
        // The macro's own, after every dependency passed.
        answer("main", "false");
        refused(dependencies + QStringList{QStringLiteral("main validate")}, QStringLiteral("main.lua"));
        if (QTest::currentTestFailed())
            return;

        // From the menu legacy says nothing (LuaCommand::RunScript).
        QVERIFY(automation.run(main.toStdString(), 0));
        ++runs;
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), runs, 30'000);
        QVERIFY(!done.last().at(0).toBool());
        QCOMPARE(taken(), dependencies + QStringList{QStringLiteral("main validate")});
        QCOMPARE(notices.size(), boxes);
        QCOMPARE(firstText(), std::string("main:Gate"));
        QCOMPARE(session().historySize(), changed);
        QVERIFY(!session().isReadOnly());
        answer("main", "true");

        // dep2.lua is unloaded while dep.lua validates: it is no longer a
        // loaded script, so it is skipped and the macro runs.
        answer("dep", "wait");
        automation.runLastLoadedScript();
        QTRY_VERIFY_WITH_TIMEOUT(QFile(records).size() > 0, 30'000);
        QVERIFY(automation.running());
        automation.unload(dep2.toStdString());
        writeFile(go, "go\n");
        ++runs;
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), runs, 30'000);
        QVERIFY(done.last().at(0).toBool());
        QCOMPARE(taken(), (QStringList{QStringLiteral("dep validate guarded"), QStringLiteral("dep validate second"),
                                       QStringLiteral("main validate"), QStringLiteral("main run")}));
        QCOMPARE(firstText(), std::string("main:main:Gate"));
        QCOMPARE(notices.size(), boxes);
    }

    // Legacy runs one macro at a time behind a modal progress dialog
    // (HikariSubFrame.cpp:916-936 runs to completion before anything else);
    // asked while another macro runs, the last script's macro is not dropped:
    // it runs when the active one ends.
    void lastScriptWaitsForTheActiveMacro()
    {
        const QString script = QDir::toNativeSeparators(dir.filePath(QStringLiteral("waiting.lua")));
        writeFile(script, prefixScript("after"));
        QVERIFY(!open("waiting.ass", "|" + script.toUtf8()).isEmpty());
        auto &automation = app->automation();
        automation.menuOpened();
        automation.load(HIKARI_LUA_FIXTURES "/shell-fixture.lua");
        QVERIFY(settled());
        QSignalSpy done(&automation, &app::AutomationShell::runCompleted);
        QSignalSpy notices(&automation, &app::AutomationShell::notice);
        // "Wait for cancel" (shell-fixture.lua) runs until cancelled.
        QVERIFY(automation.run(HIKARI_LUA_FIXTURES "/shell-fixture.lua", 1));
        QVERIFY(automation.running());
        automation.runLastLoadedScript();
        QVERIFY(notices.isEmpty());
        QCOMPARE(done.size(), 0);
        automation.cancelRun();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QVERIFY(!done.first().at(0).toBool()); // cancelled
        QVERIFY(done.last().at(0).toBool());
        QCOMPARE(firstText(), std::string("after:Gate"));
        QVERIFY(notices.isEmpty());
    }

    // HikariSubFrame.cpp:932-935: a script that did not load says why, then
    // asks for a script editor (Automation::OnEdit with no editor set);
    // an editor that does not start says so (Automation.cpp:1323-1325).
    void loadFailureOffersTheEditor()
    {
        const QString script = QDir::toNativeSeparators(dir.filePath(QStringLiteral("broken.lua")));
        writeFile(script, "this is not lua\n");
        QVERIFY(!open("broken.ass", "|" + script.toUtf8()).isEmpty());
        QSignalSpy notices(&app->automation(), &app::AutomationShell::notice);
        QSignalSpy editor(&app->automation(), &app::AutomationShell::chooseScriptEditor);
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(notices.size(), 1, 30'000);
        // NT1: the load error is the box's Details.
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("error"));
        QCOMPARE(notices.first().at(2).toString(), QStringLiteral("Error loading Lua script: broken.lua"));
        QVERIFY(!notices.first().at(3).toString().isEmpty());
        QCOMPARE(editor.size(), 1);
        QCOMPARE(editor.first().at(0).toString(), script);
        // A file that is no program: chosen and saved, but it does not start
        // (38e: the start is recorded, the desktop answers that it failed).
        const QString notAProgram = dir.filePath(QStringLiteral("notes.txt"));
        writeFile(notAProgram, "text");
        hikari::test::desktop::clear();
        hikari::test::desktop::setStartResult(false);
        app->automation().editWith(QUrl::fromLocalFile(notAProgram), script);
        const hikari::test::desktop::Started start{QDir::toNativeSeparators(notAProgram), {script}};
        QCOMPARE(hikari::test::desktop::started(), QList<hikari::test::desktop::Started>{start});
        QCOMPARE(notices.size(), 2);
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("error"));
        QCOMPARE(notices.last().at(1).toString(), QStringLiteral("Edit script"));
        QCOMPARE(notices.last().at(2).toString(), QStringLiteral("Cannot start editor."));
        QCOMPARE(app->settingsStore()->text("automation.scriptEditor"), QDir::toNativeSeparators(notAProgram));
        // With an editor set, it is used without asking.
        app->automation().editScript(script);
        QCOMPARE(editor.size(), 1);
        QCOMPARE(notices.size(), 3);
        QCOMPARE(hikari::test::desktop::started(), (QList<hikari::test::desktop::Started>{start, start}));
        // An editor that starts says nothing.
        hikari::test::desktop::setStartResult(true);
        app->automation().editScript(script);
        QCOMPARE(notices.size(), 3);
        QCOMPARE(hikari::test::desktop::started().size(), 3);
        // A missing editor is not taken (wxFileExists).
        app->automation().editWith(QUrl::fromLocalFile(dir.filePath(QStringLiteral("none.exe"))), script);
        QCOMPARE(notices.size(), 3);
        QCOMPARE(hikari::test::desktop::started().size(), 3);
        hikari::test::desktop::clear();
    }

    // A last script that loaded but registered no macros has nothing to
    // run: an info notice, not legacy's load error and script editor (#390).
    void lastScriptWithoutMacrosSaysSo()
    {
        const QString script = QDir::toNativeSeparators(dir.filePath(QStringLiteral("library.lua")));
        writeFile(script, "script_name = \"library\"\n");
        QVERIFY(!open("library.ass", "|" + script.toUtf8()).isEmpty());
        QSignalSpy notices(&app->automation(), &app::AutomationShell::notice);
        QSignalSpy editor(&app->automation(), &app::AutomationShell::chooseScriptEditor);
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(notices.size(), 1, 30'000);
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("info"));
        QCOMPARE(notices.first().at(2).toString(), QStringLiteral("library.lua has no macros to run"));
        QVERIFY(notices.first().at(3).toString().isEmpty());
        QCOMPARE(editor.size(), 0);
        QCOMPARE(firstText(), std::string("Gate"));
    }

    // Automation::Add (Automation.cpp:1153-1171): Load script lists the
    // script and appends "|<path>" to the Document's Script Info as a
    // "Changing the subtitle header" step; a listed script is not added
    // again, but it reloads (#390: legacy did nothing).
    void loadScriptNamesItInTheDocument()
    {
        const QString script = QDir::toNativeSeparators(dir.filePath(QStringLiteral("loaded.lua")));
        writeFile(script, prefixScript("loaded"));
        QVERIFY(!open("load.ass").isEmpty());
        const auto steps = session().historySize();
        app->automation().loadScript(QUrl::fromLocalFile(script));
        const QByteArray utf8 = script.toUtf8();
        const std::u8string expected =
            u8"|" + std::u8string(reinterpret_cast<const char8_t *>(utf8.constData()), static_cast<std::size_t>(utf8.size()));
        QVERIFY(session().document().scriptInfo(u8"Automation Scripts") == expected);
        QCOMPARE(session().historySize(), steps + 1);
        QCOMPARE(session().history().back().name, std::string("Changing the subtitle header"));
        QVERIFY(settled());
        const auto loaded = generation(script);
        app->automation().loadScript(QUrl::fromLocalFile(script));
        QCOMPARE(session().historySize(), steps + 1);
        QCOMPARE(generation(script), loaded + 1);
        QVERIFY(settled());
        // It is now the last loaded script.
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QCOMPARE(firstText(), std::string("loaded:Gate"));
    }

    // Legacy get_keyframes (Automation.cpp:183-197) and
    // lua_get_audio_selection (394-410): nil without a video or an audio box;
    // the timebase's keyframes, and the current Line's times (not the box's
    // selection) once they are open.
    void keyframesAndAudioSelectionAnswer()
    {
        const QString script = QDir::toNativeSeparators(dir.filePath(QStringLiteral("media.lua")));
        writeFile(script, "aegisub.register_macro(\"Media\", \"\", function(subs, sel)\n"
                          "    local kf = aegisub.keyframes()\n"
                          "    local s, e = aegisub.get_audio_selection()\n"
                          "    local l = subs[sel[1]]\n"
                          "    l.text = (kf and table.concat(kf, \",\") or \"nil\") .. \"|\" .. tostring(s) .. \",\" .. tostring(e)\n"
                          "    subs[sel[1]] = l\n"
                          "end)\n");
        QVERIFY(!open("media.ass", "|" + script.toUtf8()).isEmpty());
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QCOMPARE(firstText(), std::string("nil|nil,nil"));

        const QString kf = dir.filePath(QStringLiteral("kf.txt"));
        writeFile(kf, "# keyframe format v1\nfps 25\n0\n10\n20\n");
        QVERIFY(app->openKeyframes(QUrl::fromLocalFile(kf)).isEmpty());
        app->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(app->video().session().keyframes() == (std::vector<int>{0, 10, 20}), 20'000);
        app->audio().openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(app->audio().ready(), 20'000);
        // The second Line is the current one: its times, whatever the box selects.
        session().setSelection(application::Selection{session().document().lines()[1]->id,
                                                      {session().document().lines()[1]->id}});
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QVERIFY(done.last().at(0).toBool());
        const auto &text = session().document().lines()[1]->text;
        QCOMPARE(std::string(text.begin(), text.end()), std::string("0,10,20|3000,4000"));
        // A preview runs before Apply commits the Line editor's draft: the
        // current Line's times are the draft's, as Apply's run reads them.
        QVERIFY(session().editDraft(session().document().lines()[1]->id,
                                    application::DraftChange{.start = core::DocumentTime(3500000),
                                                             .end = core::DocumentTime(4500000)}));
        QVERIFY(app->automation().hostState().find(" audio 3500-4500") != std::string::npos);
    }

    // The legacy capture (local-s4-document-scripts-20261013, the legacy
    // Linux app driven through its "Run the last loaded script" hotkey) as
    // the rewrite gives it, case by case: what the scripts saw and did, the
    // boxes, the log window, the editor picker, the modified state.
    void replaysTheLegacyCapture_data();
    void replaysTheLegacyCapture();

    // L4 refused these; now (S4) a macro's Script Info and Style edits apply
    // with its Line edits as one undo step, which Undo reverts together.
    void styleAndInfoEditsAreOneStep()
    {
        const QString script = QDir::toNativeSeparators(dir.filePath(QStringLiteral("header.lua")));
        writeFile(script, "aegisub.register_macro(\"Header\", \"\", function(subs, sel)\n"
                          "    for i = 1, #subs do\n"
                          "        local l = subs[i]\n"
                          "        if l.class == \"info\" and l.key == \"Title\" then l.value = \"by macro\"; subs[i] = l end\n"
                          "        if l.class == \"style\" and l.name == \"Default\" then l.fontname = \"Times\"; subs[i] = l end\n"
                          "    end\n"
                          "    local l = subs[sel[1]]; l.text = \"header\"; subs[sel[1]] = l\n"
                          // Appended info goes after the last info line, moving the Lines' rows
                          // (AutoToFile::ObjectAppend, AutomationToFile.cpp:822-825).
                          "    subs.append({ class = \"info\", section = \"[Script Info]\", key = \"PlayResX\", value = \"640\" })\n"
                          "end)\n");
        QVERIFY(!open("header.ass", "|" + script.toUtf8()).isEmpty());
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        const auto steps = session().historySize();
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(session().historySize(), steps + 1);
        const auto &doc = session().document();
        QVERIFY(doc.scriptInfo(u8"Title") == std::u8string(u8"by macro"));
        QVERIFY(doc.scriptInfo(u8"PlayResX") == std::u8string(u8"640"));
        QCOMPARE(firstText(), std::string("header"));
        bool times = false;
        for (const auto &section : doc.sections())
            for (const auto &record : section.records)
                if (const auto *style = std::get_if<core::StyleRecord>(&record))
                    times = times || (style->fields.size() > 1 && style->fields[1] == u8"Times");
        QVERIFY(times);
        QVERIFY(session().undo());
        QVERIFY(session().document().scriptInfo(u8"Title") == std::u8string(u8"s4"));
        QVERIFY(!session().document().scriptInfo(u8"PlayResX"));
        QCOMPARE(firstText(), std::string("Gate"));
    }
};

// The Linux capture (document_scripts_capture.py) and the Windows one
// (drive_windows.py --document-scripts on the Winix VM), the same plan.
void AutomationDocumentScriptsTests::replaysTheLegacyCapture_data()
{
    QTest::addColumn<QString>("capture");
    QTest::newRow("linux") << QStringLiteral(HIKARI_S4_OBSERVATIONS);
    QTest::newRow("windows") << QStringLiteral(HIKARI_S4_WINDOWS_OBSERVATIONS);
}

void AutomationDocumentScriptsTests::replaysTheLegacyCapture()
{
    QFETCH(QString, capture);
    QFile observations(capture);
    QVERIFY(observations.open(QIODevice::ReadOnly));
    const QJsonArray cases =
        QJsonDocument::fromJson(observations.readAll()).object()["cases"].toArray()[0].toObject()["observations"].toArray();
    QCOMPARE(cases.size(), 7);
    const QDir scripts(QStringLiteral(HIKARI_S4_SCRIPTS));
    QFile headFile(scripts.filePath(QStringLiteral("record.lua.in")));
    QVERIFY(headFile.open(QIODevice::ReadOnly));
    QByteArray head = headFile.readAll();
    // The Document's scripts load at once, each in its own helper, and one
    // can still be loading while the last one's macro runs. On Windows two
    // helpers appending to one file can lose a line: the C runtime's append
    // seeks to the end, then writes, so both can write at the same offset.
    // A lock file taken with os.rename does not keep them apart either: two
    // LuaJIT processes taking one 5000 times each in the Windows VM were
    // both inside it about 800 times, and lines were still lost. So each
    // script's load records go to a file of its own (capture.jsonl.<tag>);
    // validations and runs come from the one active macro, one at a time,
    // and go to the shared capture in order.
    const QByteArray appendOpen = "local f = assert(io.open(output(), \"a\"))";
    QVERIFY(head.contains(appendOpen));
    head.replace(appendOpen, "local f = assert(io.open(own(output(), event), \"a\"))");
    // On the tag's line, so the scripts' line numbers stay legacy's.
    const QByteArray own = " local function own(out, event) return event == \"load\" and out .. \".\" .. tag or out end";
    const QStringList tags{QStringLiteral("first"), QStringLiteral("last"), QStringLiteral("invalid"),
                           QStringLiteral("raising"), QStringLiteral("broken")};
    // The legacy boxes' text, read from the capture's screenshots (screens/);
    // in validation-error the log window covers the box, so its text is the
    // one the same branch gives validation-false (a validation that raises
    // answers false, Automation.cpp:952-956; HikariSubFrame.cpp:924).
    const std::map<QString, QString> boxText{
        {QStringLiteral("no-scripts"), QStringLiteral("This subtitle file does not have any scripts added")},
        {QStringLiteral("only-missing"), QStringLiteral("This subtitle file does not have any scripts added")},
        {QStringLiteral("validation-false"), QStringLiteral("Validation Lua script 'invalid.lua' failed")},
        {QStringLiteral("validation-error"), QStringLiteral("Validation Lua script 'raising.lua' failed")},
        {QStringLiteral("load-error"), QStringLiteral("Error loading Lua script: broken.lua\n")},
    };
    // Keys legacy adds to Script Info and the rewrite does not, taken off
    // legacy's records only (the rewrite's are compared whole): YCbCr Matrix
    // on load (C03-ycbcr-on-load) and PlayResX and PlayResY written by
    // GetASSRes when the status bar reads the resolution (SubsGridBase.cpp:
    // 1653-1675; the rewrite only reads them, Y4, open for a decision), once
    // each; in validation-false, the "Validated" keys the validation function
    // inserts and its "edited while validating" text (S4-validation-edits).
    const QStringList addedOnLoad{QStringLiteral("YCbCr Matrix"), QStringLiteral("PlayResX"), QStringLiteral("PlayResY")};

    // A failed check leaves the slot early: the next slot starts in the
    // test's own folder without the capture variable.
    const auto restore = qScopeGuard([this] {
        app.reset();
        qunsetenv("HIKARI_CAPTURE_OUT");
        QDir::setCurrent(dir.path());
    });
    for (const auto &c : cases) {
        const QJsonObject legacy = c.toObject();
        const QString id = legacy["id"].toString();
        QCOMPARE(legacy["status"].toString(), QStringLiteral("captured"));
        // Each case in its own folder, the working directory (relative paths).
        QDir root(dir.path());
        QVERIFY(root.mkpath(id));
        QDir caseDir(root.filePath(id));
        QVERIFY(QDir::setCurrent(caseDir.path()));
        const QString absolute = caseDir.filePath(QStringLiteral("abs"));
        for (const QString &folder : {absolute, caseDir.filePath(QStringLiteral("s4"))})
            for (const QString &name : tags) {
                QFile body(scripts.filePath(name + QStringLiteral(".lua")));
                QVERIFY(body.open(QIODevice::ReadOnly));
                QByteArray text = head;
                text.replace("local tag = TAG", "local tag = \"" + name.toUtf8() + "\";" + own);
                writeFile(folder + QLatin1Char('/') + name + QStringLiteral(".lua"), text + "\n" + body.readAll());
            }
        const QString out = caseDir.filePath(QStringLiteral("capture.jsonl"));
        writeFile(out, {});
        qputenv("HIKARI_CAPTURE_OUT", out.toLocal8Bit());
        // Settings of its own, as legacy's fresh copy (no script editor set).
        auto o = options();
        o.settingsFile = caseDir.filePath(QStringLiteral("hikari.ini"));
        // Autosaves only where the replay makes one (below): none on a timer.
        o.recoveryDir = caseDir.filePath(QStringLiteral("Recovery"));
        app = std::make_unique<app::Application>(o);
        app->settingsStore()->set("autosave.maxFiles", 0);
        QByteArray value;
        if (!legacy["document_scripts"].isNull())
            value = legacy["document_scripts"].toString().replace(QStringLiteral("{abs}"), QDir::toNativeSeparators(absolute)).toUtf8();
        QVERIFY(!open(qPrintable(id + QStringLiteral(".ass")), value).isEmpty());
        auto &automation = app->automation();
        QSignalSpy done(&automation, &app::AutomationShell::runCompleted);
        QSignalSpy notices(&automation, &app::AutomationShell::notice);
        QSignalSpy editor(&automation, &app::AutomationShell::chooseScriptEditor);
        auto records = [&] {
            QFile f(out);
            if (!f.open(QIODevice::ReadOnly))
                return QList<QJsonObject>{};
            QList<QJsonObject> got;
            for (const QByteArray &line : f.readAll().split('\n'))
                if (!line.isEmpty())
                    got.append(QJsonDocument::fromJson(line).object());
            return got;
        };
        // How many times each script has loaded: the lines of its own file.
        auto loads = [&] {
            std::map<QString, qsizetype> count;
            for (const QString &tag : tags) {
                QFile f(out + QLatin1Char('.') + tag);
                if (f.open(QIODevice::ReadOnly))
                    count[tag] = f.readAll().count('\n');
            }
            return count;
        };

        const QJsonArray steps = legacy["steps"].toArray();
        for (qsizetype n = 0; n < steps.size(); ++n) {
            const QJsonObject step = steps[n].toObject();
            const QString where = id + QStringLiteral(" step ") + QString::number(n);
            if (step["do"].toString() == QStringLiteral("touch")) {
                for (const QString &folder : {absolute, caseDir.filePath(QStringLiteral("s4"))}) {
                    QFile f(folder + QLatin1Char('/') + step["script"].toString() + QStringLiteral(".lua"));
                    QVERIFY(f.open(QIODevice::Append));
                    f.write("-- changed\n");
                    QVERIFY(f.flush());
                    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
                }
                continue;
            }
            // The windows legacy opened: a box (its title and text), the log
            // window, the editor picker.
            QStringList titles;
            for (const auto &w : step["windows"].toArray())
                titles << w.toObject()["title"].toString();
            const bool box = titles.contains(QStringLiteral("Info")) || titles.contains(QStringLiteral("Error"));
            const bool picker = titles.contains(QStringLiteral("Select a script editor"));

            // S4: legacy's autosave (SubsGrid::OnBackupTimer, 20 s after the
            // first change, SubsGridBase.cpp:1085-1088) saves through SaveFile,
            // which writes "Active Line" into the open Document's Script Info
            // (GetAssHeader, SubsGridBase.cpp:1470 at 20d647c4). On Windows the
            // driver's steps are slow (the 8 s wait after each hotkey and the
            // winix tasks in between, drive_windows.py), so in runs-last the
            // timer that step 0's run started fired before step 3, and the
            // scripts see the key from there on; the Linux capture's steps all
            // came within the 20 s. The replay autosaves where legacy's first
            // records with the key show it did.
            auto hasActiveLine = [](const QJsonObject &s) {
                for (const auto &r : s["records"].toArray())
                    if (r.toObject()["info"].toArray().contains(QStringLiteral("Active Line")))
                        return true;
                return false;
            };
            bool autosaved = false;
            for (qsizetype m = 0; m < n; ++m)
                autosaved = autosaved || hasActiveLine(steps[m].toObject());
            if (hasActiveLine(step) && !autosaved) {
                app->settingsStore()->set("autosave.maxFiles", 3);
                QVERIFY2(app->autosaveNow(), qPrintable(where));
                app->settingsStore()->set("autosave.maxFiles", 0);
            }

            const auto before = records().size();
            const auto loadsBefore = loads();
            const auto doneBefore = done.size(), noticesBefore = notices.size(), editorBefore = editor.size();
            automation.runLastLoadedScript();
            QTRY_VERIFY_WITH_TIMEOUT(done.size() > doneBefore || notices.size() > noticesBefore, 30'000);
            // A box a failed run brings after its end, the picker after a load error.
            QTRY_VERIFY_WITH_TIMEOUT(notices.size() - noticesBefore >= (box ? 1 : 0) &&
                                         editor.size() - editorBefore >= (picker ? 1 : 0),
                                     30'000);
            // The run waits for its own script's load only; the others' loads,
            // each in its own helper, can land after it ends (on Windows CI).
            // So wait until every load legacy recorded in the step has a line.
            auto loadsSince = [&] {
                QStringList got;
                for (const auto &[script, count] : loads())
                    for (auto i = loadsBefore.contains(script) ? loadsBefore.at(script) : 0; i < count; ++i)
                        got << script;
                return got;
            };
            qsizetype loadsExpected = 0;
            for (const auto &r : step["records"].toArray())
                loadsExpected += r.toObject()["event"].toString() == QStringLiteral("load");
            QTRY_VERIFY_WITH_TIMEOUT(loadsSince().size() >= loadsExpected, 30'000);
            QTest::qWait(300); // nothing more comes
            const auto got = records().mid(before);
            QStringList gotLoads = loadsSince();

            // Legacy's records, less the validations its Automation menu build
            // made first: the hotkey goes through the menu item, and
            // OnMenuSelected rebuilds the item's menu before running it
            // (HikariSubFrame.cpp:680-686, BuildMenu validates every macro,
            // Automation.cpp:1375, 1434). The rewrite validates when it runs
            // (S4-run-validation).
            // The command's validation is the last one.
            const QJsonArray all = step["records"].toArray();
            qsizetype command = -1;
            for (qsizetype i = 0; i < all.size(); ++i)
                if (all[i].toObject()["event"].toString() == QStringLiteral("validate"))
                    command = i;
            QStringList expectedLoads, expected, actual;
            const bool validationEdits = id == QStringLiteral("validation-false");
            auto view = [&](const QJsonObject &r, bool fromLegacy) {
                QString first = r["first"].toString();
                QStringList info;
                for (const auto &k : r["info"].toArray())
                    info << k.toString();
                if (fromLegacy) {
                    for (const QString &key : addedOnLoad)
                        info.removeOne(key);
                    if (validationEdits && r["script"].toString() == QStringLiteral("invalid")) {
                        info.removeAll(QStringLiteral("Validated"));
                        if (first == QStringLiteral("edited while validating"))
                            first = QStringLiteral("Gate");
                    }
                }
                return r["script"].toString() + QLatin1Char(' ') + r["event"].toString() + QLatin1Char(' ') + first +
                       QLatin1Char(' ') + QString::number(r["lines"].toInt()) + QLatin1Char(' ') + info.join(QLatin1Char(','));
            };
            for (qsizetype i = 0; i < all.size(); ++i) {
                const QJsonObject r = all[i].toObject();
                if (r["event"].toString() == QStringLiteral("load"))
                    expectedLoads << r["script"].toString();
                else if (r["event"].toString() != QStringLiteral("validate") || i == command)
                    expected << view(r, true);
            }
            for (const QJsonObject &r : got)
                actual << view(r, false);
            // Each script loads in its own helper, so the loads' order is theirs.
            expectedLoads.sort();
            gotLoads.sort();
            QVERIFY2(gotLoads == expectedLoads, qPrintable(where + QStringLiteral(": ") + gotLoads.join(QLatin1Char(','))));
            QVERIFY2(actual == expected,
                     qPrintable(where + QStringLiteral(":\n  rewrite ") + actual.join(QStringLiteral("\n          ")) +
                                QStringLiteral("\n  legacy  ") + expected.join(QStringLiteral("\n          "))));

            QVERIFY2(notices.size() - noticesBefore == (box ? 1 : 0),
                     qPrintable(where + QStringLiteral(": ") + QString::number(notices.size() - noticesBefore) +
                                QStringLiteral(" notices, the last ") +
                                (notices.isEmpty() ? QString() : notices.last().at(2).toString())));
            if (box) {
                // NT1: legacy's "Info" box is a toast, its "Error" boxes
                // message boxes; a load error's second line is the Details.
                QCOMPARE(notices.last().at(0).toString(), titles.contains(QStringLiteral("Info")) ? QStringLiteral("info")
                                                                                                  : QStringLiteral("error"));
                const auto text = boxText.find(id);
                QVERIFY2(text != boxText.end(), qPrintable(where + QStringLiteral(": no legacy box text")));
                const QString details = notices.last().at(3).toString();
                const QString shown = notices.last().at(2).toString() + (details.isEmpty() ? QString() : QStringLiteral("\n") + details);
                QVERIFY2(shown.startsWith(text->second), qPrintable(where + QStringLiteral(": ") + shown));
            }
            QCOMPARE(editor.size() - editorBefore, picker ? 1 : 0);
            if (titles.contains(QStringLiteral("Log window")))
                QVERIFY(app->log().history().contains(QStringLiteral("Runtime error in Lua macro validation function:")));
            // A run marks the Document modified ("1*" in legacy's title); a
            // failed validation does not.
            QCOMPARE(session().isDirty(), step["main_title"].toString().contains(QLatin1Char('*')));
        }
        app.reset();
        qunsetenv("HIKARI_CAPTURE_OUT");
    }
}

QTEST_MAIN(AutomationDocumentScriptsTests)
#include "automation_document_scripts_tests.moc"
