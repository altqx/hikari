#include "hikari/app/application.h"
#include "hikari/app/session_json.h"
#include "hikari/app/forwarded_launches.h"
#include "hikari/app/single_instance.h"
#include "hikari/application/session_file.h"
#include "docking.h"
#include "fake_network.h"
#include "settings_store.h"

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString writeAss(const QString &path, const char *text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n"
            "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,," +
            QByteArray(text) + "\n");
    return path;
}

QString media(const char *name)
{
    return QStringLiteral(HIKARI_MEDIA_FIXTURES "/") + QLatin1String(name);
}

QByteArray pluginFixture(const QString &name)
{
    QFile f(QStringLiteral(HIKARI_PLUGIN_FIXTURES "/") + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString installLink(const char *id)
{
    return QStringLiteral("hikarisub://plugin/install?id=") + QLatin1String(id);
}

}

class ForwardedLaunchesTests : public QObject {
    Q_OBJECT

    QTemporaryDir home;
    QTemporaryDir dir;
    app::Application *application = nullptr;
    app::ForwardedLaunches *forwarded = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    std::unique_ptr<hikari::test::FakeNetwork> market;

    app::Application::Options options() const
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        return o;
    }
    void start(int restore = 0) { startWith(options(), restore); }
    void startWith(const app::Application::Options &o, int restore = 0, const QStringList &beforeWindow = {})
    {
        application = new app::Application(o);
        application->setSessionRestore(restore);
        forwarded = new app::ForwardedLaunches(*application);
        if (!beforeWindow.isEmpty())
            take(beforeWindow);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        forwarded->setWindow(window);
    }
    QObject *named(const char *name) const
    {
        for (QObject *o : engine->rootObjects().first()->findChildren<QObject *>())
            if (o->objectName() == QLatin1String(name))
                return o;
        return nullptr;
    }
    QStringList titles() const
    {
        QStringList out;
        for (const QVariant &tab : application->tabs())
            out << tab.toMap().value(QStringLiteral("title")).toString();
        return out;
    }
    application::EditSession *session(int tab) const
    {
        return application->files().session(application->workspace().tabs()[std::size_t(tab)]);
    }
    void edit(int tab, const char *text)
    {
        auto *s = session(tab);
        QVERIFY(s);
        const auto line = s->document().lines().front()->id;
        QVERIFY(s->editDraftText(line, std::u8string(reinterpret_cast<const char8_t *>(text))));
        QVERIFY(s->commitDraft());
        QVERIFY(s->isDirty());
    }
    void take(const QStringList &files) { forwarded->take({files, {}}); }
    void startMarketplace(const QStringList &beforeWindow = {}, const QString &sideload = {})
    {
        const QString base = QStringLiteral("https://marketplace.test/");
        market = std::make_unique<hikari::test::FakeNetwork>();
        market->files = {
            {base + QStringLiteral("index.json"), pluginFixture(QStringLiteral("marketplace/index.json"))},
            {base + QStringLiteral("index.json.minisig"), pluginFixture(QStringLiteral("marketplace/index.json.minisig"))},
            {base + QStringLiteral("p/com.example.hello/1.1.0.hikariplugin"), pluginFixture(QStringLiteral("hello-1.1.0.hikariplugin"))},
            {base + QStringLiteral("p/com.example.native/1.0.0.hikariplugin"),
             pluginFixture(QStringLiteral("native-1.0.0-unrestricted.hikariplugin"))},
        };
        QDir(home.filePath(QStringLiteral("market"))).removeRecursively();
        app::Application::Options o = options();
        o.plugins = {home.filePath(QStringLiteral("market/plugins")), home.filePath(QStringLiteral("market/plugin-data")),
                     home.filePath(QStringLiteral("market/cache")), home.filePath(QStringLiteral("market/plugins.json"))};
        o.marketplace = {QUrl(base), {*hikari::minisign::parsePublicKey("RWT+3LqYdlQyECmsuuFBvMrwsi4alNNNC8c2HlJtC/4SyJeUvJMilm3X")}};
        o.marketplaceNetwork = market.get();
        startWith(o, 0, beforeWindow);
        if (!sideload.isEmpty()) {
            QVERIFY(application->pluginManager()
                        ->review(QUrl::fromLocalFile(QStringLiteral(HIKARI_PLUGIN_FIXTURES "/") + sideload))
                        .value(QStringLiteral("ok"))
                        .toBool());
            QCOMPARE(application->pluginManager()->install(false), QString());
        }
    }
    QQuickWindow *pluginMessage() const
    {
        QObject *host = named("pluginManagerMessages");
        auto *box = host ? qvariant_cast<QQuickWindow *>(host->property("box")) : nullptr;
        return box && box->isVisible() ? box : nullptr;
    }
    void answerPluginMessage(const QString &title, const QString &text, const QString &choice)
    {
        QQuickWindow *box = nullptr;
        QTRY_VERIFY((box = pluginMessage()));
        QCOMPARE(box->title(), title);
        QCOMPARE(box->property("text").toString(), text);
        QVERIFY(QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, choice)));
        QTRY_VERIFY(!pluginMessage());
    }
    QObject *pluginManager() const { return named("pluginManagerDialog"); }
    void showsListing(const QString &id)
    {
        QTRY_VERIFY(pluginManager()->property("visible").toBool());
        QCOMPARE(pluginManager()->property("view").toString(), QStringLiteral("market"));
        QTRY_COMPARE(pluginManager()->property("chosenListingId").toString(), id);
    }
    void staysWaiting(const QStringList &before)
    {
        QTest::qWait(1000);
        QCOMPARE(titles(), before);
        QVERIFY(forwarded->pending());
    }

