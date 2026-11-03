#include "hikari/app/application.h"
#include "hikari/app/hotkeys_controller.h"
#include "hikari/app/localisation.h"
#include "hikari/app/plugin_runtime.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using backends::PluginProcess;

namespace {

const QString kGreet = QStringLiteral("plugin.com.example.hello.greet");

struct Notice {
    ui::LogController::Severity severity;
    QString title;
    QString text;
    QString details;
};

}

class PluginRuntimeTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QJsonObject m_registry;

    app::PluginManager::Folders folders() const
    {
        return {m_dir.filePath(QStringLiteral("user/plugins")), m_dir.filePath(QStringLiteral("user/plugin-data")),
                m_dir.filePath(QStringLiteral("cache/plugins")), m_dir.filePath(QStringLiteral("user/plugins.json"))};
    }

    void write(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }

    void install(const QString &id, const QString &name, const QByteArray &main, bool enabled = true,
                 const QByteArray &extra = {})
    {
        const QString folder = folders().plugins + QLatin1Char('/') + id;
        write(folder + QStringLiteral("/hikari-plugin.json"),
              R"({"id": ")" + id.toUtf8() + R"(", "name": ")" + name.toUtf8() +
                  R"(", "version": "1.0.0", "api": 1, "main": "main.lua", "authors": ["Ana"],)"
                  R"( "contributes": {"commands": [{"id": "greet", "title": "Say hello"}, {"id": "spin", "title": "Spin"}],)"
                  R"( "settings": [{"id": "count", "title": "Count", "type": "integer", "default": 0}]})" +
                  extra + "}");
        write(folder + QStringLiteral("/main.lua"), main);
        m_registry[id] = QJsonObject{{QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("enabled"), enabled}};
        write(folders().registry,
              QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("plugins"), m_registry}}).toJson());
    }

    static QList<Notice> notices(QSignalSpy &spy)
    {
        QList<Notice> out;
        for (const auto &args : spy)
            out << Notice{args.at(1).value<ui::LogController::Severity>(), args.at(2).toString(), args.at(3).toString(),
                          args.at(4).toString()};
        return out;
    }

