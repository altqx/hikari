#include "hikari/app/application.h"
#include "docking.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

constexpr char kScript[] =
    "script_name = \"Checked macros\"\n"
    "aegisub.register_macro(\"Ready\", \"\", function() end, function() return true, \"Turns the line around\" end)\n"
    "aegisub.register_macro(\"Blocked\", \"\", function() end, function() return false, \"Select a karaoke line\" end)\n"
    "aegisub.register_macro(\"Silent no\", \"\", function() end, function() return false end)\n"
    "aegisub.register_macro(\"Broken\", \"\", function() end, function() error(\"no way\") end)\n"
    "aegisub.register_macro(\"Forever\", \"\", function() end, function() local n = 0; while true do n = n + 1 end end)\n"
    "aegisub.register_macro(\"On\", \"\", function() end, nil, function() return true end)\n"
    "aegisub.register_macro(\"Off\", \"\", function() end, nil, function() return false end)\n"
    "aegisub.register_macro(\"Plain\", \"\", function(subs, sel)\n"
    "    local l = subs[sel[1]]; l.text = l.text .. \"+\"; subs[sel[1]] = l\n"
    "end)\n";

}

class AutomationMenuChecksTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> app;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QObject *root = nullptr;
    QString script;

    QObject *named(const QString &name) const { return root->findChild<QObject *>(name); }
    QObject *row(const char *macro) const { return named(QStringLiteral("macro_") + QLatin1String(macro)); }
    QString firstLine() const
    {
        const auto &text = app->files().session(*app->workspace().editingTarget())->document().lines().front()->text;
        return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size()));
    }
    int ordinal(const char *macro) const
    {
        for (const auto &s : app->automation().scripts())
            if (s.path == script.toStdString())
                for (std::size_t i = 0; i < s.info.macros.size(); ++i)
                    if (s.info.macros[i].name == macro)
                        return static_cast<int>(i);
        return -1;
    }
    QVariantMap check(const char *macro) const { return app->automation().macroCheck(script, ordinal(macro)); }
    bool checking() const { return check("Ready").contains(QStringLiteral("note")); }
    void openMenu()
    {
        QObject *menu = named(QStringLiteral("automationMenu"));
        QVERIFY(QMetaObject::invokeMethod(menu, "open"));
        QTRY_VERIFY(menu->property("opened").toBool());
    }
    void closeMenu()
    {
        QObject *menu = named(QStringLiteral("automationMenu"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
    }

private slots:
    void init()
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        app = std::make_unique<app::Application>(o);
        const QString path = dir.filePath(QStringLiteral("checks.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n");
        f.close();
        QVERIFY(app->openFile(path));
        script = dir.filePath(QStringLiteral("checked-macros.lua"));
        QFile s(script);
        QVERIFY(s.open(QIODevice::WriteOnly | QIODevice::Truncate));
        s.write(kScript);
        s.close();
        app->automation().load(script.toStdString());
        QDeadlineTimer deadline(30'000);
        const auto ready = [this] {
            return std::ranges::any_of(app->automation().scripts(), [this](const auto &st) {
                return st.path == script.toStdString() && st.state == application::ScriptStatus::State::Ready;
            });
        };
        while (!ready() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(ready());
        engine = std::make_unique<QQmlApplicationEngine>();
        ui::attachDocking(*engine);
        engine->setInitialProperties(app->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "buildMenus"));
        root = engine->rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        window->resize(1280, 800);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // MUST KILL: rejects a crash-session prompt an earlier case left open
        if (auto *prompt = named(QStringLiteral("crashSessionPrompt")); prompt && prompt->property("visible").toBool())
            QVERIFY(QMetaObject::invokeMethod(prompt, "reject"));
        QTRY_VERIFY(!root->property("workspaceLayout").value<QObject *>()->property("modalOpen").toBool());
    }
    void cleanup()
    {
        engine.reset();
        root = nullptr;
        app.reset();
    }

    void openingTheMenuGreysOutMacrosThatCantRunAndMarksActiveOnes()
    {
        app->automation().menuOpened();
        QCOMPARE(check("Ready").value(QStringLiteral("note")).toString(), QStringLiteral("Checking…"));
        QVERIFY2(check("Plain").isEmpty(), "a macro with neither function isn't checked");
        QTRY_VERIFY_WITH_TIMEOUT(!checking(), 10'000);

        openMenu();
        QTRY_VERIFY(row("Ready"));
        QVERIFY(row("Ready")->property("enabled").toBool());
        QCOMPARE(row("Ready")->property("reason").toString(), QString());
        QCOMPARE(row("Ready")->property("help").toString(), QStringLiteral("Turns the line around"));
        QVERIFY(!row("Blocked")->property("enabled").toBool());
        QCOMPARE(row("Blocked")->property("reason").toString(), QStringLiteral("Select a karaoke line"));
        QCOMPARE(row("Silent no")->property("reason").toString(), QStringLiteral("Can’t run on this selection"));
        QCOMPARE(row("Broken")->property("reason").toString(),
                 QStringLiteral("Its validation function stopped with an error"));
        QVERIFY2(row("Forever")->property("enabled").toBool(), "a validation past its budget leaves the macro enabled");
        QCOMPARE(row("Forever")->property("reason").toString(), QString());
        QVERIFY(row("On")->property("checkable").toBool());
        QVERIFY(row("On")->property("checked").toBool());
        QVERIFY(row("Off")->property("checkable").toBool());
        QVERIFY(!row("Off")->property("checked").toBool());
        QVERIFY(!row("Plain")->property("checkable").toBool());
        QVERIFY(row("Plain")->property("enabled").toBool());
        QCOMPARE(row("Plain")->property("description").toString(), QString());
        closeMenu();

        QSignalSpy changed(&app->automation(), &app::AutomationShell::checksChanged);
        app->automation().menuOpened();
        QCOMPARE(changed.size(), 0);
        QVERIFY2(!checking(), "the same revision, selection and active line use the cached answers");

        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        openMenu();
        QVERIFY(QMetaObject::invokeMethod(row("On"), "click"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(done.first().at(0).toBool());
        QVERIFY2(row("On")->property("checked").toBool(), "running a checked macro keeps its mark");
    }

    void aMacroChosenWhileTheMenuChecksRunsAndTheNextOpeningChecksAgain()
    {
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        QVERIFY(app->automation().run(script.toStdString(), ordinal("Plain")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QCOMPARE(firstLine(), QStringLiteral("Gate+"));

        openMenu();
        QVERIFY(checking());
        QTRY_VERIFY(row("Plain"));
        QTRY_COMPARE(row("Ready")->property("description").toString(), QStringLiteral("Checking…"));
        QVERIFY2(row("Ready")->property("enabled").toBool(), "a row being checked stays clickable");
        QVERIFY(QMetaObject::invokeMethod(row("Plain"), "triggered"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 10'000);
        QVERIFY(done.last().at(0).toBool());
        QCOMPARE(firstLine(), QStringLiteral("Gate++"));
        QVERIFY(!checking());

        app->automation().menuOpened();
        QVERIFY2(checking(), "the edit is a new revision");
        QTRY_VERIFY_WITH_TIMEOUT(!checking(), 10'000);
        QCOMPARE(check("Blocked").value(QStringLiteral("reason")).toString(), QStringLiteral("Select a karaoke line"));
    }
};

QTEST_MAIN(AutomationMenuChecksTests)
#include "automation_menu_checks_tests.moc"
