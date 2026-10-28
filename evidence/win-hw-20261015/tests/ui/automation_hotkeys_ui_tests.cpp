// S2 through the composition with real Lua scripts: every registered macro is
// listed under its legacy name; mapping is staged until OK and a taken
// shortcut moves; bindings persist and run their macro; a binding whose script
// is gone stays visibly unresolved (S44-macro-alias); a legacy Hotkeys.txt
// adds only what has no binding yet.

#include "hikari/app/application.h"

#include <QDeadlineTimer>
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

constexpr auto kEdgeblur = "Script macro-1-edgeblur.lua-0";
constexpr auto kPrefix = "Script shell-fixture.lua-0";
constexpr auto kWait = "Script shell-fixture.lua-1";

bool loadScripts(app::Application &a)
{
    a.automation().load(HIKARI_AUTOLOAD_DIR "/macro-1-edgeblur.lua");
    a.automation().load(HIKARI_LUA_FIXTURES "/shell-fixture.lua");
    QDeadlineTimer deadline(30'000);
    while (!deadline.hasExpired()) {
        const auto scripts = a.automation().scripts();
        if (scripts.size() == 2 && std::ranges::none_of(scripts, [](const auto &s) {
                return s.state == application::ScriptStatus::State::Loading;
            }))
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return false;
}

QVariantMap row(app::AutomationHotkeysController &c, const char *name)
{
    for (const QVariant &v : c.rows())
        if (v.toMap().value(QStringLiteral("legacyName")).toString() == QLatin1String(name))
            return v.toMap();
    return {};
}

} // namespace

class AutomationHotkeysUiTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    app::Application::Options options()
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("hikari.ini"));
        o.playbackAudio = false;
        return o;
    }

private slots:
    void mapPersistAndRun()
    {
        const QString path = dir.filePath(QStringLiteral("ep.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n");
        }
        {
            app::Application a(options());
            QVERIFY(loadScripts(a));
            auto &hotkeys = a.automationHotkeys();
            hotkeys.begin();
            QCOMPARE(row(hotkeys, kEdgeblur).value(QStringLiteral("macro")).toString().isEmpty(), false);
            QCOMPARE(row(hotkeys, kWait).value(QStringLiteral("macro")).toString(), QStringLiteral("Wait for cancel"));
            hotkeys.setKeys(QLatin1String(kPrefix), QStringLiteral("Ctrl+Shift+P"));
            // A taken shortcut: reported, and moved when set anyway.
            QCOMPARE(hotkeys.conflict(QLatin1String(kEdgeblur), QStringLiteral("Ctrl+Shift+P")), QLatin1String(kPrefix));
            hotkeys.setKeys(QLatin1String(kEdgeblur), QStringLiteral("Ctrl+Shift+P"));
            QCOMPARE(row(hotkeys, kPrefix).value(QStringLiteral("keys")).toString(), QString());
            QCOMPARE(row(hotkeys, kEdgeblur).value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Shift+P"));
            // Nothing applies before OK; Cancel drops the edits.
            QVERIFY(hotkeys.shortcuts().isEmpty());
            hotkeys.cancel();
            QCOMPARE(row(hotkeys, kEdgeblur).value(QStringLiteral("keys")).toString(), QString());
            hotkeys.setKeys(QLatin1String(kEdgeblur), QStringLiteral("Ctrl+Shift+E"));
            hotkeys.setKeys(QStringLiteral("Script gone.lua-0"), QStringLiteral("F8"));
            hotkeys.commit();
            QCOMPARE(hotkeys.shortcuts().size(), qsizetype(1)); // the missing script's binding does not resolve
        }
        app::Application again(options());
        auto &hotkeys = again.automationHotkeys();
        QVERIFY(again.openFile(path));
        QVERIFY(loadScripts(again));
        QCOMPARE(hotkeys.shortcuts().size(), qsizetype(1));
        QVERIFY(!row(hotkeys, "Script gone.lua-0").value(QStringLiteral("problem")).toString().isEmpty());
        // The shortcut runs its macro on the editing target.
        QSignalSpy done(&again.automation(), &app::AutomationShell::runCompleted);
        QVERIFY(hotkeys.run(QLatin1String(kEdgeblur)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(done.first().at(0).toBool());
        const auto &text = again.files().session(*again.workspace().editingTarget())->document().lines().front()->text;
        QVERIFY(std::u8string_view(text) != u8"Gate");
    }

    void legacyImportAddsOnlyWhatIsFree()
    {
        app::Application a(options()); // keeps Ctrl+Shift+E on edgeblur from the test above
        QVERIFY(loadScripts(a));
        auto &hotkeys = a.automationHotkeys();
        hotkeys.begin();
        const QString legacy = dir.filePath(QStringLiteral("Hotkeys.txt"));
        {
            QFile f(legacy);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[HikariSub 0.8.0.1200]\r\n"
                    "Script macro-1-edgeblur.lua-0=Ctrl-Alt-B\r\n" // has a binding: kept
                    "Script shell-fixture.lua-0=Ctrl-Shift-E\r\n"  // keys taken: skipped
                    "Script shell-fixture.lua-1=Ctrl-Alt-W\r\n");  // added
        }
        QCOMPARE(hotkeys.importLegacy(legacy), 1);
        QCOMPARE(row(hotkeys, kEdgeblur).value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Shift+E"));
        QCOMPARE(row(hotkeys, kWait).value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Alt+W"));
        QCOMPARE(row(hotkeys, kPrefix).value(QStringLiteral("keys")).toString(), QString());
        QCOMPARE(hotkeys.keysOf(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier), QStringLiteral("Ctrl+Shift+K"));
        QCOMPARE(hotkeys.keysOf(Qt::Key_Shift, Qt::ShiftModifier), QString());
    }
};

QTEST_MAIN(AutomationHotkeysUiTests)
#include "automation_hotkeys_ui_tests.moc"
