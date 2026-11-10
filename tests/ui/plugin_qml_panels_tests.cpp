#include "hikari/app/application.h"
#include "hikari/app/plugin_runtime.h"
#include "hikari/app/plugin_ui.h"
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <filesystem>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)
Q_IMPORT_QML_PLUGIN(Hikari_PluginPlugin)

using namespace hikari;
using backends::PluginProcess;

namespace {

const QString kPlugin = QStringLiteral("com.example.board");
const QString kBoard = QStringLiteral("plugin:com.example.board/board");
const QString kPing = QStringLiteral("plugin.com.example.board.ping");

QString fixture(const QString &path)
{
    return QStringLiteral(HIKARI_PLUGIN_FIXTURES "/qml-board/") + path;
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}

class PluginQmlPanelTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<app::Application> m_app;

    app::PluginManager::Folders folders() const
    {
        return {m_dir.filePath(QStringLiteral("user/plugins")), m_dir.filePath(QStringLiteral("user/plugin-data")),
                m_dir.filePath(QStringLiteral("cache/plugins")), m_dir.filePath(QStringLiteral("user/plugins.json"))};
    }
    QString installed(const QString &path) const { return folders().plugins + QStringLiteral("/com.example.board/") + path; }
    QString loadedPanelsFile() const { return m_dir.filePath(QStringLiteral("user/plugin-panels.json")); }

    void write(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
    }

