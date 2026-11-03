#include "hikari/app/application.h"
#include "hikari/app/hotkeys_controller.h"
#include "hikari/app/localisation.h"
#include "hikari/app/plugin_runtime.h"
#include "hikari/app/plugin_ui.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

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
                  R"( "panels": [{"id": "notes", "title": "Notes", "area": "bottom"}],)"
                  R"( "settings": [{"id": "count", "title": "Count", "type": "integer", "default": 0}]})" +
                  extra + "}");
        write(folder + QStringLiteral("/main.lua"), main);
        m_registry[id] = QJsonObject{{QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("enabled"), enabled}};
        write(folders().registry,
              QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("plugins"), m_registry}}).toJson());
    }

    std::unique_ptr<app::Application> openEpisodes(int count)
    {
        app::Application::Options o;
        o.settingsFile = m_dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        auto a = std::make_unique<app::Application>(o);
        for (int i = 1; i <= count; ++i) {
            const QString path = m_dir.filePath(QStringLiteral("episode-%1.ass").arg(i));
            write(path, "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\n"
                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n");
            if (!a->openFile(path))
                return nullptr;
        }
        return a;
    }
    static application::EditSession &session(app::Application &a)
    {
        return *a.files().session(*a.workspace().editingTarget());
    }
    static std::vector<std::uint64_t> lineIds(app::Application &a)
    {
        std::vector<std::uint64_t> out;
        for (const auto &line : session(a).document().lines())
            out.push_back(line->id.value);
        return out;
    }
    static api::Result apiCall(app::Application &a, std::string_view operation, const QString &params)
    {
        std::optional<api::Result> answer;
        a.apiHost()->api().call(api::Caller{"test", "Test", true}, operation, *api::Json::parse(params.toStdString()),
                                [&](api::Result r) { answer = std::move(r); });
        return answer ? *answer : api::fail("test_no_reply", std::string(operation));
    }
    static void select(app::Application &a, const std::vector<std::uint64_t> &ids)
    {
        QStringList items;
        for (const auto id : ids)
            items << QString::number(id);
        QVERIFY(apiCall(a, "selection.set", QStringLiteral(R"({"selected": [%1]})").arg(items.join(u','))));
    }
    app::PluginRuntime::Wiring wiring(app::Application &a, int transactionWaitMs = 5000)
    {
        app::PluginRuntime::Wiring w{QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), &a.apiHost()->api(), &a.log(), {}, {}};
        w.transactionWaitMs = transactionWaitMs;
        return w;
    }
    static QStringList texts(QSignalSpy &spy)
    {
        static const QStringList plugins{QStringLiteral("Tidy"),   QStringLiteral("Narrow"), QStringLiteral("Wide"),
                                         QStringLiteral("Holder"), QStringLiteral("Waiter"), QStringLiteral("Left"),
                                         QStringLiteral("Right")};
        QStringList out;
        for (const auto &args : spy)
            if (plugins.contains(args.at(2).toString()))
                out << args.at(2).toString() + QStringLiteral(": ") + args.at(3).toString() +
                           (args.at(4).toString().isEmpty() ? QString() : QStringLiteral(" (") + args.at(4).toString() + u')');
        return out;
    }

    static QList<Notice> notices(QSignalSpy &spy)
    {
        QList<Notice> out;
        for (const auto &args : spy)
            out << Notice{args.at(1).value<ui::LogController::Severity>(), args.at(2).toString(), args.at(3).toString(),
                          args.at(4).toString()};
        return out;
    }

    void installTurnTakers()
    {
        install(QStringLiteral("com.example.holder"), QStringLiteral("Holder"), R"~(
            hikari.commands.register('greet', function()
                hikari.document.edit('Hold', function(doc, change)
                    hikari.notify('holding')
                    repeat until #hikari.selection.get().selected == 2
                    change:set_line(doc.lines[1].id, {text = doc.lines[1].text .. ' A'})
                end)
            end)
        )~");
        install(QStringLiteral("com.example.waiter"), QStringLiteral("Waiter"), R"~(
            hikari.commands.register('greet', function()
                hikari.notify('waiting')
                local done, err = hikari.document.edit('Follow', function(doc, change)
                    change:set_line(doc.lines[1].id, {text = doc.lines[1].text .. ' B'})
                end)
                hikari.notify(done and 'followed' or err.code .. ': ' .. err.message)
            end)
        )~");
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

    void anEditIsOneNamedUndoStepAndAStaleOneIsRefused()
    {
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            hikari.commands.register('greet', function()
                local done, err = hikari.document.edit('Shout', function(doc, change)
                    change:set_line(doc.lines[1].id, {text = doc.lines[1].text:upper()})
                end)
                hikari.notify(done and 'shouted' or err.code)
            end)
            hikari.commands.register('spin', function()
                local done, err = hikari.document.edit('Late', function(doc, change)
                    hikari.call('document.apply', {revision = doc.revision, name = 'Meanwhile',
                        edits = {{op = 'set_info', key = 'Title', value = 'meanwhile'}}})
                    change:set_info('Title', 'late')
                end)
                hikari.notify(done and 'late' or err.code)
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        const std::size_t steps = session(*a).historySize();
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.tidy.greet")));
        QTRY_COMPARE(texts(noticed), QStringList{QStringLiteral("Tidy: shouted")});
        QCOMPARE(session(*a).historySize(), steps + 1);
        QCOMPARE(session(*a).history().back().name, std::string("Tidy: Shout"));
        QCOMPARE(session(*a).document().lines().front()->text, std::u8string(u8"FIRST"));
        QVERIFY(session(*a).undo());
        QCOMPARE(session(*a).document().lines().front()->text, std::u8string(u8"first"));

        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.tidy.spin")));
        QTRY_COMPARE(texts(noticed).size(), 2);
        QCOMPARE(texts(noticed).at(1), QStringLiteral("Tidy: stale_revision"));
        QCOMPARE(session(*a).history().back().name, std::string("Tidy: Meanwhile"));
    }

    void otherTabsNeedTheDocumentsAllPermission()
    {
        const QByteArray probe = R"~(
            hikari.commands.register('greet', function()
                local read, refused = 0, 0
                for id = 1, 200 do
                    local doc, err = hikari.document.read(id)
                    if doc then read = read + 1 elseif err.code == 'not_granted' then refused = refused + 1 end
                end
                hikari.notify(#hikari.document.list() .. ' listed, ' .. read .. ' read, ' .. tostring(refused > 0))
            end)
        )~";
        install(QStringLiteral("com.example.narrow"), QStringLiteral("Narrow"), probe);
        install(QStringLiteral("com.example.wide"), QStringLiteral("Wide"), probe, true,
                R"(, "permissions": {"documents.all": "It tidies every tab"})");
        auto a = openEpisodes(2);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.narrow.greet")));
        QTRY_COMPARE(texts(noticed).size(), 1);
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.wide.greet")));
        QTRY_COMPARE(texts(noticed).size(), 2);
        QCOMPARE(texts(noticed), (QStringList{QStringLiteral("Narrow: 1 listed, 1 read, true"),
                                              QStringLiteral("Wide: 2 listed, 2 read, false")}));
    }

    void twoPluginsApplyingAtOnceTakeTurns()
    {
        installTurnTakers();
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        const auto ids = lineIds(*a);
        select(*a, {ids[0]});
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.holder.greet")));
        QTRY_COMPARE(texts(noticed), QStringList{QStringLiteral("Holder: holding")});
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.waiter.greet")));
        QTRY_COMPARE(texts(noticed).size(), 2);
        QTest::qWait(300);
        QCOMPARE(session(*a).document().lines().front()->text, std::u8string(u8"first"));
        select(*a, ids);
        QTRY_COMPARE(texts(noticed).size(), 3);
        QCOMPARE(texts(noticed).at(2), QStringLiteral("Waiter: followed"));
        QCOMPARE(session(*a).document().lines().front()->text, std::u8string(u8"first A B"));
        const auto history = session(*a).history();
        QCOMPARE(history.at(history.size() - 2).name, std::string("Holder: Hold"));
        QCOMPARE(history.back().name, std::string("Waiter: Follow"));
    }

    void aWaitingEditGivesUpAsBusy()
    {
        installTurnTakers();
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a, 200));
        runtime.sync();
        const auto ids = lineIds(*a);
        select(*a, {ids[0]});
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.holder.greet")));
        QTRY_COMPARE(texts(noticed).size(), 1);
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.waiter.greet")));
        QTRY_COMPARE(texts(noticed).size(), 3);
        QCOMPARE(texts(noticed).at(2),
                 QStringLiteral("Waiter: busy: another plugin is applying edits; try again when it is done"));
        select(*a, ids);
        QTRY_COMPARE(session(*a).document().lines().front()->text, std::u8string(u8"first A"));

        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.waiter.greet")));
        QTRY_COMPARE(texts(noticed).size(), 5);
        QCOMPARE(texts(noticed).at(4), QStringLiteral("Waiter: followed"));
        QCOMPARE(session(*a).document().lines().front()->text, std::u8string(u8"first A B"));
    }

    void aStoppedHolderLetsTheNextPluginEdit()
    {
        installTurnTakers();
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        select(*a, {lineIds(*a)[0]});
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.holder.greet")));
        QTRY_COMPARE(texts(noticed).size(), 1);
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.waiter.greet")));
        QTRY_COMPARE(texts(noticed).size(), 2);
        runtime.forceStop(QStringLiteral("com.example.holder"));
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Waiter: followed")));
        QCOMPARE(session(*a).document().lines().front()->text, std::u8string(u8"first B"));
    }

    void aPanelDrawsWhenShownAndHearsItsWidgets()
    {
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            local name = ''
            local function draw(text)
                hikari.panels.render('notes', {kind = 'column', children = {
                    {kind = 'label', id = 'said', text = text},
                    {kind = 'text', id = 'name', value = name},
                    {kind = 'button', id = 'go', text = 'Go'}}})
            end
            hikari.panels.on('notes', function(e)
                if e.kind == 'show' then draw('hello') end
                if e.kind == 'click' and e.widget == 'go' then
                    name = e.values.name
                    draw('hello ' .. name)
                end
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        app::PluginUi *ui = runtime.ui();
        const QString dock = QStringLiteral("plugin:com.example.tidy/notes");
        QCOMPARE(ui->panels().size(), 1);
        QCOMPARE(ui->panels().first().toMap().value(QStringLiteral("area")).toString(), QStringLiteral("bottom"));
        app::PluginPanelState *panel = ui->panel(dock);
        QCOMPARE(panel->title(), QStringLiteral("Notes"));
        QCOMPARE(panel->pluginName(), QStringLiteral("Tidy"));
        QTRY_VERIFY(runtime.process(QStringLiteral("com.example.tidy"))->state() == PluginProcess::State::Ready);
        QVERIFY(panel->tree().isNull());

        const auto said = [&] {
            return panel->tree().toMap().value(QStringLiteral("children")).toList().value(0).toMap().value(QStringLiteral("text")).toString();
        };
        ui->setShown(dock, true);
        QTRY_COMPARE(said(), QStringLiteral("hello"));
        ui->send(dock, {{QStringLiteral("kind"), QStringLiteral("change")}, {QStringLiteral("widget"), QStringLiteral("name")},
                        {QStringLiteral("value"), QStringLiteral("Ana")}});
        ui->send(dock, {{QStringLiteral("kind"), QStringLiteral("click")}, {QStringLiteral("widget"), QStringLiteral("go")}});
        QTRY_COMPARE(said(), QStringLiteral("hello Ana"));
        QCOMPARE(ui->value(dock, QStringLiteral("name")).toString(), QStringLiteral("Ana"));

        QCOMPARE(ui->panel(QStringLiteral("plugin:com.example.gone/notes"))->status(), QStringLiteral("missing"));
        runtime.forceStop(QStringLiteral("com.example.tidy"));
        QTRY_COMPARE(panel->status(), QStringLiteral("stopped"));
        QVERIFY(panel->tree().isNull());
        QCOMPARE(ui->pluginState(QStringLiteral("com.example.tidy")).value(QStringLiteral("status")).toString(),
                 QStringLiteral("stopped"));
        runtime.restart(QStringLiteral("com.example.tidy"));
        QTRY_COMPARE(said(), QStringLiteral("hello"));
        QCOMPARE(panel->status(), QStringLiteral("running"));

        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), "", false);
        runtime.sync();
        QCOMPARE(panel->status(), QStringLiteral("off"));
        QVERIFY(ui->panels().isEmpty());
    }

    void theSamplePluginDrawsItsPanel()
    {
        const QString folder = folders().plugins + QStringLiteral("/com.example.line-tools");
        for (const char *file : {"hikari-plugin.json", "main.lua"}) {
            QFile source(QStringLiteral(HIKARI_PLUGIN_EXAMPLES "/line-tools/") + QLatin1String(file));
            QVERIFY(source.open(QIODevice::ReadOnly));
            write(folder + QLatin1Char('/') + QLatin1String(file), source.readAll());
        }
        m_registry[QStringLiteral("com.example.line-tools")] =
            QJsonObject{{QStringLiteral("version"), QStringLiteral("1.2.0")}, {QStringLiteral("enabled"), true}};
        write(folders().registry,
              QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("plugins"), m_registry}}).toJson());
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy logged(&a->log(), &ui::LogController::added);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        app::PluginUi *ui = runtime.ui();
        const QString dock = QStringLiteral("plugin:com.example.line-tools/timing-check");
        app::PluginPanelState *panel = ui->panel(dock);
        QCOMPARE(panel->title(), QStringLiteral("Timing check"));
        ui->setShown(dock, true);
        const auto kinds = [&] {
            QStringList out;
            for (const QVariant &child : panel->tree().toMap().value(QStringLiteral("children")).toList())
                out << child.toMap().value(QStringLiteral("kind")).toString();
            return out;
        };
        QTRY_COMPARE(kinds(), (QStringList{QStringLiteral("row"), QStringLiteral("choice"), QStringLiteral("canvas"),
                                           QStringLiteral("label")}));
        QCOMPARE(panel->menu().size(), 2);
        ui->send(dock, {{QStringLiteral("kind"), QStringLiteral("pointer")}, {QStringLiteral("widget"), QStringLiteral("chart")},
                        {QStringLiteral("type"), QStringLiteral("move")}, {QStringLiteral("x"), 20}, {QStringLiteral("y"), 50}});
        QTRY_VERIFY(panel->tree().toMap().value(QStringLiteral("children")).toList().value(2).toMap().contains(QStringLiteral("tip")));
        ui->send(dock, {{QStringLiteral("kind"), QStringLiteral("change")}, {QStringLiteral("widget"), QStringLiteral("view")},
                        {QStringLiteral("value"), QStringLiteral("list")}});
        QTRY_COMPARE(kinds().value(2), QStringLiteral("list"));
        for (const auto &entry : logged)
            QVERIFY2(entry.at(1).value<ui::LogController::Severity>() != ui::LogController::Error,
                     qPrintable(entry.at(3).toString()));
    }

    void aDialogAnswersWithItsButtonAndValues()
    {
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            hikari.commands.register('greet', function()
                local button, values = hikari.dialogs.ask('name', {title = 'Name',
                    body = {kind = 'text', id = 'name', value = ''},
                    buttons = {{id = 'ok', text = 'OK', default = true}, {id = 'cancel', text = 'Cancel', cancel = true}}})
                hikari.notify(tostring(button) .. ' ' .. tostring(values.name) .. ' ' .. hikari.settings.get('count'))
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        app::PluginUi *ui = runtime.ui();
        QSignalSpy opened(ui, &app::PluginUi::dialogOpened);
        QSignalSpy closed(ui, &app::PluginUi::dialogClosed);
        QTRY_VERIFY(runtime.process(QStringLiteral("com.example.tidy"))->state() == PluginProcess::State::Ready);
        runtime.settingChanged(QStringLiteral("com.example.tidy"), QStringLiteral("count"), QStringLiteral("3"));

        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.tidy.greet")));
        QTRY_VERIFY2(opened.count() == 1, qPrintable(texts(noticed).join(u'\n')));
        auto *dialog = opened.first().at(0).value<app::PluginDialogState *>();
        QCOMPARE(dialog->key(), QStringLiteral("plugin.com.example.tidy.name"));
        QCOMPARE(dialog->title(), QStringLiteral("Name"));
        QCOMPARE(dialog->buttons().size(), 2);
        ui->dialogEvent(dialog->key(), {{QStringLiteral("kind"), QStringLiteral("change")},
                                        {QStringLiteral("widget"), QStringLiteral("name")}, {QStringLiteral("value"), QStringLiteral("Bo")}});
        ui->press(dialog->key(), QStringLiteral("ok"));
        QCOMPARE(closed.count(), 1);
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: ok Bo 3")));

        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.tidy.greet")));
        QTRY_COMPARE(opened.count(), 2);
        ui->dismiss(opened.last().at(0).value<app::PluginDialogState *>()->key());
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: cancel  3")));
    }

    void pluginsHandleTheSameEventSideBySide()
    {
        for (const QString &name : {QStringLiteral("Left"), QStringLiteral("Right")})
            install(QStringLiteral("com.example.") + name.toLower(), name, R"~(
                hikari.events.on('selection.changed', function(data)
                    hikari.notify('selection ' .. #data.selected)
                end)
                hikari.events.on('document.changed', function() hikari.notify('document') end)
            )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        QTRY_VERIFY(runtime.process(QStringLiteral("com.example.left"))->state() == PluginProcess::State::Ready &&
                    runtime.process(QStringLiteral("com.example.right"))->state() == PluginProcess::State::Ready);
        select(*a, lineIds(*a));
        QVERIFY(apiCall(*a, "document.apply",
                        QStringLiteral(R"({"revision": %1, "name": "Retitle", "edits": [{"op": "set_info", "key": "Title", "value": "x"}]})")
                            .arg(session(*a).revision())));
        const QStringList wanted{QStringLiteral("Left: selection 2"), QStringLiteral("Right: selection 2"),
                                 QStringLiteral("Left: document"), QStringLiteral("Right: document")};
        QTRY_VERIFY(std::ranges::all_of(wanted, [&](const QString &w) { return texts(noticed).contains(w); }));
    }
};

QTEST_MAIN(PluginRuntimeTests)
#include "plugin_runtime_tests.moc"
