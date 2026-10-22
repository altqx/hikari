// PU1 (#303): the file and session popups on the popup kit, in the main
// window. A file changed by another program is a bar on its Document
// (PU1-disk-change-bar); what a session restore could not find is a bar
// each on its tab (PU1-session-entries-bar); "Load last session?" is a card
// on the Home page (PU1-startup-cards).

#include "hikari/app/application.h"
#include "docking.h"

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString writeAss(const QString &path, const char *text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,");
        f.write(text);
        f.write("\n");
    }
    return path;
}

QStringList titles(const app::Application &a)
{
    QStringList out;
    for (const QVariant &tab : a.tabs())
        out << tab.toMap().value(QStringLiteral("title")).toString();
    return out;
}

} // namespace

class FileSessionPopupsTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    app::Application::Options options() const
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QLatin1String(QTest::currentTestFunction()) + QStringLiteral("/hikari.ini"));
        o.recoveryDir = dir.filePath(QLatin1String(QTest::currentTestFunction()) + QStringLiteral("/Recovery"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        return o;
    }
    void start(int restore = 0)
    {
        application = std::make_unique<app::Application>(options());
        application->setSessionRestore(restore);
        engine = std::make_unique<QQmlApplicationEngine>();
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    // An object by name: a QObject child, else an item of a view's delegate.
    static QQuickItem *itemNamed(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = itemNamed(child, name))
                return found;
        return nullptr;
    }
    QObject *named(const char *name) const
    {
        if (QObject *found = window->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (QQuickItem *found = itemNamed(quick->contentItem(), QLatin1String(name)))
                    return found;
        return nullptr;
    }
    bool shown(const char *name) const
    {
        QObject *o = named(name);
        return o && o->property("visible").toBool();
    }
    application::EditSession *target() const
    {
        const auto t = application->workspace().editingTarget();
        return t ? application->files().session(*t) : nullptr;
    }
    // Another program writes the file, a second later than it was read.
    void changeOnDisk(const QString &path, const char *text)
    {
        const QDateTime later = QFileInfo(path).lastModified().addSecs(2);
        writeAss(path, text);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(later, QFileDevice::FileModificationTime));
    }
    QString firstLine() const
    {
        const auto &t = target()->document().lines().front()->text;
        return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }
    void cleanup()
    {
        engine.reset();
        application.reset();
        window = nullptr;
    }

    // The window's activation (and a tab change) checks the editing
    // target's file: changed by another program, the Document gets a bar
    // with Reload, Compare and Keep mine; with unsaved work it warns that
    // Reload drops it. The bar is that Document's: another tab shows none.
    void aFileChangedOnDiskIsABarOnItsDocument()
    {
        start();
        const QString path = writeAss(dir.filePath(QStringLiteral("disk/changed.ass")), "mine");
        QVERIFY(application->openFile(path));
        QObject *bar = named("diskChangeBar");
        QVERIFY(bar);
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QVERIFY(!bar->property("visible").toBool()); // not changed
        changeOnDisk(path, "theirs");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(bar->property("visible").toBool());
        QCOMPARE(bar->property("severity").toString(), QStringLiteral("info"));
        QCOMPARE(bar->property("text").toString(), QStringLiteral("changed.ass was changed by another program."));
        QVERIFY(!bar->property("closable").toBool());
        // Unsaved work: a warning.
        application->duplicateLines();
        QTRY_COMPARE(bar->property("severity").toString(), QStringLiteral("warning"));
        QCOMPARE(bar->property("text").toString(),
                 QStringLiteral("changed.ass was changed by another program. Reloading drops the changes you made here."));
        // Another tab: no bar; back: the bar again.
        application->addPage();
        QTRY_VERIFY(!bar->property("visible").toBool());
        application->selectTab(0);
        QTRY_VERIFY(bar->property("visible").toBool());
        // Compare: the file as it is on disk, the protected reference.
        QVERIFY(QMetaObject::invokeMethod(named("diskChangeCompare"), "clicked"));
        QTRY_VERIFY(application->workspace().reference().has_value());
        QVERIFY(bar->property("visible").toBool());
        // Keep mine: the bar goes; the work stays.
        QVERIFY(QMetaObject::invokeMethod(named("reloadNo"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(target()->document().lines().size(), std::size_t(2));
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QVERIFY(!bar->property("visible").toBool()); // asked once per change
        // Changed again: Reload reads it.
        changeOnDisk(path, "theirs again");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(bar->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("reloadYes"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(firstLine(), QStringLiteral("theirs again"));
    }

    // What a restore could not find stays on its tab: a bar each, with
    // Retry, Relink… and Remove, and no ×; the window opens nothing.
    void missingSessionFilesAreBarsOnTheirTab()
    {
        start();
        const QString d = QDir::fromNativeSeparators(dir.filePath(QStringLiteral("restore")));
        writeAss(d + QStringLiteral("/here.ass"), "here");
        const QString kls = d + QStringLiteral("/partial.kls");
        {
            QFile f(kls);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("﻿[HikariSub v0.0.1]\r\nTab: 0\r\nVideo: \r\nPosition: 0\r\nFFMS2: 1\r\n"
                                   "Subtitles: %1/here.ass\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n"
                                   "Tab: 1\r\nVideo: %1/missing.mkv\r\nPosition: 0\r\nFFMS2: 1\r\n"
                                   "Subtitles: %1/gone.ass\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n")
                        .arg(d)
                        .toUtf8());
        }
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(kls));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QTRY_COMPARE(titles(*application), (QStringList{QStringLiteral("here.ass"), QStringLiteral("Untitled")}));
        QCOMPARE(application->unresolvedRestores().size(), qsizetype(2));
        QCOMPARE(application->currentTab(), 1);
        QTRY_VERIFY(shown("unresolvedBar0"));
        QTRY_VERIFY(shown("unresolvedBar1"));
        QVERIFY(!named("unresolvedBar2"));
        QCOMPARE(named("unresolvedBar0")->property("text").toString(), QStringLiteral("gone.ass, this tab’s subtitles, was not found."));
        QCOMPARE(named("unresolvedBar1")->property("text").toString(), QStringLiteral("missing.mkv, this tab’s video, was not found."));
        QVERIFY(!named("unresolvedBar0")->property("closable").toBool());
        QVERIFY(shown("restoreRetry0") && shown("restoreRelink0") && shown("restoreRemove0"));
        // The first tab has all it had: no bar.
        application->selectTab(0);
        QTRY_VERIFY(!named("unresolvedBar0"));
        application->selectTab(1);
        QTRY_VERIFY(shown("unresolvedBar1"));
        // Remove forgets the video; Retry finds the subtitles once there.
        QVERIFY(QMetaObject::invokeMethod(named("restoreRemove1"), "clicked"));
        QTRY_VERIFY(!named("unresolvedBar1"));
        QCOMPARE(application->unresolvedRestores().size(), qsizetype(1));
        writeAss(d + QStringLiteral("/gone.ass"), "found");
        QVERIFY(QMetaObject::invokeMethod(named("restoreRetry0"), "clicked"));
        QTRY_VERIFY(!named("unresolvedBar0"));
        QVERIFY(application->unresolvedRestores().isEmpty());
        QCOMPARE(titles(*application), (QStringList{QStringLiteral("here.ass"), QStringLiteral("gone.ass")}));
    }

    // On start: Ask. "Load last session?" no longer holds the start back:
    // Home shows it where the Last session row is, Restore loads it, and
    // "Restore it at every start" sets On start to Restore.
    void askingToRestoreIsACardOnHome()
    {
        {
            app::Application before(options());
            QVERIFY(before.openFile(writeAss(dir.filePath(QStringLiteral("ask/last.ass")), "l")));
        }
        start(1);
        QObject *card = named("homeOffer");
        QVERIFY(card);
        QTRY_VERIFY(card->property("visible").toBool());
        QVERIFY(!shown("homeSession"));
        QVERIFY(!shown("startupBar"));
        QVERIFY(titles(*application).isEmpty()); // nothing loaded yet
        QCOMPARE(named("homeOfferTitle")->property("text").toString(), QStringLiteral("Restore the last session?"));
        QCOMPARE(named("homeOfferDetail")->property("text").toString(), QStringLiteral("1 document · last.ass"));
        QVERIFY(shown("homeOfferRestore"));
        QVERIFY(!shown("homeOfferAutosaves"));
        QVERIFY(!shown("homeOfferDismiss"));
        QObject *always = named("homeOfferAlways");
        QVERIFY(always->property("visible").toBool());
        always->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(always, "toggled"));
        QCOMPARE(application->sessionRestore(), 2);
        QVERIFY(QMetaObject::invokeMethod(named("homeOfferRestore"), "clicked"));
        QTRY_COMPARE(titles(*application), QStringList{QStringLiteral("last.ass")});
        QTRY_VERIFY(!card->property("visible").toBool());
        QVERIFY(!shown("startupBar"));
    }

    // Started with files (On start: Ask): the offer is the startup bar over
    // the Document, and Restore replaces the tabs after the review.
    void aStartWithFilesOffersTheSessionInABar()
    {
        {
            app::Application before(options());
            QVERIFY(before.openFile(writeAss(dir.filePath(QStringLiteral("bar/last.ass")), "l")));
        }
        application = std::make_unique<app::Application>(options());
        application->setSessionRestore(1);
        application->setStartedWithPaths(true);
        QVERIFY(application->openFile(writeAss(dir.filePath(QStringLiteral("bar/given.ass")), "g")));
        engine = std::make_unique<QQmlApplicationEngine>();
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *bar = named("startupBar");
        QTRY_VERIFY(bar->property("visible").toBool());
        QCOMPARE(bar->property("severity").toString(), QStringLiteral("info"));
        QCOMPARE(bar->property("text").toString(), QStringLiteral("Restore the last session (1 document)?"));
        QVERIFY(shown("startupRestore"));
        QVERIFY(!shown("startupAutosaves"));
        QCOMPARE(titles(*application), QStringList{QStringLiteral("given.ass")});
        QVERIFY(QMetaObject::invokeMethod(named("startupRestore"), "clicked"));
        QTRY_COMPARE(titles(*application), QStringList{QStringLiteral("last.ass")});
        QTRY_VERIFY(!bar->property("visible").toBool());
    }
};

QTEST_MAIN(FileSessionPopupsTests)
#include "file_session_popups_tests.moc"
