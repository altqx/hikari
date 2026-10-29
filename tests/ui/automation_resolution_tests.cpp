#include "hikari/app/application.h"
#include "hikari/application/script_resolution.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

const QString kScript = QStringLiteral(HIKARI_LUA_FIXTURES "/script-resolution.lua");

QByteArray assWith(const QByteArray &info)
{
    return "[Script Info]\nTitle: playres\n" + info + "ScriptType: v4.00+\n\n[V4+ Styles]\n"
           "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
           "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
           "Alignment, MarginL, MarginR, MarginV, Encoding\n"
           "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
           "10,10,10,1\n\n"
           "[Events]\n"
           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n";
}

std::string str(const std::optional<std::u8string> &v)
{
    return v ? std::string(v->begin(), v->end()) : std::string("<none>");
}

}

class AutomationResolutionTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> app;
    // MUST KILL: a bare QObject named view stands in for the window that shows the question
    QObject view;
    int files = 0;

    app::Application::Options options()
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        return o;
    }
    ui::ScriptResolutionPromptController &prompt() { return *app->automation().resolutionPrompt(); }
    application::EditSession &session() { return *app->files().session(*app->workspace().editingTarget()); }
    const core::Document &document() { return session().document(); }
    std::string firstText()
    {
        const auto &t = document().lines().front()->text;
        return std::string(t.begin(), t.end());
    }
    bool open(const QByteArray &info)
    {
        const QString path = dir.filePath(QStringLiteral("s%1.ass").arg(++files));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(assWith(info));
        f.close();
        if (!app->openFile(path))
            return false;
        app->selectTab(files - 1);
        return QTest::qWaitFor([&] {
            const auto target = app->workspace().editingTarget();
            const auto destination = target ? app->files().destination(*target) : std::nullopt;
            return destination && QFileInfo(QString::fromStdString(destination->value)).fileName() ==
                                      QFileInfo(path).fileName();
        }, 5000);
    }
    bool loadScript()
    {
        app->automation().load(kScript.toStdString());
        QDeadlineTimer deadline(30'000);
        while (!deadline.hasExpired()) {
            for (const auto &s : app->automation().scripts())
                if (s.path == kScript.toStdString() && s.state == application::ScriptStatus::State::Ready)
                    return true;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        return false;
    }
    bool loadReady(const std::string &path)
    {
        app->automation().load(path);
        return QTest::qWaitFor([&] {
            for (const auto &s : app->automation().scripts())
                if (s.path == path && s.state == application::ScriptStatus::State::Ready)
                    return true;
            return false;
        }, 30'000);
    }
    bool run(const std::string &name, const QString &script = kScript)
    {
        for (const auto &s : app->automation().scripts())
            if (s.path == script.toStdString())
                for (std::size_t i = 0; i < s.info.macros.size(); ++i)
                    if (s.info.macros[i].name == name)
                        return app->automation().run(script.toStdString(), static_cast<int>(i));
        return false;
    }
    bool ended()
    {
        return QTest::qWaitFor([&] { return !app->automation().running(); }, 30'000);
    }
    bool openVideo()
    {
        app->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        return QTest::qWaitFor([&] { return app->video().session().lastFrame() != nullptr; }, 20'000);
    }
    std::vector<std::string> steps()
    {
        std::vector<std::string> out;
        for (const auto &step : session().history())
            out.push_back(step.name);
        return out;
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }
    void init()
    {
        files = 0;
        app = std::make_unique<app::Application>(options());
        prompt().setView(&view);
        QVERIFY(loadScript());
    }
    void cleanup()
    {
        app.reset();
        for (const char *name : {"settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(dir.filePath(QLatin1String(name)));
    }

    void aMissingResolutionIsAskedForWithTheVideosSize()
    {
        QVERIFY(open(""));
        QVERIFY(openVideo());
        const auto before = steps();
        QVERIFY(run("Read resolution"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        QCOMPARE(prompt().script(), QStringLiteral("script-resolution.lua"));
        QVERIFY(prompt().hasVideo());
        QCOMPARE(prompt().videoWidth(), 320);
        QCOMPARE(prompt().videoHeight(), 240);
        QCOMPARE(prompt().width(), 320);
        QCOMPARE(prompt().height(), 240);
        QVERIFY(app->automation().running());
        prompt().accept(320, 240, false);
        QVERIFY(ended());
        QVERIFY(!prompt().isOpen());
        QVERIFY2(app->automation().log().contains(QStringLiteral("320 x 240, 320 x 240, PlayResX=320;PlayResY=240")),
                 qPrintable(app->automation().log()));
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("320"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("240"));
        auto after = before;
        after.push_back(std::string(application::kScriptResolutionStep));
        QCOMPARE(steps(), after);
        QVERIFY(app->automation().lastMessage().isEmpty());
        QVERIFY(!app->settingsStore()->boolean("automation.scriptResolutionFromVideo"));
        QVERIFY(session().undo());
        QVERIFY(!document().scriptInfo(u8"PlayResX"));
        QVERIFY(!document().scriptInfo(u8"PlayResY"));
    }

    void withoutAVideoItStartsFromLegacysValues()
    {
        QVERIFY(open(""));
        QVERIFY(run("Read resolution"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        QVERIFY(!prompt().hasVideo());
        QCOMPARE(prompt().width(), 1280);
        QCOMPARE(prompt().height(), 720);
        prompt().accept(1280, 720, true);
        QVERIFY(ended());
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("1280"));
        QVERIFY(!app->settingsStore()->boolean("automation.scriptResolutionFromVideo"));

        QVERIFY(open("PlayResY: 480\n"));
        QVERIFY(run("Karaskel head"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        QCOMPARE(prompt().width(), 853);
        QCOMPARE(prompt().height(), 480);
        prompt().accept(1024, 576, false);
        QVERIFY(ended());
        QVERIFY2(app->automation().log().contains(QStringLiteral("1024 x 576")), qPrintable(app->automation().log()));
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("1024"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("576"));
    }

    void cancelStopsTheMacro()
    {
        QVERIFY(open(""));
        const auto before = steps();
        QVERIFY(run("Edit and read"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().reject();
        QVERIFY(ended());
        QVERIFY(!prompt().isOpen());
        QCOMPARE(firstText(), std::string("first"));
        QVERIFY(!document().scriptInfo(u8"PlayResX"));
        QCOMPARE(steps(), before);
        QVERIFY(app->automation().lastMessage().isEmpty());
        QVERIFY(!session().isReadOnly());
    }

    void theResolutionAndTheMacrosEditsAreTwoSteps()
    {
        QVERIFY(open(""));
        QVERIFY(openVideo());
        const auto before = steps();
        QVERIFY(run("Edit and read"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().accept(640, 480, false);
        QVERIFY(ended());
        QVERIFY2(app->automation().log().contains(QStringLiteral("640 x 480")), qPrintable(app->automation().log()));
        QCOMPARE(firstText(), std::string("edited"));
        auto after = before;
        after.push_back(std::string(application::kScriptResolutionStep));
        after.push_back("Automation: Edit and read");
        QCOMPARE(steps(), after);
        QVERIFY(session().undo());
        QCOMPARE(firstText(), std::string("first"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("640"));
        QVERIFY(session().undo());
        QVERIFY(!document().scriptInfo(u8"PlayResX"));
    }

    void alwaysFromTheVideoSkipsTheQuestion()
    {
        QVERIFY(open(""));
        QVERIFY(openVideo());
        QVERIFY(run("Read resolution"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().accept(320, 240, true);
        QVERIFY(ended());
        QVERIFY(app->settingsStore()->boolean("automation.scriptResolutionFromVideo"));

        QVERIFY(open("PlayResX: 1920\n"));
        QVERIFY(openVideo());
        QSignalSpy asked(&prompt(), &ui::ScriptResolutionPromptController::changed);
        const auto before = steps();
        QVERIFY(run("Read resolution"));
        QVERIFY(ended());
        QCOMPARE(asked.count(), 0);
        QVERIFY2(app->automation().log().contains(QStringLiteral("320 x 240, 320 x 240, PlayResX=320;PlayResY=240")),
                 qPrintable(app->automation().log()));
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("320"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("240"));
        auto after = before;
        after.push_back(std::string(application::kScriptResolutionStep));
        QCOMPARE(steps(), after);

        QVERIFY(open(""));
        QVERIFY(!app->video().hasVideo());
        QVERIFY(run("Read resolution"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().reject();
        QVERIFY(ended());
    }

    void aWrittenValueIsReplacedByTheVideosSuggestion()
    {
        QVERIFY(open("PlayResY: 480\n"));
        QVERIFY(openVideo());
        QVERIFY(run("Read resolution"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        QCOMPARE(prompt().width(), 320);
        QCOMPARE(prompt().height(), 240);
        QCOMPARE(prompt().videoWidth(), 320);
        QCOMPARE(prompt().videoHeight(), 240);
        prompt().accept(320, 240, false);
        QVERIFY(ended());
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("320"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("240"));
    }

    void aValidationReadIsAskedOnce()
    {
        QVERIFY(open(""));
        QVERIFY(openVideo());
        const auto before = steps();
        int asked = 0;
        connect(&prompt(), &ui::ScriptResolutionPromptController::changed, this, [&] { asked += prompt().isOpen(); });
        QVERIFY(run("Validate and read"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().accept(640, 480, false);
        QVERIFY(ended());
        disconnect(&prompt(), nullptr, this, nullptr);
        QCOMPARE(asked, 1);
        QVERIFY2(app->automation().log().contains(QStringLiteral("run 640 x 480")), qPrintable(app->automation().log()));
        QCOMPARE(firstText(), std::string("edited"));
        auto after = before;
        after.push_back(std::string(application::kScriptResolutionStep));
        after.push_back("Automation: Validate and read");
        QCOMPARE(steps(), after);
    }

    void cancelInValidationStopsTheMacro()
    {
        QVERIFY(open(""));
        const auto before = steps();
        QVERIFY(run("Validate and read"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().reject();
        QVERIFY(ended());
        QCOMPARE(firstText(), std::string("first"));
        QVERIFY(!document().scriptInfo(u8"PlayResX"));
        QCOMPARE(steps(), before);
        QVERIFY(app->automation().lastMessage().isEmpty());
        QVERIFY(!session().isReadOnly());
    }

    void aRefusingValidationKeepsTheResolutionSet()
    {
        QVERIFY(open(""));
        const auto before = steps();
        QVERIFY(run("Validate refuses"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().accept(1280, 720, false);
        QVERIFY(ended());
        QCOMPARE(firstText(), std::string("first"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("1280"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("720"));
        auto after = before;
        after.push_back(std::string(application::kScriptResolutionStep));
        QCOMPARE(steps(), after);
    }

    void aDocumentChangedUnderTheRunKeepsTheResolution()
    {
        QVERIFY(open(""));
        const auto before = steps();
        QVERIFY(run("Edit and read"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        session().setReadOnly(false);
        QVERIFY(session().run(application::Command{"Outside", session().revision(), {},
                                                   [](core::Document &d) { return d.setScriptInfo(u8"Title", u8"outside"); }}));
        session().setReadOnly(true);
        prompt().accept(640, 480, false);
        QVERIFY(ended());
        QCOMPARE(firstText(), std::string("first"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("640"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("480"));
        auto after = before;
        after.push_back("Outside");
        after.push_back(std::string(application::kScriptResolutionStep));
        QCOMPARE(steps(), after);
        QCOMPARE(app->automation().lastMessage(),
                 QStringLiteral("The document changed while the macro ran; nothing was changed."));
    }

    void nothingIsAskedWhenNothingIsMissingOrRead()
    {
        QSignalSpy asked(&prompt(), &ui::ScriptResolutionPromptController::changed);
        QVERIFY(open("PlayResX: 640\nPlayResY: 360\n"));
        QVERIFY(run("Karaskel head"));
        QVERIFY(ended());
        QVERIFY2(app->automation().log().contains(QStringLiteral("640 x 360")), qPrintable(app->automation().log()));

        QVERIFY(open(""));
        QVERIFY(run("Edit only"));
        QVERIFY(ended());
        QCOMPARE(firstText(), std::string("edited"));
        QVERIFY(!document().scriptInfo(u8"PlayResX"));
        QVERIFY(!document().scriptInfo(u8"PlayResY"));
        QCOMPARE(asked.count(), 0);
    }

    void withoutAWindowNothingWaits()
    {
        prompt().setView(nullptr);
        QVERIFY(!prompt().asking());
        QSignalSpy asked(&prompt(), &ui::ScriptResolutionPromptController::changed);
        QVERIFY(open("PlayResY: 480\n"));
        const auto before = steps();
        QVERIFY(run("Read resolution"));
        QVERIFY(ended());
        QCOMPARE(asked.count(), 0);
        QVERIFY2(app->automation().log().contains(QStringLiteral("853 x 480, 853 x 480, PlayResY=480")),
                 qPrintable(app->automation().log()));
        QVERIFY(!document().scriptInfo(u8"PlayResX"));
        QCOMPARE(steps(), before);
    }

    void cancellingTheRunWithdrawsTheQuestion()
    {
        QVERIFY(open(""));
        QVERIFY(run("Read resolution"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        app->automation().cancelRun();
        QVERIFY(ended());
        QTRY_VERIFY(!prompt().isOpen());
        prompt().accept(320, 240, false);
        QVERIFY(!document().scriptInfo(u8"PlayResX"));
        QCOMPARE(app->automation().lastMessage(), QStringLiteral("The macro was cancelled; nothing was changed."));
    }

    void cancellingAfterSetKeepsTheResolution()
    {
        QVERIFY(open(""));
        const auto before = steps();
        QVERIFY(run("Read and wait"));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        prompt().accept(640, 480, false);
        QTRY_VERIFY(!prompt().isOpen());
        app->automation().cancelRun();
        QVERIFY(ended());
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("640"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("480"));
        auto after = before;
        after.push_back(std::string(application::kScriptResolutionStep));
        QCOMPARE(steps(), after);
        QCOMPARE(app->automation().lastMessage(),
                 QStringLiteral("The macro was cancelled; only the script resolution was set."));
    }

    void aDependencyAsksOnceForTheRun()
    {
        const QString deps = dir.filePath(QStringLiteral("deps"));
        QVERIFY(QDir().mkpath(deps));
        auto write = [](const QString &path, const QByteArray &bytes) {
            QFile f(path);
            return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
        };
        const QString dep = QDir::toNativeSeparators(deps + QStringLiteral("/dep.lua"));
        QVERIFY(write(dep, "aegisub.register_macro(\"Dep\", \"\", function() end, function(subs)\n"
                           "    local w, h = subs.script_resolution()\n"
                           "    return w > 0 and h > 0\n"
                           "end)\n"
                           "return {}\n"));
        const QString main = QDir::toNativeSeparators(deps + QStringLiteral("/main.lua"));
        QVERIFY(write(main, "require \"dep\"\n"
                            "aegisub.register_macro(\"Main\", \"\", function(subs)\n"
                            "    local w, h = subs.script_resolution()\n"
                            "    aegisub.log(\"main %d x %d\", w, h)\n"
                            "end)\n"));
        QVERIFY(loadReady(dep.toStdString()));
        QVERIFY(loadReady(main.toStdString()));
        QVERIFY(open(""));
        const auto before = steps();
        QVERIFY(run("Main", main));
        QTRY_VERIFY_WITH_TIMEOUT(prompt().isOpen(), 30'000);
        QCOMPARE(prompt().script(), QStringLiteral("dep.lua"));
        prompt().accept(640, 480, false);
        QVERIFY(ended());
        QVERIFY(!prompt().isOpen());
        QVERIFY2(app->automation().log().contains(QStringLiteral("main 640 x 480")), qPrintable(app->automation().log()));
        QCOMPARE(str(document().scriptInfo(u8"PlayResX")), std::string("640"));
        QCOMPARE(str(document().scriptInfo(u8"PlayResY")), std::string("480"));
        auto after = before;
        after.push_back(std::string(application::kScriptResolutionStep));
        QCOMPARE(steps(), after);
    }
};

QTEST_MAIN(AutomationResolutionTests)
#include "automation_resolution_tests.moc"
