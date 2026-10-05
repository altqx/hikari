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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

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

QTEST_MAIN(AutomationDocumentScriptsTests)
#include "automation_document_scripts_tests.moc"
