#include "hikari/app/application.h"
#include "hikari/app/session_json.h"
#include "hikari/app/forwarded_launches.h"
#include "hikari/application/session_file.h"
#include "docking.h"

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
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

}

class ForwardedLaunchesTests : public QObject {
    Q_OBJECT

    QTemporaryDir home;
    QTemporaryDir dir;
    app::Application *application = nullptr;
    app::ForwardedLaunches *forwarded = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    void start(int restore = 0)
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        application = new app::Application(o);
        application->setSessionRestore(restore);
        forwarded = new app::ForwardedLaunches(*application);
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
        for (const char *name : {"settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(home.filePath(QLatin1String(name)));
        QFile::remove(home.filePath(QStringLiteral("last-session.json")));
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
