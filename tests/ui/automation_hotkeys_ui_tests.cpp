// S2, AM1 through the composition with real Lua scripts: a macro's
// shortcut is set through the shortcut editor's gesture path (the Automation
// panel records it there) at once, saved and installed; a taken shortcut
// asks first and moves; bindings persist and run their macro; a binding
// whose script is gone stays visible as missing (S44-macro-alias); a legacy
// Hotkeys.txt adds only what has no binding yet; Settings → Hotkeys lists
// every macro, by name, under the Automation filter; a binding left missing
// by a changed script resolves again once its shortcut is recorded.

#include "hikari/app/application.h"
#include "hikari/app/hotkeys_controller.h"
#include "hikari/application/settings.h"

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

QString keysOf(app::AutomationHotkeysController &c, const char *name)
{
    return c.keys().value(QLatin1String(name)).toString();
}

QVariantMap missing(app::AutomationHotkeysController &c, const char *name)
{
    for (const QVariant &v : c.missing())
        if (v.toMap().value(QStringLiteral("legacyName")).toString() == QLatin1String(name))
            return v.toMap();
    return {};
}

// The panel's Enter: the gesture's question, then the answer ("cancel"
// sets the keys when nothing holds them).
QVariantMap map(app::HotkeysController &h, const char *name, const char *accel, const char *answer = "cancel")
{
    const QVariantMap target = h.gestureTarget(QLatin1String(name));
    const QVariantMap conflict = h.gestureConflict(target.value("id").toInt(), QLatin1String(accel), 0);
    h.gestureMap(target.value("id").toInt(), target.value("name").toString(), QLatin1String(accel), 0,
                 conflict.isEmpty() ? QStringLiteral("cancel") : QLatin1String(answer));
    return conflict;
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
            auto &macros = a.automationHotkeys();
            auto &h = a.hotkeys();
            QVERIFY(map(h, kPrefix, "Ctrl-Shift-P").isEmpty());
            // At once: saved and installed, no OK.
            QCOMPARE(keysOf(macros, kPrefix), QStringLiteral("Ctrl+Shift+P"));
            QCOMPARE(macros.shortcuts().size(), qsizetype(1));
            // A taken shortcut asks; "delete" (the panel's Reassign) moves it.
            QVERIFY(!map(h, kEdgeblur, "Ctrl-Shift-P", "delete").isEmpty());
            QCOMPARE(keysOf(macros, kPrefix), QString());
            QCOMPARE(keysOf(macros, kEdgeblur), QStringLiteral("Ctrl+Shift+P"));
            QCOMPARE(h.holdersOf(QStringLiteral("Ctrl-Shift-P"), h.gestureTarget(QLatin1String(kPrefix)).value("id").toInt()),
                     QStringList{QStringLiteral("Add edgeblur")});
            // A Global command's keys: the holder is named, and its binding
            // takes them from a macro that keeps them anyway.
            QCOMPARE(h.holdersOf(QStringLiteral("Ctrl-S"), -1), QStringList{QStringLiteral("Save")});
            QCOMPARE(h.globalHolder(QStringLiteral("Ctrl+S")), QStringLiteral("Save"));
            QCOMPARE(h.globalHolder(QStringLiteral("Ctrl+Shift+P")), QString());
            QVERIFY(map(h, kEdgeblur, "Ctrl-Shift-E").isEmpty());
            macros.replaceCommitted({{kEdgeblur, "Ctrl+Shift+E"}, {"Script gone.lua-0", "F8"}});
            macros.save();
            macros.install();
            QCOMPARE(macros.shortcuts().size(), qsizetype(1)); // the missing script's binding does not resolve
            // Clear: at once.
            QVERIFY(map(h, kWait, "Ctrl-Alt-Q").isEmpty());
            QCOMPARE(keysOf(macros, kWait), QStringLiteral("Ctrl+Alt+Q"));
            macros.clearKeys(QLatin1String(kWait));
            QCOMPARE(keysOf(macros, kWait), QString());
            QCOMPARE(macros.shortcuts().size(), qsizetype(1));
        }
        app::Application again(options());
        auto &macros = again.automationHotkeys();
        QVERIFY(again.openFile(path));
        QVERIFY(loadScripts(again));
        QCOMPARE(macros.shortcuts().size(), qsizetype(1));
        QCOMPARE(keysOf(macros, kEdgeblur), QStringLiteral("Ctrl+Shift+E"));
        const QVariantMap gone = missing(macros, "Script gone.lua-0");
        QCOMPARE(gone.value(QStringLiteral("keys")).toString(), QStringLiteral("F8"));
        QCOMPARE(gone.value(QStringLiteral("problem")).toString(), QStringLiteral("No loaded script has this macro"));
        // Settings → Hotkeys: every macro is listed by name, the bound and
        // the unbound, and the missing one with why.
        auto &h = again.hotkeys();
        h.beginOptions();
        h.filter(7);
        QStringList shown;
        QString goneNote;
        for (const QVariant &v : h.rows()) {
            const QVariantMap r = v.toMap();
            QVERIFY(r.contains(QStringLiteral("script")));
            shown << r.value(QStringLiteral("script")).toString() + " > " + r.value(QStringLiteral("macro")).toString() + " = " +
                         r.value(QStringLiteral("accel")).toString();
            if (r.value(QStringLiteral("text")).toString() == QStringLiteral("Global Script gone.lua-0"))
                goneNote = r.value(QStringLiteral("note")).toString();
        }
        QVERIFY2(shown.contains(QStringLiteral("Shell fixture > Wait for cancel = ")), qPrintable(shown.join('\n')));
        QVERIFY2(shown.contains(QStringLiteral("Add edgeblur > Add edgeblur = Ctrl-Shift-E")), qPrintable(shown.join('\n')));
        QCOMPARE(goneNote, QStringLiteral("No loaded script has this macro"));
        // An unbound macro's row maps there too, into the same binding.
        int waitRow = -1;
        for (int i = 0; i < h.rows().size(); ++i)
            if (h.rows().at(i).toMap().value(QStringLiteral("macro")).toString() == QStringLiteral("Wait for cancel"))
                waitRow = i;
        QVERIFY(waitRow >= 0);
        h.select(waitRow);
        h.optionsMap(QStringLiteral("Ctrl-Alt-W"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(keysOf(macros, kWait), QStringLiteral("Ctrl+Alt+W"));
        // The shortcut runs its macro on the editing target.
        QSignalSpy done(&again.automation(), &app::AutomationShell::runCompleted);
        QVERIFY(macros.run(QLatin1String(kEdgeblur)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30'000);
        QVERIFY(done.first().at(0).toBool());
        const auto &text = again.files().session(*again.workspace().editingTarget())->document().lines().front()->text;
        QVERIFY(std::u8string_view(text) != u8"Gate");
    }

    // A binding made for an earlier version of its script is missing until
    // its shortcut is recorded again, with new keys or the same: it is then
    // made for the macro loaded now.
    void rerecordingAChangedMacroBindsItAgain()
    {
        app::Application::Options o = options();
        o.settingsFile = dir.filePath(QStringLiteral("changed.ini"));
        {
            app::Application a(o);
            a.settingsStore()->set(application::kAutomationHotkeysSetting.data(),
                                   QStringList{QStringLiteral("Script shell-fixture.lua-0\tCtrl+Alt+K\tPrefix from dialog\tearlier"),
                                               QStringLiteral("Script shell-fixture.lua-1\tCtrl+Alt+L\tWait for cancel\tearlier"),
                                               QStringLiteral("Script macro-1-edgeblur.lua-0\tCtrl+Alt+E\tAdd edgeblur\tearlier")});
        }
        app::Application a(o);
        QVERIFY(loadScripts(a));
        auto &macros = a.automationHotkeys();
        auto &h = a.hotkeys();
        QCOMPARE(missing(macros, kPrefix).value(QStringLiteral("problem")).toString(),
                 QStringLiteral("The script or its macro changed"));
        QCOMPARE(missing(macros, kWait).value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Alt+L"));
        QVERIFY(macros.shortcuts().isEmpty());
        QVERIFY(map(h, kPrefix, "Ctrl-Alt-M").isEmpty());
        QCOMPARE(keysOf(macros, kPrefix), QStringLiteral("Ctrl+Alt+M"));
        QVERIFY(missing(macros, kPrefix).isEmpty());
        QSignalSpy committed(&macros, &app::AutomationHotkeysController::committed);
        QVERIFY(map(h, kWait, "Ctrl-Alt-L").isEmpty());
        QCOMPARE(committed.size(), 1); // saved and installed at once
        QCOMPARE(keysOf(macros, kWait), QStringLiteral("Ctrl+Alt+L"));
        QCOMPARE(macros.missing().size(), qsizetype(1));
        QCOMPARE(macros.shortcuts().size(), qsizetype(2));
        // Recorded again as it now is: nothing to save.
        QVERIFY(map(h, kWait, "Ctrl-Alt-L").isEmpty());
        QCOMPARE(committed.size(), 1);
        // The same in Settings → Hotkeys, its keys given again there.
        h.beginOptions();
        h.filter(7);
        int edgeblur = -1;
        for (int i = 0; i < h.rows().size(); ++i)
            if (h.rows().at(i).toMap().value(QStringLiteral("text")).toString() == QStringLiteral("Global ") + kEdgeblur)
                edgeblur = i;
        QVERIFY(edgeblur >= 0);
        h.select(edgeblur);
        h.optionsMap(QStringLiteral("Ctrl-Alt-E"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(keysOf(macros, kEdgeblur), QStringLiteral("Ctrl+Alt+E"));
        QVERIFY(macros.missing().isEmpty());
        QCOMPARE(macros.shortcuts().size(), qsizetype(3));
        // Set default clears them (in memory); the loaded macros stay
        // listed there, unbound, to be mapped again.
        h.beginOptions();
        h.resetDefaults();
        h.filter(7);
        QStringList listed;
        for (const QVariant &v : h.rows())
            if (v.toMap().contains(QStringLiteral("script")))
                listed << v.toMap().value(QStringLiteral("macro")).toString() + " = " + v.toMap().value(QStringLiteral("accel")).toString();
        listed.sort();
        QCOMPARE(listed, (QStringList{QStringLiteral("Add edgeblur = "), QStringLiteral("Prefix from dialog = "),
                                      QStringLiteral("Wait for cancel = ")}));
    }

    // AM1-hotkeys-window-retired: the window's command given Audio keys (the
    // Audio list is stored apart) is dropped there too, once.
    void retiredWindowAudioBindingIsDropped()
    {
        app::Application::Options o = options();
        o.settingsFile = dir.filePath(QStringLiteral("retired.ini"));
        ui::SettingsStore(o.settingsFile)
            .set(application::kAudioHotkeysSetting.data(),
                 QStringList{QStringLiteral("GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW A=Ctrl-Shift-H"), QStringLiteral("AUDIO_COMMIT_ALT A=G")});
        app::Application a(o);
        QVERIFY(a.log().history().contains(QStringLiteral("Open shortcut mapping window")));
        QCOMPARE(a.settingsStore()->list(application::kAudioHotkeysSetting.data()), QStringList{QStringLiteral("AUDIO_COMMIT_ALT A=G")});
    }

    void legacyImportAddsOnlyWhatIsFree()
    {
        app::Application a(options()); // keeps Ctrl+Shift+E on edgeblur from the test above
        QVERIFY(loadScripts(a));
        auto &hotkeys = a.automationHotkeys();
        hotkeys.clearKeys(QLatin1String(kWait));
        const QString legacy = dir.filePath(QStringLiteral("Hotkeys.txt"));
        {
            QFile f(legacy);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[HikariSub 0.8.0.1200]\r\n"
                    "Script macro-1-edgeblur.lua-0=Ctrl-Alt-B\r\n" // has a binding: kept
                    "Script shell-fixture.lua-0=Ctrl-Shift-E\r\n"  // keys taken: skipped
                    "Script shell-fixture.lua-1=Ctrl-Alt-W\r\n");  // added
        }
        QSignalSpy committed(&hotkeys, &app::AutomationHotkeysController::committed);
        QCOMPARE(hotkeys.importLegacy(legacy), 1);
        QCOMPARE(committed.size(), 1); // saved and installed at once
        QCOMPARE(keysOf(hotkeys, kEdgeblur), QStringLiteral("Ctrl+Shift+E"));
        QCOMPARE(keysOf(hotkeys, kWait), QStringLiteral("Ctrl+Alt+W"));
        QCOMPARE(keysOf(hotkeys, kPrefix), QString());
        QCOMPARE(hotkeys.shortcuts().size(), qsizetype(2));
        QCOMPARE(hotkeys.keysOf(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier), QStringLiteral("Ctrl+Shift+K"));
        QCOMPARE(hotkeys.keysOf(Qt::Key_Shift, Qt::ShiftModifier), QString());
    }
};

QTEST_MAIN(AutomationHotkeysUiTests)
#include "automation_hotkeys_ui_tests.moc"