private slots:
    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path());
        m_registry = {};
    }

    void enabledPluginsStartAndListTheirCommands()
    {
        install(QStringLiteral("com.example.hello"), QStringLiteral("Hello"),
                "hikari.commands.register('greet', function() hikari.notify('hi ' .. hikari.settings.get('count')) "
                "hikari.settings.set('count', hikari.settings.get('count') + 1) end)");
        install(QStringLiteral("com.example.off"), QStringLiteral("Off"), "", false);
        ui::LogController log;
        QSignalSpy noticed(&log, &ui::LogController::noticed);
        app::PluginRuntime runtime({QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), nullptr, &log, {}, {}});
        QSignalSpy changed(&runtime, &app::PluginRuntime::commandsChanged);
        runtime.sync();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(runtime.commands().size(), 2);
        const QVariantMap first = runtime.commands().first().toMap();
        QCOMPARE(first.value(QStringLiteral("id")).toString(), kGreet);
        QCOMPARE(first.value(QStringLiteral("titles")).toStringList(), (QStringList{QStringLiteral("Hello"), QStringLiteral("Say hello")}));
        QVERIFY(!runtime.process(QStringLiteral("com.example.off")));

        QVERIFY(runtime.run(kGreet));
        QTRY_COMPARE(noticed.size(), 1);
        QVERIFY(runtime.run(kGreet));
        QTRY_COMPARE(noticed.size(), 2);
        QCOMPARE(notices(noticed).at(0).title, QStringLiteral("Hello"));
        QCOMPARE(notices(noticed).at(0).text, QStringLiteral("hi 0"));
        QCOMPARE(notices(noticed).at(1).text, QStringLiteral("hi 1"));
        QVERIFY(!runtime.run(QStringLiteral("plugin.com.example.hello.missing")));
        QVERIFY(!runtime.run(QStringLiteral("plugin.com.example.off.greet")));

        install(QStringLiteral("com.example.off"), QStringLiteral("Off"), "", true);
        runtime.sync();
        QCOMPARE(runtime.commands().size(), 4);
        QVERIFY(runtime.process(QStringLiteral("com.example.off")));
        m_registry.remove(QStringLiteral("com.example.hello"));
        write(folders().registry,
              QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("plugins"), m_registry}}).toJson());
        runtime.sync();
        QCOMPARE(runtime.commands().size(), 2);
        QVERIFY(!runtime.process(QStringLiteral("com.example.hello")));
    }

    void aRunawayPluginIsStoppedAndACrashTakesDownOnlyItsHelper()
    {
        install(QStringLiteral("com.example.hello"), QStringLiteral("Hello"),
                "hikari.commands.register('greet', function() hikari.notify('still here') end)");
        install(QStringLiteral("com.example.spin"), QStringLiteral("Spinner"),
                "hikari.commands.register('spin', function() while true do end end)");
        install(QStringLiteral("com.example.crash"), QStringLiteral("Crasher"),
                "hikari.commands.register('greet', function() os.exit(3) end)", true,
                R"(, "tier": "unrestricted")");
        app::Localisation english;
        ui::LogController log;
        QSignalSpy noticed(&log, &ui::LogController::noticed);
        app::PluginRuntime runtime({QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), nullptr, &log, {}, {.timeLimitMs = 1000}});
        runtime.sync();
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.spin.spin")));
        QTRY_VERIFY_WITH_TIMEOUT(noticed.size() >= 1, 10'000);
        QCOMPARE(noticed.size(), 1);
        QCOMPARE(notices(noticed).at(0).severity, ui::LogController::Warning);
        QCOMPARE(notices(noticed).at(0).title, QStringLiteral("Spinner"));
        QCOMPARE(notices(noticed).at(0).text,
                 QStringLiteral("The plugin ran for more than 1 second, so HikariSub stopped it. Its next command starts it again."));

        const qint64 steady = runtime.process(QStringLiteral("com.example.hello"))->processId();
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.crash.greet")));
        QTRY_COMPARE_WITH_TIMEOUT(noticed.size(), 2, 10'000);
        QCOMPARE(notices(noticed).at(1).severity, ui::LogController::Error);
        QCOMPARE(notices(noticed).at(1).text, QStringLiteral("The plugin stopped unexpectedly. Its next command starts it again."));
        QVERIFY(runtime.run(kGreet));
        QTRY_COMPARE(noticed.size(), 3);
        QCOMPARE(notices(noticed).at(2).text, QStringLiteral("still here"));
        QCOMPARE(runtime.process(QStringLiteral("com.example.hello"))->processId(), steady);
    }

    void loadAndCommandErrorsAreNoticed()
    {
        install(QStringLiteral("com.example.hello"), QStringLiteral("Hello"),
                "hikari.commands.register('greet', function() error('no greeting today') end)");
        install(QStringLiteral("com.example.broken"), QStringLiteral("Broken"), "this is not lua");
        ui::LogController log;
        QSignalSpy noticed(&log, &ui::LogController::noticed);
        app::PluginRuntime runtime({QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), nullptr, &log, {}, {}});
        runtime.sync();
        QTRY_COMPARE(noticed.size(), 1);
        QCOMPARE(notices(noticed).at(0).title, QStringLiteral("Broken"));
        QCOMPARE(notices(noticed).at(0).text, QStringLiteral("The plugin didn’t start."));
        QVERIFY(notices(noticed).at(0).details.startsWith(QStringLiteral("main.lua:1:")));
        QVERIFY(!runtime.run(QStringLiteral("plugin.com.example.broken.greet")));
        QCOMPARE(noticed.size(), 2);
        QCOMPARE(notices(noticed).at(1).text, QStringLiteral("The plugin isn’t running."));

        QVERIFY(runtime.run(kGreet));
        QTRY_COMPARE(noticed.size(), 3);
        QCOMPARE(notices(noticed).at(2).text, QStringLiteral("“Say hello” failed."));
        QVERIFY(notices(noticed).at(2).details.contains(QStringLiteral("main.lua:1: no greeting today")));
    }

    void pluginCommandsTakeShortcutsUnderThePluginsFilter()
    {
        install(QStringLiteral("com.example.hello"), QStringLiteral("Hello"),
                "hikari.commands.register('greet', function() "
                "hikari.notify(#hikari.call('documents.list') .. ' open') end)");
        app::Application::Options o;
        o.settingsFile = m_dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.plugins = folders();
        app::Application a(o);
        QSignalSpy noticed(&a.log(), &ui::LogController::noticed);
        QTRY_COMPARE(a.pluginRuntime()->commands().size(), 2);
        auto &h = a.hotkeys();
        h.beginOptions();
        h.filter(7);
        for (const QVariant &v : h.rows())
            QVERIFY(!v.toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("plugin.")));
        h.filter(8);
        QStringList shown;
        int greet = -1;
        for (int i = 0; i < h.rows().size(); ++i) {
            const QVariantMap r = h.rows().at(i).toMap();
            shown << r.value(QStringLiteral("script")).toString() + " > " + r.value(QStringLiteral("macro")).toString();
            if (r.value(QStringLiteral("macro")).toString() == QStringLiteral("Say hello"))
                greet = i;
        }
        QCOMPARE(shown, (QStringList{QStringLiteral("Hello > Say hello"), QStringLiteral("Hello > Spin")}));
        h.select(greet);
        QVERIFY(!h.mapTarget().value(QStringLiteral("windows")).toBool());
        h.optionsMap(QStringLiteral("Ctrl-Alt-H"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        auto &shortcuts = a.automationHotkeys();
        QCOMPARE(shortcuts.keys().value(kGreet).toString(), QStringLiteral("Ctrl+Alt+H"));
        QVariantList installed = shortcuts.shortcuts();
        QCOMPARE(installed.size(), qsizetype(1));
        QCOMPARE(installed.first().toMap().value(QStringLiteral("legacyName")).toString(), kGreet);
        QVERIFY(shortcuts.missing().isEmpty());

        QVERIFY(shortcuts.run(kGreet));
        QTRY_COMPARE(noticed.size(), 1);
        QVERIFY(noticed.first().at(3).toString().endsWith(QStringLiteral(" open")));

        a.pluginManager()->setEnabled(QStringLiteral("com.example.hello"), false);
        QCOMPARE(a.pluginRuntime()->commands().size(), 0);
        QVERIFY(shortcuts.shortcuts().isEmpty());
        QVERIFY(!shortcuts.run(kGreet));
        h.beginOptions();
        h.filter(8);
        QCOMPARE(h.rows().size(), qsizetype(1));
        QVERIFY(h.rows().first().toMap().value(QStringLiteral("note")).toString().contains(QStringLiteral("isn’t installed")));
    }
};

QTEST_MAIN(PluginRuntimeTests)
#include "plugin_runtime_tests.moc"