    void install(const QString &version = QStringLiteral("1.0.0"), bool enabled = true)
    {
        for (const QString &path : {QStringLiteral("hikari-plugin.json"), QStringLiteral("main.lua"), QStringLiteral("qml/Board.qml"),
                                    QStringLiteral("qml/Noisy.qml"), QStringLiteral("qml/parts/Badge.qml")})
            write(installed(path), read(fixture(path)));
        write(installed(QStringLiteral("hikari-plugin.json")),
              read(fixture(QStringLiteral("hikari-plugin.json"))).replace("\"1.0.0\"", "\"" + version.toUtf8() + "\""));
        const QJsonObject entry{{QStringLiteral("version"), version}, {QStringLiteral("enabled"), enabled}};
        write(folders().registry, QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                                            {QStringLiteral("plugins"), QJsonObject{{kPlugin, entry}}}})
                                      .toJson());
    }

    void edit(const QString &path, const QByteArray &from, const QByteArray &to)
    {
        QByteArray text = read(installed(path));
        QVERIFY(text.contains(from));
        write(installed(path), text.replace(from, to));
    }

    app::PluginRuntime::Wiring wiring()
    {
        return {QStringLiteral(HIKARI_PLUGIN_HELPER), folders(), &m_app->apiHost()->api(), &m_app->log(), {}, {}};
    }

    std::unique_ptr<QQuickItem> body(QQmlEngine &engine, app::PluginUi *ui, bool shown)
    {
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Hikari.Ui\nPluginPanelBody { width: 400; height: 300 }\n",
                          QUrl(QStringLiteral("qrc:/pl8-test.qml")));
        QObject *made = component.createWithInitialProperties(
            {{QStringLiteral("ui"), QVariant::fromValue(ui)}, {QStringLiteral("dock"), kBoard}, {QStringLiteral("shown"), shown}});
        if (!made)
            qWarning() << component.errors();
        return std::unique_ptr<QQuickItem>(qobject_cast<QQuickItem *>(made));
    }

    static QQuickItem *child(QQuickItem *root, const char *name)
    {
        return root ? root->findChild<QQuickItem *>(QLatin1String(name)) : nullptr;
    }

    static QStringList logged(const QSignalSpy &added)
    {
        QStringList out;
        for (const auto &args : added)
            if (args.at(2).toString() == QStringLiteral("Board"))
                out << args.at(3).toString();
        return out;
    }

    QJsonObject loadedPanels() const { return QJsonDocument::fromJson(read(loadedPanelsFile())).object(); }

    static QVariant call(QQuickItem *item, const char *function, const QVariant &argument = {})
    {
        QVariant result;
        if (argument.isValid())
            QMetaObject::invokeMethod(item, function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
        else
            QMetaObject::invokeMethod(item, function, Q_RETURN_ARG(QVariant, result));
        return result;
    }

private slots:
    void init()
    {
        std::error_code error;
        std::filesystem::remove_all(m_dir.path().toStdU16String(), error);
        QVERIFY2(!error, error.message().c_str());
        QDir().mkpath(m_dir.path());
        write(m_dir.filePath(QStringLiteral("episode.ass")), "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                                                           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                                           "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\n"
                                                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n");
        app::Application::Options o;
        o.settingsFile = m_dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        m_app = std::make_unique<app::Application>(o);
        QVERIFY(m_app->openFile(m_dir.filePath(QStringLiteral("episode.ass"))));
    }

    void cleanup() { m_app.reset(); }

    void aFullTrustQmlPanelLoadsOnFirstShowAndReachesItsApi()
    {
        install();
        QSignalSpy added(&m_app->log(), &ui::LogController::added);
        app::PluginRuntime runtime(wiring());
        runtime.sync();
        app::PluginUi *ui = runtime.ui();
        QTRY_VERIFY(runtime.process(kPlugin)->state() == PluginProcess::State::Ready);
        app::PluginPanelState *panel = ui->panel(kBoard);
        QVERIFY(panel->qml());
        QVERIFY(!ui->panel(QStringLiteral("plugin:com.example.board/notes"))->qml());

        QVERIFY(runtime.run(kPing));
        QTRY_VERIFY(logged(added).contains(QStringLiteral("false")));

        QQmlEngine engine;
        auto host = body(engine, ui, false);
        QVERIFY(host);
        QVERIFY(!panel->item());
        host->setProperty("shown", true);
        QTRY_VERIFY(panel->item());
        QQuickItem *board = panel->item();
        QCOMPARE(board->objectName(), QStringLiteral("board"));
        QCOMPARE(board->parentItem(), child(host.get(), "pluginQmlHost"));
        QCOMPARE(board->width(), 400.0);
        QVERIFY(!child(host.get(), "pluginQmlTrouble")->isVisible());
        QVERIFY(!child(host.get(), "pluginPanelPlaceholder")->isVisible());
        QCOMPARE(host->property("minimumBodySize").toSizeF(), QSizeF(220, 100));
        QCOMPARE(board->property("accent").value<QColor>(), ui::theme::current().roles.accent);
        QCOMPARE(board->property("language").toString(), QStringLiteral("en"));
        QCOMPARE(board->property("count").toInt(), 0);
        QCOMPARE(loadedPanels().value(QStringLiteral("loaded")).toArray(), QJsonArray{kPlugin});

        call(board, "countLines");
        QTRY_COMPARE(board->property("lines").toString(), QStringLiteral("2"));
        call(board, "callNothing");
        QTRY_COMPARE(board->property("failed").toString(), QStringLiteral("unknown_operation"));
        call(board, "say", QStringLiteral("hi"));
        QTRY_COMPARE(board->property("echo").toString(), QStringLiteral("hi!"));
        added.clear();
        QVERIFY(runtime.run(kPing));
        QTRY_VERIFY(logged(added).contains(QStringLiteral("true")));
        QTRY_COMPARE(board->property("echo").toString(), QStringLiteral("ping"));

        call(board, "keep", 3);
        QCOMPARE(board->property("refused").toString(), QString());
        QTRY_COMPARE(board->property("count").toInt(), 3);
        call(board, "keep", 11);
        QVERIFY(!board->property("refused").toString().isEmpty());
        QCOMPARE(board->property("count").toInt(), 3);

        runtime.forceStop(kPlugin);
        QTRY_COMPARE(panel->status(), QStringLiteral("stopped"));
        QVERIFY(!panel->item());
        QVERIFY(child(host.get(), "pluginPanelPlaceholder")->isVisible());
        QTRY_VERIFY(!QFile::exists(loadedPanelsFile()));
        runtime.restart(kPlugin);
        QTRY_VERIFY(panel->item());

        install(QStringLiteral("1.0.1"));
        edit(QStringLiteral("qml/Board.qml"), "property string version: \"1\"", "property string version: \"2\"");
        edit(QStringLiteral("qml/parts/Badge.qml"), "font.bold: true", "font.bold: true\n    property bool fresh: true");
        runtime.sync();
        QTRY_COMPARE(panel->item() ? panel->item()->property("version").toString() : QString(), QStringLiteral("2"));
        QVERIFY(panel->item()->findChild<QObject *>(QStringLiteral("badge"))->property("fresh").toBool());

        install(QStringLiteral("1.0.1"), false);
        runtime.sync();
        QCOMPARE(panel->status(), QStringLiteral("off"));
        QVERIFY(!panel->item());
        QTRY_VERIFY(!QFile::exists(loadedPanelsFile()));
    }

    void aQmlPanelThatCantLoadSaysWhyAndLoadsAgainOnceFixed()
    {
        install();
        write(installed(QStringLiteral("qml/Board.qml")), "import QtQuick\nItem {\n");
        QSignalSpy added(&m_app->log(), &ui::LogController::added);
        app::PluginRuntime runtime(wiring());
        runtime.sync();
        app::PluginUi *ui = runtime.ui();
        QTRY_VERIFY(runtime.process(kPlugin)->state() == PluginProcess::State::Ready);
        QQmlEngine engine;
        auto host = body(engine, ui, true);
        app::PluginPanelState *panel = ui->panel(kBoard);
        QTRY_VERIFY(!panel->loadError().isEmpty());
        QVERIFY2(panel->loadError().startsWith(QStringLiteral("qml/Board.qml:")), qPrintable(panel->loadError()));
        QVERIFY(!panel->item());
        QVERIFY(child(host.get(), "pluginQmlTrouble")->isVisible());
        QCOMPARE(child(host.get(), "pluginQmlTroubleText")->property("text").toString(), QStringLiteral("The panel couldn’t load."));
        QCOMPARE(child(host.get(), "pluginQmlTroubleDetail")->property("text").toString(), panel->loadError());
        QVERIFY(child(host.get(), "pluginQmlCopy")->isVisible());
        const QStringList lines = logged(added);
        QVERIFY(std::ranges::any_of(lines, [](const QString &l) { return l.startsWith(QStringLiteral("Its panel “Board” couldn’t load: qml/Board.qml:")); }));
        QCOMPARE(runtime.process(kPlugin)->state(), PluginProcess::State::Ready);
        QVERIFY(!QFile::exists(loadedPanelsFile()));

        write(installed(QStringLiteral("qml/Board.qml")), "import QtQuick\nimport Hikari.Ui\nItem { required property var api }\n");
        ui->loadAgain(kBoard);
        QCOMPARE(panel->loadError(), QStringLiteral("qml/Board.qml imports Hikari.Ui, which plugin panels can’t use."));

        write(installed(QStringLiteral("qml/Board.qml")), read(fixture(QStringLiteral("qml/Board.qml"))));
        ui->loadAgain(kBoard);
        QTRY_VERIFY(panel->item());
        QCOMPARE(panel->loadError(), QString());
        QVERIFY(!child(host.get(), "pluginQmlTrouble")->isVisible());
    }

    void onlyAFullTrustPluginsQmlPanelLoads()
    {
        install();
        app::PluginUi ui([](const std::string &, const std::string &, const std::string &, const std::string &) { return true; });
        plugins::Panel panel{"board", "Board", plugins::PanelArea::Right, "qml/Board.qml"};
        ui.setPlugins({{kPlugin.toStdString(), "Board", true, {panel}, QUrl::fromLocalFile(installed(QString())), false, "1.0.0"}});
        QQmlEngine engine;
        auto host = body(engine, &ui, true);
        QCOMPARE(ui.panel(kBoard)->loadError(), QStringLiteral("Only Full trust plugins can ship QML panels."));
        QVERIFY(!ui.panel(kBoard)->item());

        edit(QStringLiteral("hikari-plugin.json"), "\"tier\": \"unrestricted\",", "");
        QSignalSpy added(&m_app->log(), &ui::LogController::added);
        app::PluginRuntime runtime(wiring());
        runtime.sync();
        QVERIFY(!runtime.process(kPlugin));
        QCOMPARE(runtime.ui()->panel(kBoard)->status(), QStringLiteral("missing"));
        QVERIFY(std::ranges::any_of(added, [](const QList<QVariant> &args) {
            return args.at(3).toString().startsWith(QStringLiteral("Can’t start: "));
        }));
    }

    void aPanelOpenWhenHikariSubClosedWaitsForLoadAgain()
    {
        install();
        write(loadedPanelsFile(), R"({"loaded": ["com.example.board"]})");
        QQmlEngine engine;
        {
            QSignalSpy added(&m_app->log(), &ui::LogController::added);
            app::PluginRuntime runtime(wiring());
            runtime.sync();
            app::PluginUi *ui = runtime.ui();
            app::PluginPanelState *panel = ui->panel(kBoard);
            QVERIFY(panel->held());
            QCOMPARE(logged(added), QStringList{QStringLiteral("HikariSub closed while its panel was open, so its panels "
                                                               "stay closed until you choose Load again.")});
            QCOMPARE(loadedPanels(), (QJsonObject{{QStringLiteral("loaded"), QJsonArray()}, {QStringLiteral("held"), QJsonArray{kPlugin}}}));
            QTRY_VERIFY(runtime.process(kPlugin)->state() == PluginProcess::State::Ready);
            auto host = body(engine, ui, true);
            QTest::qWait(50);
            QVERIFY(!panel->item());
            QVERIFY(child(host.get(), "pluginQmlTrouble")->isVisible());
            QCOMPARE(child(host.get(), "pluginQmlTroubleText")->property("text").toString(),
                     QStringLiteral("HikariSub closed while Board’s panel was open."));
            QVERIFY(!child(host.get(), "pluginQmlCopy")->isVisible());

            ui->loadAgain(kBoard);
            QTRY_VERIFY(panel->item());
            QVERIFY(!panel->held());
            QCOMPARE(loadedPanels(), (QJsonObject{{QStringLiteral("loaded"), QJsonArray{kPlugin}}, {QStringLiteral("held"), QJsonArray()}}));
        }
        QVERIFY2(!QFile::exists(loadedPanelsFile()), "a clean close forgets the loaded panels");

        write(loadedPanelsFile(), R"({"held": ["com.example.board"]})");
        QSignalSpy added(&m_app->log(), &ui::LogController::added);
        app::PluginRuntime runtime(wiring());
        runtime.sync();
        QVERIFY2(runtime.ui()->panel(kBoard)->held(), "a hold lasts until Load again");
        QCOMPARE(logged(added), QStringList());
    }

    void aLoadedPanelsFileThatCantBeSavedIsLogged()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows ignores a folder's read-only attribute");
#endif
        const QString folder = m_dir.filePath(QStringLiteral("locked"));
        const QString file = QDir(folder).filePath(QStringLiteral("plugin-panels.json"));
        write(file, R"({"held": ["com.example.board"]})");
        QVERIFY(QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner));
        const auto unlock = qScopeGuard([&] { QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner); });
        if (QFileInfo(folder).isWritable())
            QSKIP("permissions don't apply to this user");

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^Couldn't save .*plugin-panels\\.json")));
        app::PluginUi ui([](const std::string &, const std::string &, const std::string &, const std::string &) { return true; });
        ui.keepLoadedPanelsIn(file);
    }

    void aPanelsWarningsReachTheLogAtMostTwentyAMinute()
    {
        install();
        edit(QStringLiteral("hikari-plugin.json"), "\"qml/Board.qml\"", "\"qml/Noisy.qml\"");
        QSignalSpy added(&m_app->log(), &ui::LogController::added);
        app::PluginRuntime runtime(wiring());
        runtime.sync();
        app::PluginUi *ui = runtime.ui();
        QTRY_VERIFY(runtime.process(kPlugin)->state() == PluginProcess::State::Ready);
        QQmlEngine engine;
        auto host = body(engine, ui, true);
        QTRY_VERIFY(ui->panel(kBoard)->item());
        QTRY_COMPARE(logged(added).size(), app::PluginUi::kQmlWarningsPerMinute);
        QTest::qWait(100);
        const QStringList lines = logged(added);
        QCOMPARE(lines.size(), app::PluginUi::kQmlWarningsPerMinute);
        for (const QString &line : lines)
            QVERIFY2(line.startsWith(QStringLiteral("qml/Noisy.qml:9: ReferenceError: missingName")), qPrintable(line));
    }
};

QTEST_MAIN(PluginQmlPanelTests)
#include "plugin_qml_panels_tests.moc"
