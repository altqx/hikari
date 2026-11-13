#include "hikari/app/application.h"
#include "hikari/app/hotkeys_controller.h"
#include "hikari/app/localisation.h"
#include "hikari/app/plugin_runtime.h"
#include "hikari/app/plugin_ui.h"
#include "hikari/app/project_windows.h"
#include "hikari/application/node_catalogue.h"
#include "hikari/application/row_marks.h"
#include "hikari/plugins/registry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <filesystem>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using backends::PluginProcess;

namespace {

const QString kGreet = QStringLiteral("plugin.com.example.hello.greet");
const QString kTidyGreet = QStringLiteral("plugin.com.example.tidy.greet");
const QString kTidySpin = QStringLiteral("plugin.com.example.tidy.spin");

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

    void installZigzag(bool enabled, const QString &pinnedKey = {})
    {
        const QString folder = folders().plugins + QStringLiteral("/com.example.zigzag");
        for (const char *file : {"hikari-plugin.json", "main.lua", "nodes/zigzag.lua"}) {
            QFile source(QStringLiteral(HIKARI_PLUGIN_EXAMPLES "/zigzag/") + QLatin1String(file));
            QVERIFY(source.open(QIODevice::ReadOnly));
            write(folder + QLatin1Char('/') + QLatin1String(file), source.readAll());
        }
        QJsonObject entry{{QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("enabled"), enabled}};
        if (!pinnedKey.isEmpty())
            entry[QStringLiteral("key")] = pinnedKey;
        m_registry[QStringLiteral("com.example.zigzag")] = entry;
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
                                         QStringLiteral("Right"),  QStringLiteral("Chains")};
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
        std::error_code error;
        std::filesystem::remove_all(m_dir.path().toStdU16String(), error);
        QVERIFY2(!error, error.message().c_str());
        QDir().mkpath(m_dir.path());
        m_registry = {};
        application::setPluginNodes({}, {});
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

    void restrictedPluginsStartSandboxedAndFullTrustOnesDoNot()
    {
        install(QStringLiteral("com.example.hello"), QStringLiteral("Hello"),
                "hikari.commands.register('greet', function() hikari.notify('confined') end)");
        install(QStringLiteral("com.example.native"), QStringLiteral("Native"),
                "hikari.commands.register('greet', function() hikari.notify(io and 'full' or 'none') end)", true,
                R"(, "tier": "unrestricted")");
        ui::LogController log;
        QSignalSpy noticed(&log, &ui::LogController::noticed);
        app::PluginRuntime::Wiring w{QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), nullptr, &log, {}, {}};
        w.sandbox = true;
        app::PluginRuntime runtime(w);
        runtime.sync();
        QVERIFY(runtime.process(QStringLiteral("com.example.hello"))->sandboxed());
        QVERIFY(!runtime.process(QStringLiteral("com.example.native"))->sandboxed());
        QCOMPARE(runtime.sandboxed(), backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete());
        QVERIFY(runtime.run(kGreet));
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.native.greet")));
        QTRY_COMPARE_WITH_TIMEOUT(noticed.size(), 2, 10'000);
        QStringList said;
        for (const Notice &notice : notices(noticed))
            said << notice.title + QStringLiteral(": ") + notice.text;
        said.sort();
        QCOMPARE(said, (QStringList{QStringLiteral("Hello: confined"), QStringLiteral("Native: full")}));

        app::PluginRuntime unconfined({QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), nullptr, &log, {}, {}});
        QVERIFY(!unconfined.sandboxed());
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