private slots:
    void initTestCase() { QVERIFY(home.isValid() && dir.isValid()); }
    void cleanup()
    {
        delete engine;
        engine = nullptr;
        delete forwarded;
        forwarded = nullptr;
        delete application;
        application = nullptr;
        market.reset();
        for (const char *name : {"settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(home.filePath(QLatin1String(name)));
        QFile::remove(home.filePath(QStringLiteral("last-session.json")));
    }

    void aForwardedLinkOpensTheInstallQuestion()
    {
        startMarketplace();
        take({installLink("com.example.native")});
        answerPluginMessage(QStringLiteral("Install plugin"), QStringLiteral("Install “Native Tools” 1.0.0?"), QStringLiteral("cancel"));
        showsListing(QStringLiteral("com.example.native"));
        QVERIFY(application->pluginManager()->plugins().isEmpty());
        QVERIFY(std::ranges::any_of(market->requests, [](const QNetworkRequest &r) {
            return r.url().path() == QStringLiteral("/p/com.example.native/1.0.0.hikariplugin");
        }));
    }

    void aLinkOnAColdStartWaitsForTheWindow()
    {
        startMarketplace({installLink("com.example.native")});
        answerPluginMessage(QStringLiteral("Install plugin"), QStringLiteral("Install “Native Tools” 1.0.0?"), QStringLiteral("cancel"));
        showsListing(QStringLiteral("com.example.native"));
    }

    void aLinkToTheNewestInstalledVersionOnlySelectsIt()
    {
        startMarketplace({}, QStringLiteral("hello-1.1.0.hikariplugin"));
        take({installLink("com.example.hello")});
        showsListing(QStringLiteral("com.example.hello"));
        QTRY_COMPARE(market->requests.size(), 2);
        QTest::qWait(500);
        QVERIFY(!pluginMessage());
        QCOMPARE(market->requests.size(), 2);
    }

    void aLinkToAnUnlistedPluginSaysSo()
    {
        startMarketplace();
        take({installLink("com.example.missing")});
        answerPluginMessage(QStringLiteral("Plugin not installed"),
                            QStringLiteral("“com.example.missing” isn’t in the marketplace for this HikariSub."), QStringLiteral("ok"));
    }

    void aLinkWhilePluginUpdatesAreOffAsksToTurnThemOn()
    {
        startMarketplace();
        application->settingsStore()->set("plugins.checkUpdates", false);
        take({installLink("com.example.native")});
        answerPluginMessage(QStringLiteral("Install plugin"),
                            QStringLiteral("“com.example.native” can’t be installed while plugin updates are off."), QStringLiteral("close"));
        QTest::qWait(500);
        QVERIFY(!pluginManager()->property("visible").toBool());
        QVERIFY(market->requests.isEmpty());
        QVERIFY(!application->settingsStore()->boolean("plugins.checkUpdates"));

        take({installLink("com.example.native")});
        answerPluginMessage(QStringLiteral("Install plugin"),
                            QStringLiteral("“com.example.native” can’t be installed while plugin updates are off."), QStringLiteral("turnOn"));
        QVERIFY(application->settingsStore()->boolean("plugins.checkUpdates"));
        answerPluginMessage(QStringLiteral("Install plugin"), QStringLiteral("Install “Native Tools” 1.0.0?"), QStringLiteral("cancel"));
        showsListing(QStringLiteral("com.example.native"));
    }

    void refusedLinksAreOnlyLogged()
    {
        startMarketplace();
        const QStringList refused{QStringLiteral("hikarisub://plugin/uninstall?id=com.example.hello"),
                                  QStringLiteral("hikarisub://plugin/install?id=com.example.hello&url=https://evil.test/x"),
                                  QStringLiteral("hikarisub://plugin/install?id=com%2Eexample.hello"),
                                  installLink("com.example.hello") + QString(5000, QLatin1Char('a'))};
        for (qsizetype i = 0; i < refused.size(); ++i)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^Ignored a link HikariSub doesn't take: ")));
        take(refused);
        QTest::qWait(1000);
        QVERIFY(!pluginMessage());
        QVERIFY(!pluginManager()->property("visible").toBool());
        QVERIFY(market->requests.isEmpty());
        QVERIFY(!forwarded->pending());
        QVERIFY(titles().isEmpty());
    }

    void onlyTheLastOfSeveralLinksCounts()
    {
        startMarketplace();
        take({installLink("com.example.missing")});
        take({installLink("com.example.native"), installLink("com.example.glossary"), installLink("com.example.native")});
        answerPluginMessage(QStringLiteral("Install plugin"), QStringLiteral("Install “Native Tools” 1.0.0?"), QStringLiteral("cancel"));
        QTest::qWait(500);
        QVERIFY(!pluginMessage());
    }

    void aFileUrlOpensTheFile()
    {
        const QString path = writeAss(dir.filePath(QStringLiteral("a b/字幕.ass")), "url");
        const QString url = QString::fromLatin1(QUrl::fromLocalFile(path).toEncoded());
        QVERIFY(url.contains(QStringLiteral("a%20b/%E5%AD%97%E5%B9%95.ass")));
        const QStringList files = app::launchFiles({url}, dir.path());
        QCOMPARE(files, QStringList{QDir::cleanPath(path)});
        start();
        take(files);
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("字幕.ass")});
        cleanup();
        startWith(options(), 0, files);
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("字幕.ass")});
    }

    void theUntouchedEmptyTabTakesTheFile()
    {
        start();
        QVERIFY(titles().isEmpty());
        take({writeAss(dir.filePath(QStringLiteral("a.ass")), "a")});
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("a.ass")});
        application->addPage();
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("Untitled")}));
        take({writeAss(dir.filePath(QStringLiteral("b.ass")), "b")});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass")}));
        QCOMPARE(application->currentTab(), 1);
    }

    void aSessionFileLoadsAsASession()
    {
        start();
        application::SessionTab first, second;
        first.subtitles = writeAss(dir.filePath(QStringLiteral("s1.ass")), "s1").toStdString();
        second.subtitles = writeAss(dir.filePath(QStringLiteral("s2.ass")), "s2").toStdString();
        const QString file = dir.filePath(QStringLiteral("two.hksession"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(app::writeSessionJson(application::Session{"HikariSub", true, {first, second}, 0}));
        }
        take({file});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("s1.ass"), QStringLiteral("s2.ass")}));
        QCOMPARE(application->currentTab(), 0);
        take({writeAss(dir.filePath(QStringLiteral("s3.ass")), "s3"), file});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("s1.ass"), QStringLiteral("s2.ass"), QStringLiteral("s3.ass")}));
    }

    void aSessionOnAnotherComputerAsksFirst()
    {
        start();
        application::SessionTab share;
        share.subtitles = "\\\\nas\\subs\\x.ass";
        const QString file = dir.filePath(QStringLiteral("shared.hksession"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(app::writeSessionJson(application::Session{"HikariSub", true, {share}, {}}));
        }
        take({file});
        QQuickWindow *box = nullptr;
        QTRY_VERIFY((box = qvariant_cast<QQuickWindow *>(named("sessionMessages")->property("box"))) && box->isVisible());
        QVERIFY(QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!box->isVisible());
        QVERIFY(titles().isEmpty());
        take({file});
        QTRY_VERIFY(box->isVisible());
        QVERIFY(QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, QStringLiteral("open"))));
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("Untitled")});
        QCOMPARE(application->unresolvedRestores().size(), qsizetype(1));
    }

    void aTabWithWorkKeepsIt()
    {
        start();
        take({writeAss(dir.filePath(QStringLiteral("a.ass")), "a")});
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("a.ass")});
        edit(0, "mine");
        application->addPage();
        edit(1, "draft");
        take({writeAss(dir.filePath(QStringLiteral("b.ass")), "b")});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("Untitled"), QStringLiteral("b.ass")}));
        QCOMPARE(application->currentTab(), 2);
        QVERIFY(session(0)->isDirty() && session(1)->isDirty());
        const QByteArray last = [&] {
            QFile f(application->lastSessionPath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(last.contains("b.ass"));
    }

    void startsInARowOpenTogether()
    {
        start();
        application->addPage();
        edit(0, "work");
        take({writeAss(dir.filePath(QStringLiteral("row/d.ass")), "d")});
        take({writeAss(dir.filePath(QStringLiteral("row/c.ass")), "c")});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("Untitled"), QStringLiteral("c.ass"), QStringLiteral("d.ass")}));
        QVERIFY(session(0)->isDirty());
    }

    void tabsWithSomethingOfTheirOwnKeepIt()
    {
        {
            app::Application::Options o;
            o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
            o.playbackAudio = false;
            app::Application before(o);
            QVERIFY(before.openFile(writeAss(dir.filePath(QStringLiteral("m/gone.ass")), "g")));
        }
        QVERIFY(QFile::remove(dir.filePath(QStringLiteral("m/gone.ass"))));
        start(2);
        QTRY_COMPARE(application->unresolvedRestores().size(), 1);
        QTRY_VERIFY(!application->forwardedOpenWaits());
        QCOMPARE(titles(), QStringList{QStringLiteral("Untitled")});
        take({writeAss(dir.filePath(QStringLiteral("m/b.ass")), "b")});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("Untitled"), QStringLiteral("b.ass")}));
        QCOMPARE(application->unresolvedRestores().size(), 1);

        application->addPage();
        const QString keyframes = dir.filePath(QStringLiteral("m/k.txt"));
        QFile k(keyframes);
        QVERIFY(k.open(QIODevice::WriteOnly));
        k.write("# keyframe format v1\nfps 0\n0\n24\n");
        k.close();
        application->openKeyframes(QUrl::fromLocalFile(keyframes));
        take({writeAss(dir.filePath(QStringLiteral("m/c.ass")), "c")});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("Untitled"), QStringLiteral("b.ass"), QStringLiteral("Untitled"),
                                            QStringLiteral("c.ass")}));
    }

    void aVideoOpensInANewTab()
    {
        start();
        take({writeAss(dir.filePath(QStringLiteral("a.ass")), "a")});
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("a.ass")});
        const QString video = dir.filePath(QStringLiteral("v/clip.mkv"));
        QDir().mkpath(QFileInfo(video).absolutePath());
        QFile::remove(video);
        QVERIFY(QFile::copy(media("cfr.mkv"), video));
        writeAss(dir.filePath(QStringLiteral("v/clip.ass")), "c");
        take({video});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("Untitled")}));
        QCOMPARE(application->currentTab(), 1);
        QTRY_VERIFY(named("subtitlesFromVideoPrompt"));
        QObject *prompt = named("subtitlesFromVideoPrompt");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QMetaObject::invokeMethod(prompt, "reject");
    }

    void aKeyframesFileOpensInANewTab()
    {
        start();
        take({writeAss(dir.filePath(QStringLiteral("k/a.ass")), "a")});
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("a.ass")});
        const QString keyframes = dir.filePath(QStringLiteral("k/clip_keyframes.txt"));
        QFile k(keyframes);
        QVERIFY(k.open(QIODevice::WriteOnly));
        k.write("# keyframe format v1\nfps 0\n0\n24\n");
        k.close();
        take({keyframes});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("Untitled")}));
        QCOMPARE(application->currentTab(), 1);
        auto kinds = [&](int tab) {
            QStringList out;
            for (const QVariant &folder : application->tabMenu(tab).value(QStringLiteral("folders")).toList())
                out << folder.toMap().value(QStringLiteral("kind")).toString();
            return out;
        };
        QVERIFY(!kinds(0).contains(QStringLiteral("keyframes")));
        QVERIFY(kinds(1).contains(QStringLiteral("keyframes")));
    }

    void filesWaitForAReview()
    {
        start();
        application->addPage();
        edit(0, "work");
        const QVariantList rows = application->reviewClose(QStringLiteral("close"));
        QCOMPARE(rows.size(), 1);
        take({writeAss(dir.filePath(QStringLiteral("e.ass")), "e")});
        staysWaiting({QStringLiteral("Untitled")});
        application->cancelClose();
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("Untitled"), QStringLiteral("e.ass")}));
    }

    void filesWaitForAModalDialog()
    {
        start();
        application->addPage();
        edit(0, "work");
        QVERIFY(QMetaObject::invokeMethod(named("tabCommands"), "confirmCloseAll"));
        QObject *prompt = named("closeAllPrompt");
        QVERIFY(prompt);
        QTRY_VERIFY(prompt->property("visible").toBool());
        take({writeAss(dir.filePath(QStringLiteral("f.ass")), "f")});
        staysWaiting({QStringLiteral("Untitled")});
        QMetaObject::invokeMethod(prompt, "reject");
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("Untitled"), QStringLiteral("f.ass")}));
    }

    void filesDoNotWaitForTheStartupOffer()
    {
        {
            app::Application::Options o;
            o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
            o.playbackAudio = false;
            app::Application before(o);
            QVERIFY(before.openFile(writeAss(dir.filePath(QStringLiteral("s/last.ass")), "l")));
        }
        start(1);
        QObject *offer = named("homeOffer");
        QVERIFY(offer);
        QTRY_VERIFY(offer->property("visible").toBool());
        take({writeAss(dir.filePath(QStringLiteral("s/g.ass")), "g")});
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("g.ass")});
        QVERIFY(!forwarded->pending());
        QVERIFY(!named("startupBar")->property("visible").toBool());
    }

    void forwardedFilesEndTheStartupBar()
    {
        {
            app::Application::Options o;
            o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
            o.playbackAudio = false;
            app::Application before(o);
            QVERIFY(before.openFile(writeAss(dir.filePath(QStringLiteral("b/last.ass")), "l")));
        }
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        application = new app::Application(o);
        application->setSessionRestore(1);
        application->setStartedWithPaths(true);
        QVERIFY(application->openFile(writeAss(dir.filePath(QStringLiteral("b/given.ass")), "g")));
        forwarded = new app::ForwardedLaunches(*application);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        forwarded->setWindow(window);
        QObject *bar = named("startupBar");
        QVERIFY(bar);
        QTRY_VERIFY(bar->property("visible").toBool());
        take({dir.filePath(QStringLiteral("b/missing.ass"))});
        QTRY_VERIFY(!forwarded->pending());
        QTest::qWait(300);
        QVERIFY(bar->property("visible").toBool());
        const QString broken = dir.filePath(QStringLiteral("b/broken.hksession"));
        {
            QFile f(broken);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ not json");
        }
        take({broken});
        QTRY_VERIFY(!forwarded->pending());
        QTest::qWait(300);
        QVERIFY(bar->property("visible").toBool());
        take({writeAss(dir.filePath(QStringLiteral("b/more.ass")), "m")});
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("given.ass"), QStringLiteral("more.ass")}));
        QTRY_VERIFY(!bar->property("visible").toBool());
    }

    void filesOpenAfterTheStartupSession()
    {
        {
            app::Application::Options o;
            o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
            o.playbackAudio = false;
            app::Application before(o);
            QVERIFY(before.openFile(writeAss(dir.filePath(QStringLiteral("s/last.ass")), "l")));
        }
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        application = new app::Application(o);
        application->setSessionRestore(2);
        forwarded = new app::ForwardedLaunches(*application);
        forwarded->take({{writeAss(dir.filePath(QStringLiteral("s/h.ass")), "h")}, {}});
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        forwarded->setWindow(window);
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("last.ass"), QStringLiteral("h.ass")}));
    }

    void filesTheQuitLeavesGoToTheNextStart()
    {
        start();
        application->addPage();
        edit(0, "work");
        const QVariantList rows = application->reviewClose(QStringLiteral("quit"));
        QCOMPARE(rows.size(), 1);
        const QString file = writeAss(dir.filePath(QStringLiteral("q/late.ass")), "q");
        take({file});
        staysWaiting({QStringLiteral("Untitled")});
        QVariantList discard;
        for (const QVariant &row : rows)
            discard << QVariantMap{{QStringLiteral("id"), row.toMap().value(QStringLiteral("id"))},
                                   {QStringLiteral("save"), false}};
        application->resolveClose(discard);
        QTRY_VERIFY(application->quitApproved());
        QTest::qWait(500);
        QCOMPARE(titles(), QStringList{QStringLiteral("Untitled")});
        QCOMPARE(forwarded->takeUnopened(), QStringList{file});
        QVERIFY(!forwarded->pending());
    }

    void aStartWithoutFilesDuringTheQuitOutlivesIt()
    {
        start();
        application->addPage();
        edit(0, "work");
        auto quit = [this] {
            const QVariantList rows = application->reviewClose(QStringLiteral("quit"));
            QVariantList discard;
            for (const QVariant &row : rows)
                discard << QVariantMap{{QStringLiteral("id"), row.toMap().value(QStringLiteral("id"))},
                                       {QStringLiteral("save"), false}};
            return discard;
        };
        forwarded->take({{}, QStringLiteral("before")});
        QVariantList discard = quit();
        QCOMPARE(discard.size(), 1);
        forwarded->take({{}, QStringLiteral("cancelled")});
        QVERIFY(!forwarded->startAfterQuit());
        QSignalSpy cancelled(application, &app::Application::quitCancelled);
        application->cancelClose();
        QCOMPARE(cancelled.size(), 1);
        discard = quit();
        application->resolveClose(discard);
        QVERIFY(application->quitApproved());
        QVERIFY(!forwarded->startAfterQuit());
        QVERIFY(forwarded->activationTokenAfterQuit().isEmpty());
        QVERIFY(forwarded->takeUnopened().isEmpty());
    }

    void aStartWithoutFilesBeforeTheQuitGoesAheadOutlivesIt()
    {
        start();
        application->addPage();
        edit(0, "work");
        const QVariantList rows = application->reviewClose(QStringLiteral("quit"));
        QCOMPARE(rows.size(), 1);
        forwarded->take({{}, QStringLiteral("token")});
        application->resolveClose({QVariantMap{{QStringLiteral("id"), rows.first().toMap().value(QStringLiteral("id"))},
                                               {QStringLiteral("save"), false}}});
        QVERIFY(application->quitApproved());
        QVERIFY(forwarded->startAfterQuit());
        QCOMPARE(forwarded->activationTokenAfterQuit(), QStringLiteral("token"));
        QVERIFY(forwarded->takeUnopened().isEmpty());
    }

    void theWindowComesForward()
    {
        start();
        window->setWindowStates(Qt::WindowMinimized);
        take({});
        QVERIFY(!(window->windowStates() & Qt::WindowMinimized));
        QVERIFY(window->isVisible());
        QVERIFY(!forwarded->pending());
        QVERIFY(titles().isEmpty());
        window->setWindowStates(Qt::WindowMaximized | Qt::WindowMinimized);
        take({});
        QCOMPARE(window->windowStates(), Qt::WindowStates(Qt::WindowMaximized));
    }
};

QTEST_MAIN(ForwardedLaunchesTests)
#include "forwarded_launches_tests.moc"
