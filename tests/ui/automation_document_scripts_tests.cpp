// S4 through the composition with real Lua helpers and media: the scripts a
// Document's Script Info names ("Automation Scripts", legacy
// Automation::AddFromSubs) load when the Automation menu opens; "Run the last
// loaded script" (GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT, HikariSubFrame.cpp:916-936
// at 20d647c4) reloads a changed file and runs its first macro after
// validation, or says why not; Load script names the script in Script Info;
// keyframes and get_audio_selection answer from the video and the audio box;
// a macro's Style and Script Info edits are one undo step.

#include "hikari/app/application.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("Info"));
        QCOMPARE(notices.first().at(1).toString(), QStringLiteral("This subtitle file does not have any scripts added"));
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
        const auto steps = session().historySize();
        app->automation().runLastLoadedScript();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("Error"));
        QCOMPARE(notices.first().at(1).toString(), QStringLiteral("Validation Lua script 'invalid.lua' failed"));
        QVERIFY(app->log().history().contains(QStringLiteral("Runtime error in Lua macro validation function:")));
        QVERIFY(app->log().history().contains(QStringLiteral("no way")));
        QCOMPARE(firstText(), std::string("Gate"));
        QCOMPARE(session().historySize(), steps);
        QVERIFY(!session().isReadOnly());
        // From the menu legacy says nothing (LuaCommand::RunScript).
        app->automation().run(QDir::toNativeSeparators(script).toStdString(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QCOMPARE(notices.size(), 1);
        // A script hotkey says so (HikariSubFrame::OnRunScript,
        // HikariSubFrame.cpp:2636-2642 at 20d647c4).
        auto &hotkeys = app->automationHotkeys();
        hotkeys.begin();
        hotkeys.setKeys(QStringLiteral("Script invalid.lua-0"), QStringLiteral("Ctrl+Shift+F9"));
        hotkeys.commit();
        QVERIFY(hotkeys.run(QStringLiteral("Script invalid.lua-0")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 30'000);
        QVERIFY(!done.last().at(0).toBool());
        QCOMPARE(notices.size(), 2);
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Error"));
        QCOMPARE(notices.last().at(1).toString(), QStringLiteral("Validation Lua script 'invalid.lua' failed"));
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
        QCOMPARE(notices.first().at(1).toString(), QStringLiteral("Validation Lua script 'editing.lua' failed"));
        app->automation().run(QDir::toNativeSeparators(script).toStdString(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 30'000);
        QVERIFY(!done.last().at(0).toBool());
        QCOMPARE(firstText(), std::string("Gate"));
        QVERIFY(!session().document().scriptInfo(u8"Validated"));
        QCOMPARE(session().historySize(), steps);
        QCOMPARE(session().isDirty(), dirty);
        QVERIFY(!session().isReadOnly());
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
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("Error"));
        const QString text = notices.first().at(1).toString();
        QVERIFY2(text.startsWith(QStringLiteral("Error loading Lua script: broken.lua\n")), qPrintable(text));
        QVERIFY(text.size() > QStringLiteral("Error loading Lua script: broken.lua\n").size());
        QCOMPARE(editor.size(), 1);
        QCOMPARE(editor.first().at(0).toString(), script);
        // A file that is no program: chosen and saved, but it does not start.
        const QString notAProgram = dir.filePath(QStringLiteral("notes.txt"));
        writeFile(notAProgram, "text");
        app->automation().editWith(QUrl::fromLocalFile(notAProgram), script);
        QCOMPARE(notices.size(), 2);
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Automation error"));
        QCOMPARE(notices.last().at(1).toString(), QStringLiteral("Cannot start editor."));
        QCOMPARE(app->settingsStore()->text("automation.scriptEditor"), QDir::toNativeSeparators(notAProgram));
        // With an editor set, it is used without asking.
        app->automation().editScript(script);
        QCOMPARE(editor.size(), 1);
        QCOMPARE(notices.size(), 3);
        // A missing editor is not taken (wxFileExists).
        app->automation().editWith(QUrl::fromLocalFile(dir.filePath(QStringLiteral("none.exe"))), script);
        QCOMPARE(notices.size(), 3);
    }

    // Automation::Add (Automation.cpp:1153-1171): Load script lists the
    // script and appends "|<path>" to the Document's Script Info as a
    // "Changing the subtitle header" step; a listed script is not added again.
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
        app->automation().loadScript(QUrl::fromLocalFile(script));
        QCOMPARE(session().historySize(), steps + 1);
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
    }

    // The legacy capture (local-s4-document-scripts-20261013, the legacy
    // Linux app driven through its "Run the last loaded script" hotkey) as
    // the rewrite gives it, case by case: what the scripts saw and did, the
    // boxes, the log window, the editor picker, the modified state.
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

void AutomationDocumentScriptsTests::replaysTheLegacyCapture()
{
    QFile observations(QStringLiteral(HIKARI_S4_OBSERVATIONS));
    QVERIFY(observations.open(QIODevice::ReadOnly));
    const QJsonArray cases =
        QJsonDocument::fromJson(observations.readAll()).object()["cases"].toArray()[0].toObject()["observations"].toArray();
    QCOMPARE(cases.size(), 7);
    const QDir scripts(QStringLiteral(HIKARI_S4_SCRIPTS));
    QFile headFile(scripts.filePath(QStringLiteral("record.lua.in")));
    QVERIFY(headFile.open(QIODevice::ReadOnly));
    const QByteArray head = headFile.readAll();
    // The legacy boxes' text, read from the capture's screenshots (screens/).
    const std::map<QString, QString> boxText{
        {QStringLiteral("no-scripts"), QStringLiteral("This subtitle file does not have any scripts added")},
        {QStringLiteral("only-missing"), QStringLiteral("This subtitle file does not have any scripts added")},
        {QStringLiteral("validation-false"), QStringLiteral("Validation Lua script 'invalid.lua' failed")},
        {QStringLiteral("load-error"), QStringLiteral("Error loading Lua script: broken.lua\n")},
    };
    // Keys legacy adds to Script Info and the rewrite does not: YCbCr Matrix
    // on load (C03-ycbcr-on-load), PlayResX and PlayResY written by
    // GetASSRes when the status bar reads the resolution (SubsGridBase.cpp:
    // 1653-1675; the rewrite only reads them, Y4), and the key the
    // validation function inserts (S4-validation-edits).
    const QStringList notInTheRewrite{QStringLiteral("YCbCr Matrix"), QStringLiteral("PlayResX"),
                                      QStringLiteral("PlayResY"), QStringLiteral("Validated")};

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
            for (const char *name : {"first", "last", "invalid", "raising", "broken"}) {
                QFile body(scripts.filePath(QLatin1String(name) + QStringLiteral(".lua")));
                QVERIFY(body.open(QIODevice::ReadOnly));
                QByteArray text = head;
                text.replace("local tag = TAG", "local tag = \"" + QByteArray(name) + "\"");
                writeFile(folder + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".lua"), text + "\n" + body.readAll());
            }
        const QString out = caseDir.filePath(QStringLiteral("capture.jsonl"));
        writeFile(out, {});
        qputenv("HIKARI_CAPTURE_OUT", out.toLocal8Bit());
        // Settings of its own, as legacy's fresh copy (no script editor set).
        auto o = options();
        o.settingsFile = caseDir.filePath(QStringLiteral("hikari.ini"));
        app = std::make_unique<app::Application>(o);
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
            // The windows legacy opened: a box (its title, and its text where
            // the screenshot shows it), the log window, the editor picker.
            QStringList titles;
            for (const auto &w : step["windows"].toArray())
                titles << w.toObject()["title"].toString();
            const bool box = titles.contains(QStringLiteral("Info")) || titles.contains(QStringLiteral("Error"));
            const bool picker = titles.contains(QStringLiteral("Select a script editor"));

            const auto before = records().size();
            const auto doneBefore = done.size(), noticesBefore = notices.size(), editorBefore = editor.size();
            automation.runLastLoadedScript();
            QTRY_VERIFY_WITH_TIMEOUT(done.size() > doneBefore || notices.size() > noticesBefore, 30'000);
            // A box a failed run brings after its end, the picker after a load error.
            QTRY_VERIFY_WITH_TIMEOUT(notices.size() - noticesBefore >= (box ? 1 : 0) &&
                                         editor.size() - editorBefore >= (picker ? 1 : 0),
                                     30'000);
            QTest::qWait(300); // nothing more comes
            const auto got = records().mid(before);

            // Legacy's records, less the validations its Automation menu build
            // made first: the hotkey goes through the menu item, and
            // OnMenuSelected rebuilds the item's menu before running it
            // (HikariSubFrame.cpp:680-686, BuildMenu validates every macro,
            // Automation.cpp:1375, 1434). The rewrite validates when it runs.
            // The command's validation is the last one.
            const QJsonArray all = step["records"].toArray();
            qsizetype command = -1;
            for (qsizetype i = 0; i < all.size(); ++i)
                if (all[i].toObject()["event"].toString() == QStringLiteral("validate"))
                    command = i;
            QStringList expectedLoads, gotLoads, expected, actual;
            auto view = [&](const QJsonObject &r) {
                QString first = r["first"].toString();
                // S4-validation-edits: the validation function's edit is dropped.
                if (first == QStringLiteral("edited while validating"))
                    first = QStringLiteral("Gate");
                QStringList info;
                for (const auto &k : r["info"].toArray())
                    if (!notInTheRewrite.contains(k.toString()))
                        info << k.toString();
                return r["script"].toString() + QLatin1Char(' ') + r["event"].toString() + QLatin1Char(' ') + first +
                       QLatin1Char(' ') + QString::number(r["lines"].toInt()) + QLatin1Char(' ') + info.join(QLatin1Char(','));
            };
            for (qsizetype i = 0; i < all.size(); ++i) {
                const QJsonObject r = all[i].toObject();
                if (r["event"].toString() == QStringLiteral("load"))
                    expectedLoads << r["script"].toString();
                else if (r["event"].toString() != QStringLiteral("validate") || i == command)
                    expected << view(r);
            }
            for (const QJsonObject &r : got) {
                if (r["event"].toString() == QStringLiteral("load"))
                    gotLoads << r["script"].toString();
                else
                    actual << view(r);
            }
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
                                (notices.isEmpty() ? QString() : notices.last().at(1).toString())));
            if (box) {
                QCOMPARE(notices.last().at(0).toString(), titles.contains(QStringLiteral("Info")) ? QStringLiteral("Info")
                                                                                                  : QStringLiteral("Error"));
                if (const auto text = boxText.find(id); text != boxText.end())
                    QVERIFY2(notices.last().at(1).toString().startsWith(text->second),
                             qPrintable(where + QStringLiteral(": ") + notices.last().at(1).toString()));
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
    QVERIFY(QDir::setCurrent(dir.path()));
}

QTEST_MAIN(AutomationDocumentScriptsTests)
#include "automation_document_scripts_tests.moc"