    void jobsStackNewestFirstAndCancelOffersStopAfterTheGrace()
    {
        const QByteArray scan = "hikari.commands.register('spin', function() "
                                "hikari.job.begin{title = 'Scanning'} hikari.job.progress(0.25, 'Frame 64') "
                                "while true do end end)";
        install(QStringLiteral("example.chains"), QStringLiteral("Chains"), scan);
        install(QStringLiteral("com.example.other"), QStringLiteral("Other"), scan);
        ui::LogController log;
        application::RowMarks marks;
        QStringList used;
        app::PluginRuntime::Wiring w{QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), nullptr, &log, {}, {.timeLimitMs = 500}};
        w.jobGraceMs = 200;
        w.marks = &marks;
        w.featureUsed = [&](const QString &feature) { used << feature; };
        app::PluginRuntime runtime(std::move(w));
        runtime.sync();
        QVERIFY(runtime.run(QStringLiteral("plugin.example.chains.spin")));
        QTRY_COMPARE_WITH_TIMEOUT(runtime.jobs().size(), 1, 10'000);
        QVERIFY(runtime.run(QStringLiteral("plugin.com.example.other.spin")));
        QTRY_COMPARE_WITH_TIMEOUT(runtime.jobs().size(), 2, 10'000);
        QTRY_COMPARE(runtime.jobs().at(1).toMap().value(QStringLiteral("fraction")).toDouble(), 0.25);
        QCOMPARE(runtime.jobs().at(0).toMap().value(QStringLiteral("plugin")).toString(), QStringLiteral("com.example.other"));
        const QVariantMap chains = runtime.jobs().at(1).toMap();
        QCOMPARE(chains.value(QStringLiteral("name")).toString(), QStringLiteral("Chains"));
        QCOMPARE(chains.value(QStringLiteral("title")).toString(), QStringLiteral("Scanning"));
        QCOMPARE(chains.value(QStringLiteral("text")).toString(), QStringLiteral("Frame 64"));
        QCOMPARE(used, (QStringList{QStringLiteral("job"), QStringLiteral("job")}));

        QTest::qWait(800);
        QCOMPARE(runtime.jobs().size(), 2);
        marks.set("plugin:example.chains", "Chains", application::DocumentId{1},
                  {{core::LineId{4}, application::MarkColour::Warning, "Changed"}});
        QCOMPARE(runtime.marks(QStringLiteral("example.chains")).size(), 1);
        QCOMPARE(runtime.marks(QStringLiteral("example.chains")).at(0).toMap().value(QStringLiteral("colour")).toString(),
                 QStringLiteral("warning"));

        runtime.cancelJob(QStringLiteral("example.chains"));
        QVERIFY(runtime.jobs().at(1).toMap().value(QStringLiteral("cancelling")).toBool());
        QVERIFY(!runtime.jobs().at(1).toMap().value(QStringLiteral("stuck")).toBool());
        QTRY_VERIFY(runtime.jobs().at(1).toMap().value(QStringLiteral("stuck")).toBool());
        runtime.forceStop(QStringLiteral("example.chains"));
        QTRY_COMPARE(runtime.jobs().size(), 1);
        QCOMPARE(runtime.jobs().at(0).toMap().value(QStringLiteral("plugin")).toString(), QStringLiteral("com.example.other"));
        QVERIFY(marks.counts("plugin:example.chains").empty());
        runtime.forceStop(QStringLiteral("com.example.other"));
        QTRY_VERIFY(runtime.jobs().isEmpty());
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

    void aGroupedChainUndoesAsOneStepEvenWhenTheCommandLeavesItOpen()
    {
        install(QStringLiteral("example.chains"), QStringLiteral("Chains"), R"~(
            local function shout(n)
                hikari.document.edit('Shout ' .. n, function(doc, change)
                    change:set_line(doc.lines[n].id, {text = doc.lines[n].text:upper()})
                end)
            end
            hikari.commands.register('greet', function()
                local ended, value = hikari.document.group('Chain', function()
                    shout(1)
                    shout(2)
                    return 'kept'
                end)
                hikari.notify(ended.steps .. ' ' .. value)
            end)
            hikari.commands.register('spin', function()
                hikari.call('document.group_begin', {name = 'Left open'})
                shout(1)
                shout(2)
                hikari.notify('left')
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        const std::size_t steps = session(*a).historySize();
        QVERIFY(runtime.run(QStringLiteral("plugin.example.chains.greet")));
        QTRY_COMPARE(texts(noticed), QStringList{QStringLiteral("Chains: 2 kept")});
        QCOMPARE(session(*a).historySize(), steps + 1);
        QCOMPARE(session(*a).history().back().name, std::string("Chains: Chain"));
        QVERIFY(session(*a).undo());
        QCOMPARE(session(*a).document().lines().at(0)->text, std::u8string(u8"first"));
        QCOMPARE(session(*a).document().lines().at(1)->text, std::u8string(u8"second"));

        QVERIFY(runtime.run(QStringLiteral("plugin.example.chains.spin")));
        QTRY_COMPARE(texts(noticed).size(), 2);
        QTRY_COMPARE(session(*a).history().back().name, std::string("Chains: Left open"));
        QCOMPARE(session(*a).historySize(), steps + 1);
        QVERIFY(session(*a).undo());
        QCOMPARE(session(*a).document().lines().at(1)->text, std::u8string(u8"second"));
    }

    void aGroupEndsWhenItsFunctionFailsAndTheErrorPassesThrough()
    {
        install(QStringLiteral("example.chains"), QStringLiteral("Chains"), R"~(
            hikari.commands.register('greet', function()
                local ok, err = pcall(hikari.document.group, 'Broken', function()
                    hikari.document.edit('One', function(doc, change)
                        change:set_line(doc.lines[1].id, {text = 'one'})
                    end)
                    error('chain broke')
                end)
                local again = hikari.call('document.group_begin', {name = 'Again'})
                hikari.notify(tostring(not ok and err:find('chain broke') ~= nil) .. ' ' .. tostring(again ~= nil))
                hikari.call('document.group_end', {})
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        QVERIFY(runtime.run(QStringLiteral("plugin.example.chains.greet")));
        QTRY_COMPARE(texts(noticed), QStringList{QStringLiteral("Chains: true true")});
        QCOMPARE(session(*a).history().back().name, std::string("Chains: Broken"));
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

    void theSamplePluginsNodeJoinsTheCatalogueWithItsOwnCode()
    {
        installZigzag(true);
        auto a = openEpisodes(1);
        QVERIFY(a);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        const std::string type = application::pluginNodeType("com.example.zigzag", "zigzag");
        const application::PluginNode *node = application::pluginNode(type);
        QVERIFY(node);
        QCOMPARE(node->pluginName, std::string("Zigzag"));
        QCOMPARE(node->spec.category, std::string("com.example.zigzag"));
        QCOMPARE(node->spec.inputs.size(), 2u);
        QCOMPARE(node->spec.outputs.at(0).type, application::PortType::Number);
        QCOMPARE(node->spec.fields.size(), 1u);
        QCOMPARE(node->spec.fields.at(0).fallback.asNumber(), 10.0);
        QFile code(QStringLiteral(HIKARI_PLUGIN_EXAMPLES "/zigzag/nodes/zigzag.lua"));
        QVERIFY(code.open(QIODevice::ReadOnly));
        QCOMPARE(QByteArray::fromStdString(node->code), code.readAll());
        QVERIFY(application::nodeSpec(type));
        QVERIFY(!application::pluginNodeProblem(type));
    }

    void aTurnedOffPluginsNodeIsKeptWithAProblemNamingThePlugin()
    {
        installZigzag(false);
        auto a = openEpisodes(1);
        QVERIFY(a);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        const std::string type = application::pluginNodeType("com.example.zigzag", "zigzag");
        QVERIFY(!application::pluginNode(type));
        QCOMPARE(application::pluginNodeProblem(type).value_or(""),
                 std::string(R"(comes from the plugin "Zigzag", which is turned off or not trusted)"));

        installZigzag(true);
        runtime.sync();
        QVERIFY(application::pluginNode(type));
        installZigzag(false);
        runtime.sync();
        QVERIFY(!application::pluginNode(type));
    }

    void aPluginWhoseKeyNoLongerMatchesItsPinIsNotTrusted()
    {
        installZigzag(true, QStringLiteral("RWQBI0VniavN7wOhB7/zzhC+HXDdGOdLwJln5NYwm6UNXx3chmQSVTG4"));
        auto a = openEpisodes(1);
        QVERIFY(a);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        const std::string type = application::pluginNodeType("com.example.zigzag", "zigzag");
        QVERIFY(!application::pluginNode(type));
        QCOMPARE(application::pluginNodeProblem(type).value_or(""),
                 std::string(R"(comes from the plugin "Zigzag", which is turned off or not trusted)"));
    }

    void theFirstPluginGuideRunsAtEveryStep()
    {
        const QString id = QStringLiteral("com.example.first-plugin");
        const QString path = m_dir.filePath(QStringLiteral("guide.ass"));
        write(path, "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,one\\Ntwo\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,three\n");
        app::Application::Options o;
        o.settingsFile = m_dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        app::Application a(o);
        QVERIFY(a.openFile(path));
        QSignalSpy noticed(&a.log(), &ui::LogController::noticed);
        const auto said = [&] {
            QList<Notice> out;
            for (const Notice &notice : notices(noticed))
                if (notice.title == QLatin1String("First plugin"))
                    out << notice;
            return out;
        };
        const auto step = [&](int number) {
            const QString from = QStringLiteral(HIKARI_PLUGIN_EXAMPLES "/first-plugin/step-%1/").arg(number);
            for (const char *file : {"hikari-plugin.json", "main.lua"}) {
                QFile source(from + QLatin1String(file));
                if (!source.open(QIODevice::ReadOnly))
                    return false;
                write(folders().plugins + QLatin1Char('/') + id + QLatin1Char('/') + QLatin1String(file), source.readAll());
            }
            m_registry[id] = QJsonObject{{QStringLiteral("version"), QStringLiteral("0.%1.0").arg(number)},
                                         {QStringLiteral("enabled"), true}};
            write(folders().registry,
                  QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("plugins"), m_registry}}).toJson());
            return true;
        };
        const QString count = QStringLiteral("plugin.com.example.first-plugin.count");
        const QString dock = QStringLiteral("plugin:com.example.first-plugin/summary");
        const auto heading = [](app::PluginPanelState *panel) {
            return panel->tree().toMap().value(QStringLiteral("children")).toList().value(0).toMap().value(QStringLiteral("text"));
        };

        QVERIFY(step(1));
        {
            app::PluginRuntime runtime(wiring(a));
            runtime.sync();
            QVERIFY(runtime.run(count));
            QTRY_COMPARE(said().size(), 1);
            QCOMPARE(said().at(0).text, QStringLiteral("guide.ass has 2 lines."));
            QVERIFY(runtime.ui()->panels().isEmpty());
        }

        QVERIFY(step(2));
        {
            app::PluginRuntime runtime(wiring(a));
            runtime.sync();
            app::PluginPanelState *panel = runtime.ui()->panel(dock);
            QVERIFY(panel);
            QCOMPARE(panel->title(), QStringLiteral("Summary"));
            runtime.ui()->setShown(dock, true);
            QTRY_COMPARE(heading(panel), QVariant(QStringLiteral("2 lines")));
            QVERIFY(apiCall(a, "document.apply", QStringLiteral(R"({"revision": %1, "name": "More",
                "edits": [{"op": "insert_line", "fields": {"text": "four"}}]})")
                                                     .arg(session(a).revision())));
            QTRY_COMPARE(heading(panel), QVariant(QStringLiteral("3 lines")));
            QVERIFY(apiCall(a, "document.apply", QStringLiteral(R"({"revision": %1, "name": "Less",
                "edits": [{"op": "delete_line", "line": %2}]})")
                                                     .arg(session(a).revision())
                                                     .arg(lineIds(a).back())));
            QTRY_COMPARE(heading(panel), QVariant(QStringLiteral("2 lines")));
        }

        QVERIFY(step(3));
        {
            app::PluginRuntime runtime(wiring(a));
            runtime.sync();
            app::PluginPanelState *panel = runtime.ui()->panel(dock);
            runtime.ui()->setShown(dock, true);
            QTRY_COMPARE(panel->tree().toMap().value(QStringLiteral("children")).toList().size(), 2);
            select(a, lineIds(a));
            const std::size_t steps = session(a).historySize();
            QVERIFY(runtime.run(QStringLiteral("plugin.com.example.first-plugin.unbreak")));
            QTRY_COMPARE(session(a).historySize(), steps + 1);
            QCOMPARE(session(a).history().back().name, std::string("First plugin: Join line breaks"));
            QCOMPARE(session(a).document().lines().front()->text, std::u8string(u8"one two"));
            QCOMPARE(session(a).document().lines().back()->text, std::u8string(u8"three"));
            QVERIFY(runtime.run(QStringLiteral("plugin.com.example.first-plugin.unbreak")));
            QTRY_COMPARE(said().size(), 2);
            QCOMPARE(said().last().text, QStringLiteral("The selected lines have no line breaks."));
            QCOMPARE(session(a).historySize(), steps + 1);

            QVERIFY(session(a).undo());
            select(a, lineIds(a));
            runtime.ui()->send(dock, {{QStringLiteral("kind"), QStringLiteral("click")},
                                      {QStringLiteral("widget"), QStringLiteral("unbreak")}});
            QTRY_COMPARE(session(a).document().lines().front()->text, std::u8string(u8"one two"));
        }
        for (const Notice &notice : said())
            QVERIFY2(notice.severity != ui::LogController::Error, qPrintable(notice.text));
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

    void aPluginOpensOnlyPathsInsideItsOwnFolders()
    {
        write(m_dir.filePath(QStringLiteral("secret/key.txt")), "secret");
        const QString data = folders().data + QStringLiteral("/com.example.tidy");
        QDir().mkpath(data);
        std::error_code linkError;
        std::filesystem::create_directory_symlink(m_dir.filePath(QStringLiteral("secret")).toStdU16String(),
                                                  (data + QStringLiteral("/door")).toStdU16String(), linkError);
        const bool linked = !linkError;
        QStringList refused{QStringLiteral("../com.example.other/x"), QStringLiteral("../../plugins.json"), QStringLiteral("/etc/passwd"),
                            QStringLiteral("notes/../../x"), QStringLiteral("C:/Windows/win.ini")};
        if (linked)
            refused << QStringLiteral("door/key.txt");
        QByteArray main = R"~(
            hikari.commands.register('greet', function()
                local note = hikari.files.open('data', 'notes/today.txt')
                hikari.files.write(note, 'hello')
                hikari.notify('data ' .. note.name .. ' ' .. note.access .. ' ' .. hikari.files.read(note))
                hikari.notify('cache ' .. hikari.files.open('cache', 'thumbs/a.bin').access)
                hikari.notify('entries ' .. hikari.files.entries('data', 'notes')[1].name)
                for _, path in ipairs({PATHS}) do
                    local _, err = hikari.files.open('data', path)
                    hikari.notify('refused ' .. path .. ' ' .. err.code)
                end
                local _, empty = hikari.files.open('data', '')
                hikari.notify('empty ' .. empty.code)
                local _, home = hikari.files.open('home', '.bashrc')
                hikari.notify('home ' .. home.code)
                local _, forged = hikari.files.read('f999')
                hikari.notify('forged ' .. forged.code)
            end)
        )~";
        main.replace("PATHS", (u'\'' + refused.join(QStringLiteral("', '")) + u'\'').toUtf8());
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), main);
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        QVERIFY(runtime.run(kTidyGreet));
        QStringList wanted{QStringLiteral("Tidy: data notes/today.txt write hello"), QStringLiteral("Tidy: cache write"),
                           QStringLiteral("Tidy: entries today.txt")};
        for (const QString &path : refused)
            wanted << QStringLiteral("Tidy: refused %1 not_granted").arg(path);
        wanted << QStringLiteral("Tidy: empty invalid_params") << QStringLiteral("Tidy: home not_granted")
               << QStringLiteral("Tidy: forged not_granted");
        QTRY_COMPARE(texts(noticed).size(), wanted.size());
        QCOMPARE(texts(noticed), wanted);
        QFile note(data + QStringLiteral("/notes/today.txt"));
        QVERIFY(note.open(QIODevice::ReadOnly));
        QCOMPARE(note.readAll(), QByteArray("hello"));
        QVERIFY(QFileInfo(folders().cache + QStringLiteral("/com.example.tidy/thumbs")).isDir());
        QFile secret(m_dir.filePath(QStringLiteral("secret/key.txt")));
        QVERIFY(secret.open(QIODevice::ReadOnly));
        QCOMPARE(secret.readAll(), QByteArray("secret"));
    }

    void aPluginReachesATrustedProjectsPlaceAndHearsTheProjectChange()
    {
        write(m_dir.filePath(QStringLiteral("show/notes.txt")), "notes");
        write(m_dir.filePath(QStringLiteral("secret/key.txt")), "secret");
        std::error_code linkError;
        std::filesystem::create_symlink(m_dir.filePath(QStringLiteral("secret/key.txt")).toStdU16String(),
                                        m_dir.filePath(QStringLiteral("show/key.txt")).toStdU16String(), linkError);
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            hikari.events.on('project.changed', function(data)
                hikari.notify('changed ' .. (data.project == hikari.null and 'none' or data.project.name))
            end)
            hikari.commands.register('greet', function()
                local project = hikari.project.info()
                if project == hikari.null then
                    local _, err = hikari.files.open('project', 'notes.txt')
                    hikari.notify('no project ' .. err.code)
                    return
                end
                hikari.notify('project ' .. project.name .. ' ' .. tostring(project.trusted))
                if not project.trusted then
                    local _, err = hikari.files.open('project', 'notes.txt')
                    hikari.notify('untrusted ' .. err.code)
                    return
                end
                local notes = hikari.files.open('project', 'notes.txt')
                hikari.notify('read ' .. notes.access .. ' ' .. hikari.files.read(notes))
                hikari.files.write(hikari.files.open('project', 'out/made.txt'), 'made')
                local names = {}
                for _, entry in ipairs(hikari.files.entries('project', '')) do
                    names[#names + 1] = entry.name
                end
                hikari.notify('entries ' .. table.concat(names, ' '))
                local _, err = hikari.files.open('project', 'key.txt')
                hikari.notify('link ' .. (err and err.code or 'granted'))
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime::Wiring w = wiring(*a);
        w.project = [&a] { return a->activeProject(); };
        app::PluginRuntime runtime(std::move(w));
        runtime.sync();
        QTRY_VERIFY(runtime.process(QStringLiteral("com.example.tidy"))->state() == PluginProcess::State::Ready);
        QVERIFY(runtime.run(kTidyGreet));
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: no project not_found")));
        const QString show = app::ProjectWindows::canonicalFolder(m_dir.filePath(QStringLiteral("show")));
        a->projects().set(a->lastUsedWindow(), show);
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: changed show")));
        QVERIFY(runtime.run(kTidyGreet));
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: untrusted not_found")));
        QVERIFY(texts(noticed).contains(QStringLiteral("Tidy: project show false")));
        a->settingsStore()->set("trust.folders", QStringList{show});
        QVERIFY(runtime.run(kTidyGreet));
        QStringList wanted{QStringLiteral("Tidy: project show true"), QStringLiteral("Tidy: read write notes"),
                           QStringLiteral("Tidy: entries notes.txt out")};
        if (!linkError)
            wanted << QStringLiteral("Tidy: link not_granted");
        QTRY_VERIFY(texts(noticed).contains(wanted.last()));
        for (const QString &line : wanted)
            QVERIFY2(texts(noticed).contains(line), qPrintable(texts(noticed).join(u'|')));
        QFile made(m_dir.filePath(QStringLiteral("show/out/made.txt")));
        QVERIFY(made.open(QIODevice::ReadOnly));
        QCOMPARE(made.readAll(), QByteArray("made"));
        a->projects().set(a->lastUsedWindow(), QString());
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: changed none")));
    }

    void aJunctionOutOfItsFolderIsRefused()
    {
#ifdef Q_OS_WIN
        write(m_dir.filePath(QStringLiteral("secret/key.txt")), "secret");
        const QString data = folders().data + QStringLiteral("/com.example.tidy");
        QDir().mkpath(data);
        QProcess mklink;
        mklink.setProcessChannelMode(QProcess::MergedChannels);
        mklink.start(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                                             QDir::toNativeSeparators(data + QStringLiteral("/door")),
                                             QDir::toNativeSeparators(m_dir.filePath(QStringLiteral("secret")))});
        QVERIFY(mklink.waitForFinished());
        QVERIFY2(mklink.exitCode() == 0, mklink.readAll().constData());
        QVERIFY(QFile::exists(data + QStringLiteral("/door/key.txt")));
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            hikari.commands.register('greet', function()
                for _, path in ipairs({'door/key.txt', 'door/new.txt'}) do
                    local _, err = hikari.files.open('data', path)
                    hikari.notify('open ' .. path .. ' ' .. (err and err.code or 'granted'))
                end
                local _, listed = hikari.files.entries('data', 'door')
                hikari.notify('entries door ' .. (listed and listed.code or 'granted'))
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        QVERIFY(runtime.run(kTidyGreet));
        const QStringList wanted{QStringLiteral("Tidy: open door/key.txt not_granted"), QStringLiteral("Tidy: open door/new.txt not_granted"),
                                 QStringLiteral("Tidy: entries door not_granted")};
        QTRY_COMPARE(texts(noticed).size(), wanted.size());
        QCOMPARE(texts(noticed), wanted);
        QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("secret/new.txt"))));
        QFile secret(m_dir.filePath(QStringLiteral("secret/key.txt")));
        QVERIFY(secret.open(QIODevice::ReadOnly));
        QCOMPARE(secret.readAll(), QByteArray("secret"));
#else
        QSKIP("Directory junctions exist only on Windows");
#endif
    }

    void aFolderIsAskedForOnceAndRememberedWhenTheUserSaysSo()
    {
        const QString exports = m_dir.filePath(QStringLiteral("exports"));
        const QString fonts = m_dir.filePath(QStringLiteral("fonts"));
        write(fonts + QStringLiteral("/a.ttf"), "font");
        QDir().mkpath(exports);
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            hikari.commands.register('greet', function()
                hikari.notify('exports ' .. tostring(hikari.files.folder('exports')))
                local report = hikari.files.open('exports', 'week/report.txt')
                hikari.notify('wrote ' .. tostring(hikari.files.write(report, 'report')))
            end)
            hikari.commands.register('spin', function()
                local font, err = hikari.files.open('fonts', 'a.ttf')
                if not font then hikari.notify('fonts ' .. err.code) return end
                local _, refused = hikari.files.write(font, 'x')
                hikari.notify('fonts ' .. hikari.files.read(font) .. ' ' .. refused.code)
            end)
        )~", true, R"(, "permissions": {"files": [
                {"id": "exports", "title": "Exports", "writable": true, "reason": "Saves the weekly report"},
                {"id": "fonts", "title": "Fonts", "reason": "Reads your fonts"}]})");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        {
            app::PluginRuntime runtime(wiring(*a));
            runtime.sync();
            app::PluginUi *ui = runtime.ui();
            QVERIFY(runtime.run(kTidyGreet));
            QTRY_VERIFY(ui->fileRequest());
            app::PluginFileRequest *asked = ui->fileRequest();
            QCOMPARE(asked->kind(), QStringLiteral("folder"));
            QCOMPARE(asked->pluginName(), QStringLiteral("Tidy"));
            QCOMPARE(asked->title(), QStringLiteral("Exports"));
            QCOMPARE(asked->reason(), QStringLiteral("Saves the weekly report"));
            QVERIFY(asked->writable());
            asked->accept(QUrl::fromLocalFile(exports), true);
            QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: wrote true")));
            QVERIFY(texts(noticed).contains(QStringLiteral("Tidy: exports true")));
            QVERIFY(!ui->fileRequest());

            QVERIFY(runtime.run(kTidySpin));
            QTRY_VERIFY(ui->fileRequest());
            QVERIFY(!ui->fileRequest()->writable());
            ui->fileRequest()->reject();
            QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: fonts not_granted")));
            QVERIFY(runtime.run(kTidySpin));
            QTRY_VERIFY(ui->fileRequest());
            ui->fileRequest()->accept(QUrl::fromLocalFile(fonts), false);
            QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: fonts font not_granted")));
        }
        {
            QFile report(exports + QStringLiteral("/week/report.txt"));
            QVERIFY(report.open(QIODevice::ReadOnly));
            QCOMPARE(report.readAll(), QByteArray("report"));
        }
        const auto registry = plugins::loadRegistry(std::filesystem::path(folders().registry.toStdU16String()));
        QVERIFY(registry);
        const auto &grants = registry->at("com.example.tidy").grants;
        QCOMPARE(grants.size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(grants.at("exports").path), QDir(exports).absolutePath());
        QVERIFY(grants.at("exports").writable);

        noticed.clear();
        app::PluginRuntime again(wiring(*a));
        again.sync();
        QVERIFY(again.run(kTidyGreet));
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: wrote true")));
        QVERIFY(!again.ui()->fileRequest());
        QVERIFY(again.run(kTidySpin));
        QTRY_VERIFY(again.ui()->fileRequest());
        again.ui()->fileRequest()->reject();
    }

    void aPickedOrDroppedFileIsTheOnlyFileGranted()
    {
        const QString picked = m_dir.filePath(QStringLiteral("picked.txt"));
        const QString dropped = m_dir.filePath(QStringLiteral("dropped.txt"));
        write(picked, "picked");
        write(dropped, "dropped");
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            hikari.panels.render('notes', {kind = 'column', drop = true, children = {{kind = 'label', text = 'Drop notes here'}}})
            hikari.panels.on('notes', function(e)
                if e.kind == 'drop' then
                    hikari.notify('dropped ' .. e.files[1].name .. ' ' .. e.files[1].access .. ' ' .. hikari.files.read(e.files[1]))
                end
            end)
            hikari.commands.register('greet', function()
                local file = hikari.files.pick({title = 'Pick a note', filters = {{name = 'Notes', patterns = {'*.txt', '*.md'}}}})
                local _, refused = hikari.files.write(file, 'x')
                hikari.notify('picked ' .. file.name .. ' ' .. hikari.files.read(file) .. ' ' .. refused.code)
                local saved = hikari.files.pick({save = true, name = 'out.txt'})
                hikari.notify('saved ' .. tostring(hikari.files.write(saved, 'saved')))
                hikari.notify('cancelled ' .. tostring(hikari.files.pick()))
                local _, bad = hikari.files.pick({filters = {{name = 'Bad', patterns = {'*.(x)'}}}})
                hikari.notify('bad ' .. bad.code)
                hikari.notify('granted ' .. #hikari.files.list())
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        app::PluginUi *ui = runtime.ui();
        QVERIFY(runtime.run(kTidyGreet));
        QTRY_VERIFY(ui->fileRequest());
        QCOMPARE(ui->fileRequest()->kind(), QStringLiteral("open"));
        QCOMPARE(ui->fileRequest()->title(), QStringLiteral("Pick a note"));
        QCOMPARE(ui->fileRequest()->nameFilters(), QStringList{QStringLiteral("Notes (*.txt *.md)")});
        ui->fileRequest()->accept(QUrl::fromLocalFile(picked));
        QTRY_VERIFY(ui->fileRequest() && ui->fileRequest()->kind() == QStringLiteral("save"));
        QCOMPARE(ui->fileRequest()->file().fileName(), QStringLiteral("out.txt"));
        ui->fileRequest()->accept(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("out.txt"))));
        QTRY_VERIFY(ui->fileRequest() && ui->fileRequest()->kind() == QStringLiteral("open"));
        ui->fileRequest()->reject();
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: granted 2")));
        QVERIFY(texts(noticed).contains(QStringLiteral("Tidy: picked picked.txt picked not_granted")));
        QVERIFY(texts(noticed).contains(QStringLiteral("Tidy: saved true")));
        QVERIFY(texts(noticed).contains(QStringLiteral("Tidy: cancelled nil")));
        QVERIFY(texts(noticed).contains(QStringLiteral("Tidy: bad invalid_params")));
        QFile out(m_dir.filePath(QStringLiteral("out.txt")));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), QByteArray("saved"));

        const QString dock = QStringLiteral("plugin:com.example.tidy/notes");
        QTRY_VERIFY(!ui->panel(dock)->tree().isNull());
        QVERIFY(!ui->drop(dock, {QUrl(QStringLiteral("https://example.com/x.txt"))}));
        QVERIFY(ui->drop(dock, {QUrl::fromLocalFile(dropped)}));
        QTRY_VERIFY(texts(noticed).contains(QStringLiteral("Tidy: dropped dropped.txt read dropped")));
        QVERIFY(!ui->drop(QStringLiteral("plugin:com.example.other/notes"), {QUrl::fromLocalFile(dropped)}));
    }

    void aStoppedPluginLosesItsPickerAndItsHandles()
    {
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            hikari.commands.register('greet', function() hikari.files.pick() end)
            hikari.commands.register('spin', function()
                local note = hikari.files.open('data', 'a.txt')
                hikari.files.write(note, 'a')
                hikari.notify('handle ' .. note.file)
            end)
        )~");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        app::PluginRuntime runtime(wiring(*a));
        runtime.sync();
        QVERIFY(runtime.run(kTidySpin));
        QTRY_COMPARE(texts(noticed).size(), 1);
        const std::string handle = texts(noticed).first().section(u' ', -1).toStdString();
        auto &api = a->apiHost()->api();
        const auto read = [&] {
            std::optional<api::Result> answer;
            api.call(api::Caller{"plugin:com.example.tidy", "Tidy"}, "files.read", api::Json::object({{"file", handle}}),
                     [&](api::Result r) { answer = std::move(r); });
            return answer;
        };
        QVERIFY(read() && *read());
        QVERIFY(runtime.run(kTidyGreet));
        QTRY_VERIFY(runtime.ui()->fileRequest());
        runtime.forceStop(QStringLiteral("com.example.tidy"));
        QTRY_VERIFY(!runtime.ui()->fileRequest());
        QCOMPARE(read()->error().code, std::string(api::errors::kNotGranted));
    }

    void pluginRequestsNeedTheSwitchHttpsAndADeclaredHost()
    {
        install(QStringLiteral("com.example.tidy"), QStringLiteral("Tidy"), R"~(
            local function fetch(urls)
                for _, url in ipairs(urls) do
                    local _, err = hikari.net.fetch(url)
                    hikari.notify(url .. ' ' .. err.code .. ' ' .. tostring(err.data.reason or err.data.host))
                end
            end
            hikari.commands.register('greet', function()
                fetch({'https://api.example.com/v1', 'http://api.example.com/v1', 'https://evil.example.net/'})
            end)
            hikari.commands.register('spin', function()
                fetch({'http://api.example.com/v1', 'https://evil.example.net/'})
            end)
        )~", true, R"(, "permissions": {"network": {"hosts": ["api.example.com"], "reason": "Looks words up"}})");
        auto a = openEpisodes(1);
        QVERIFY(a);
        QSignalSpy noticed(&a->log(), &ui::LogController::noticed);
        bool allowed = false;
        auto w = wiring(*a);
        w.networkAllowed = [&] { return allowed; };
        app::PluginRuntime runtime(std::move(w));
        runtime.sync();
        QVERIFY(runtime.run(kTidyGreet));
        QTRY_COMPARE(texts(noticed).size(), 3);
        QCOMPARE(texts(noticed), (QStringList{QStringLiteral("Tidy: https://api.example.com/v1 refused network_off"),
                                              QStringLiteral("Tidy: http://api.example.com/v1 refused network_off"),
                                              QStringLiteral("Tidy: https://evil.example.net/ refused network_off")}));
        allowed = true;
        noticed.clear();
        QVERIFY(runtime.run(kTidySpin));
        QTRY_COMPARE(texts(noticed).size(), 2);
        QCOMPARE(texts(noticed), (QStringList{QStringLiteral("Tidy: http://api.example.com/v1 refused not_https"),
                                              QStringLiteral("Tidy: https://evil.example.net/ not_granted evil.example.net")}));
    }
};

QTEST_MAIN(PluginRuntimeTests)
#include "plugin_runtime_tests.moc"
