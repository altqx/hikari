#include "hikari/app/application.h"
#include "hikari/app/matroska_controller.h"
#include "docking.h"
#include "theme.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <functional>

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

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QStringList titles(const app::Application &a)
{
    QStringList out;
    for (const QVariant &tab : a.tabs())
        out << tab.toMap().value(QStringLiteral("title")).toString();
    return out;
}

}

class FileSessionPopupsTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    app::Application::Options options() const
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QLatin1String(QTest::currentTestFunction()) + QStringLiteral("/settings.toml"));
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

    void aFileChangedOnDiskIsABarOnItsDocument()
    {
        start();
        const QString path = writeAss(dir.filePath(QStringLiteral("disk/changed.ass")), "mine");
        QVERIFY(application->openFile(path));
        QObject *bar = named("diskChangeBar");
        QVERIFY(bar);
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QVERIFY(!bar->property("visible").toBool());
        changeOnDisk(path, "theirs");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(bar->property("visible").toBool());
        QCOMPARE(bar->property("severity").toString(), QStringLiteral("info"));
        QCOMPARE(bar->property("text").toString(), QStringLiteral("changed.ass was changed by another program."));
        QVERIFY(!bar->property("closable").toBool());
        application->duplicateLines();
        QTRY_COMPARE(bar->property("severity").toString(), QStringLiteral("warning"));
        QCOMPARE(bar->property("text").toString(),
                 QStringLiteral("changed.ass was changed by another program. Reloading drops the changes you made here."));
        application->addPage();
        QTRY_VERIFY(!bar->property("visible").toBool());
        application->selectTab(0);
        QTRY_VERIFY(bar->property("visible").toBool());
        const auto documents = application->workspace().documents().size();
        QVERIFY(QMetaObject::invokeMethod(named("diskChangeCompare"), "clicked"));
        QTRY_VERIFY(application->workspace().reference().has_value());
        QVERIFY(bar->property("visible").toBool());
        QCOMPARE(application->workspace().documents().size(), documents + 1);
        const auto tabs = application->workspace().tabs().size();
        QVERIFY(QMetaObject::invokeMethod(named("diskChangeCompare"), "clicked"));
        QTRY_VERIFY(application->workspace().reference().has_value());
        QCOMPARE(application->workspace().tabs().size(), tabs);
        QCOMPARE(application->workspace().documents().size(), documents + 1);
        changeOnDisk(path, "theirs twice");
        QVERIFY(QMetaObject::invokeMethod(named("reloadNo"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(target()->document().lines().size(), std::size_t(2));
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QVERIFY(!bar->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        application->waitForWrites();
        QTRY_COMPARE(readAll(path).count("mine"), qsizetype(2));
        changeOnDisk(path, "theirs again");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(bar->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("reloadYes"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(firstLine(), QStringLiteral("theirs again"));
        changeOnDisk(path, "theirs third");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(bar->property("visible").toBool());
        application->duplicateLines();
        const auto save = [this] {
            return QMetaObject::invokeMethod(window, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
        };
        QVERIFY(save());
        QTRY_VERIFY(shown("diskSaveQuestion"));
        QCOMPARE(named("diskSaveQuestion")->property("text").toString(), QStringLiteral("“changed.ass” was changed by another program."));
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!shown("diskSaveQuestion"));
        application->waitForWrites();
        QVERIFY(readAll(path).contains("theirs third"));
        QVERIFY(bar->property("visible").toBool());
        QVERIFY(save());
        QTRY_VERIFY(shown("diskSaveQuestion"));
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("saveOver"))));
        QTRY_VERIFY(!bar->property("visible").toBool());
        application->waitForWrites();
        QTRY_COMPARE(readAll(path).count("theirs again"), qsizetype(2));
        QVERIFY(!readAll(path).contains("theirs third"));
    }

    void aReloadComparesAgain()
    {
        start();
        const QString path = writeAss(dir.filePath(QStringLiteral("disk/compared.ass")), "mine");
        QVERIFY(application->openFile(path));
        const auto document = *application->workspace().editingTarget();
        changeOnDisk(path, "theirs");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(shown("diskChangeBar"));
        QVERIFY(QMetaObject::invokeMethod(named("diskChangeCompare"), "clicked"));
        QTRY_VERIFY(application->comparison().active());
        const auto differs = [&] {
            const auto *table = application->comparison().table(document);
            return table && std::ranges::any_of(*table, [](const auto &line) { return !line.match(); });
        };
        QVERIFY(differs());
        QVERIFY(QMetaObject::invokeMethod(named("reloadYes"), "clicked"));
        QTRY_VERIFY(!shown("diskChangeBar"));
        QCOMPARE(firstLine(), QStringLiteral("theirs"));
        QVERIFY(application->comparison().table(document));
        QVERIFY(!differs());
    }

    void pickersStartWhereTheirKindWasLeft()
    {
        start();
        const QString keyframes = writeAss(dir.filePath(QStringLiteral("frames/keys.txt")), "x");
        QObject *dialog = named("keyframesDialog");
        QVERIFY(dialog);
        dialog->setProperty("selectedFile", QUrl::fromLocalFile(keyframes));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
        const QStringList rows = application->settingsStore()->value(QStringLiteral("workspace.pickerFolders")).toStringList();
        QVERIFY2(rows.contains(QStringLiteral("keyframes\t") + QUrl::fromLocalFile(dir.filePath(QStringLiteral("frames"))).toString()),
                 qPrintable(rows.join(u'|')));
        application->settingsStore()->setValue(QStringLiteral("workspace.pickerFolders"),
                                         QStringList{QStringLiteral("video\t") + QUrl::fromLocalFile(dir.path()).toString()});
        QVERIFY(QMetaObject::invokeMethod(named("videoOpenDialog"), "show"));
        QCOMPARE(named("videoOpenDialog")->property("currentFolder").toUrl(), QUrl::fromLocalFile(dir.path()));
        QVERIFY(QMetaObject::invokeMethod(named("videoOpenDialog"), "close"));
    }

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
        application->selectTab(0);
        QTRY_VERIFY(!named("unresolvedBar0"));
        application->selectTab(1);
        QTRY_VERIFY(shown("unresolvedBar1"));
        QVERIFY(QMetaObject::invokeMethod(named("restoreRemove1"), "clicked"));
        QTRY_VERIFY(!named("unresolvedBar1"));
        QCOMPARE(application->unresolvedRestores().size(), qsizetype(1));
        writeAss(d + QStringLiteral("/gone.ass"), "found");
        QVERIFY(QMetaObject::invokeMethod(named("restoreRetry0"), "clicked"));
        QTRY_VERIFY(!named("unresolvedBar0"));
        QVERIFY(application->unresolvedRestores().isEmpty());
        QCOMPARE(titles(*application), (QStringList{QStringLiteral("here.ass"), QStringLiteral("gone.ass")}));
    }

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
        QVERIFY(titles(*application).isEmpty());
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

    void closingAFileChangedOnDiskAsksToSaveOverIt()
    {
        start();
        const QString path = writeAss(dir.filePath(QStringLiteral("disk/closing.ass")), "mine");
        QVERIFY(application->openFile(path));
        changeOnDisk(path, "theirs");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(shown("diskChangeBar"));
        application->duplicateLines();
        const auto closeAndSave = [this] {
            window->close();
            QTRY_VERIFY(shown("closeQuestion"));
            QVERIFY(QMetaObject::invokeMethod(named("closeQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("save"))));
            QTRY_VERIFY(shown("diskSaveQuestion"));
        };
        closeAndSave();
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!shown("diskSaveQuestion"));
        QVERIFY(!application->quitApproved());
        QVERIFY(window->isVisible());
        QVERIFY(readAll(path).contains("theirs"));
        QVERIFY(shown("diskChangeBar"));
        closeAndSave();
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("saveOver"))));
        QTRY_VERIFY(application->quitApproved());
        application->waitForWrites();
        QCOMPARE(readAll(path).count("mine"), qsizetype(2));
    }

    void aFileGoneAfterItsBarKeepsTheBar()
    {
        start();
        const QString path = writeAss(dir.filePath(QStringLiteral("disk/gone.ass")), "mine");
        QVERIFY(application->openFile(path));
        changeOnDisk(path, "theirs");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(shown("diskChangeBar"));
        application->duplicateLines();
        QVERIFY(QFile::remove(path));
        QVERIFY(QMetaObject::invokeMethod(named("reloadYes"), "clicked"));
        QTest::qWait(200);
        QVERIFY(shown("diskChangeBar"));
        QCOMPARE(target()->document().lines().size(), std::size_t(2));
        QVERIFY(QMetaObject::invokeMethod(named("reloadNo"), "clicked"));
        QTest::qWait(200);
        QVERIFY(shown("diskChangeBar"));
        QVERIFY(QMetaObject::invokeMethod(window, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(shown("diskSaveQuestion"));
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("saveOver"))));
        QTRY_VERIFY(!shown("diskSaveQuestion"));
        application->waitForWrites();
        QVERIFY(shown("diskChangeBar"));
        QVERIFY(!QFileInfo::exists(path));
    }

    void aSaveAllThatDoesNotWriteTheFileLeavesItsBar()
    {
        start();
        const QString path = writeAss(dir.filePath(QStringLiteral("disk/locked.ass")), "mine");
        QVERIFY(application->openFile(path));
        changeOnDisk(path, "theirs");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(shown("diskChangeBar"));
        application->duplicateLines();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser);
        auto writable = qScopeGuard([&] { QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner); });
        if (QFileInfo(path).isWritable()) // root (CI containers) writes read-only files
            QSKIP("Read-only files are writable for this user");
        const qulonglong id = application->workspace().editingTarget()->value;
        QCOMPARE(application->saveRouteFor(id), QStringLiteral("readonly"));
        QVERIFY(QMetaObject::invokeMethod(window, "saveAllTabs", Q_ARG(QVariant, QVariant())));
        QObject *messages = named("messages");
        QVERIFY(messages);
        QQuickWindow *box = nullptr;
        QTRY_VERIFY((box = qvariant_cast<QQuickWindow *>(messages->property("box"))) && box->isVisible());
        QVERIFY(!shown("diskSaveQuestion"));
        QCOMPARE(box->property("text").toString(), QStringLiteral("“locked.ass” is read-only."));
        QVERIFY(QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!box->isVisible());
        QVERIFY(shown("diskChangeBar"));
        writable.dismiss();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(QMetaObject::invokeMethod(window, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(shown("diskSaveQuestion"));
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!shown("diskSaveQuestion"));
        QVERIFY(readAll(path).contains("theirs"));
    }

private:
    QList<int> mkvStates;
    QObject *saveOverForAnMkvLoad(const QString &path, const std::function<void()> &beforeAnswer)
    {
        start();
        QObject *flow = named("matroskaSubtitles");
        if (!flow || !application->openFile(path))
            return nullptr;
        if (!application->matroska().beginFile(dir.filePath(QStringLiteral("none.mkv"))))
            return nullptr;
        mkvStates.clear();
        connect(&application->matroska(), &app::MatroskaController::stateChanged, this,
                [this] { mkvStates << application->matroska().state(); });
        changeOnDisk(path, "theirs");
        QMetaObject::invokeMethod(window, "checkExternalChange");
        if (!QTest::qWaitFor([&] { return shown("diskChangeBar"); }))
            return nullptr;
        application->duplicateLines();
        flow->setProperty("waitingForSave", true);
        QMetaObject::invokeMethod(window, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
        if (!QTest::qWaitFor([&] { return shown("diskSaveQuestion"); }))
            return nullptr;
        beforeAnswer();
        QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("saveOver")));
        return flow;
    }

private slots:
    void anMkvLoadGoesOnOnceTheSaveOverIsWritten()
    {
        const QString path = writeAss(dir.filePath(QStringLiteral("mkvwait/written.ass")), "mine");
        QObject *flow = saveOverForAnMkvLoad(path, [] {});
        QVERIFY(flow);
        QVERIFY(flow->property("waitingForSave").toBool());
        application->waitForWrites();
        QTRY_VERIFY(!flow->property("waitingForSave").toBool());
        QCOMPARE(readAll(path).count("mine"), qsizetype(2));
        QVERIFY(mkvStates.contains(1));
    }
    void anMkvLoadIsCancelledWhenAnotherTabIsChosenDuringTheSave()
    {
        const QString path = writeAss(dir.filePath(QStringLiteral("mkvwait/switched.ass")), "mine");
        QObject *flow = saveOverForAnMkvLoad(path, [] {});
        QVERIFY(flow);
        application->addPage();
        application->waitForWrites();
        QTRY_VERIFY(!flow->property("waitingForSave").toBool());
        QVERIFY(!mkvStates.contains(1));
    }
    void anMkvLoadIsCancelledByWorkTheSaveDidNotCover()
    {
        const QString path = writeAss(dir.filePath(QStringLiteral("mkvwait/newer.ass")), "mine");
        QObject *flow = saveOverForAnMkvLoad(path, [] {});
        QVERIFY(flow);
        application->duplicateLines();
        application->waitForWrites();
        QTRY_VERIFY(!flow->property("waitingForSave").toBool());
        QVERIFY(!mkvStates.contains(1));
        QVERIFY(target()->isDirty());
    }
    void anMkvLoadIsCancelledWhenTheSaveOverFails()
    {
        const QString locked = dir.filePath(QStringLiteral("mkvlocked"));
        const QString path = writeAss(locked + QStringLiteral("/failing.ass"), "mine");
        auto writable = qScopeGuard([&] {
            QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        });
        bool rootWrites = false;
        QObject *flow = saveOverForAnMkvLoad(path, [&] {
            QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
            rootWrites = QFileInfo(locked).isWritable(); // root (CI containers) writes read-only folders
        });
        QVERIFY(flow);
        if (rootWrites)
            QSKIP("Read-only folders are writable for this user");
        QVERIFY(flow->property("waitingForSave").toBool());
        application->waitForWrites();
        QTRY_VERIFY(!flow->property("waitingForSave").toBool());
        QCOMPARE(flow->property("writes").toList().size(), 0);
        QVERIFY(!mkvStates.contains(1));
        QVERIFY(target()->isDirty());
        QVERIFY(readAll(path).contains("theirs"));
    }

    void saveAsEndsTheDiskChangeBar()
    {
        start();
        const QString path = writeAss(dir.filePath(QStringLiteral("disk/saveas.ass")), "mine");
        QVERIFY(application->openFile(path));
        changeOnDisk(path, "theirs");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(shown("diskChangeBar"));
        QObject *dialog = named("saveAsDialog");
        QVERIFY(dialog);
        const QString other = dir.filePath(QStringLiteral("disk/saveas-mine.ass"));
        dialog->setProperty("document", 0);
        dialog->setProperty("selectedFile", QUrl::fromLocalFile(other));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
        QTRY_VERIFY(!shown("diskChangeBar"));
        application->waitForWrites();
        QVERIFY(readAll(other).contains("mine"));
        QVERIFY(readAll(path).contains("theirs"));
    }

    void aRestoredSessionKeepsTheAutosavesOffered()
    {
        start();
        QObject *offer = named("startupBar")->property("offer").value<QObject *>();
        QVERIFY(offer);
        QTRY_VERIFY(application->home());
        offer->setProperty("names", QStringList{QStringLiteral("kept.ass")});
        offer->setProperty("autosaveCount", 1);
        offer->setProperty("restorable", true);
        offer->setProperty("kind", QStringLiteral("crash"));
        offer->setProperty("restoring", true);
        QVERIFY(application->openFile(writeAss(dir.filePath(QStringLiteral("restored/kept.ass")), "kept")));
        QVERIFY(QMetaObject::invokeMethod(application.get(), "sessionRestored", Q_ARG(int, 0)));
        QTRY_VERIFY(shown("startupBar"));
        QCOMPARE(offer->property("kind").toString(), QStringLiteral("crash"));
        QVERIFY(!shown("startupRestore"));
        QVERIFY(shown("startupAutosaves"));
        offer->setProperty("restorable", true);
        offer->setProperty("kind", QStringLiteral("ask"));
        QVERIFY(QMetaObject::invokeMethod(application.get(), "sessionRestored", Q_ARG(int, 0)));
        QTRY_VERIFY(!shown("startupBar"));
    }

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

    void screenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        window->resize(1280, 760);
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        QObject *offer = named("startupBar")->property("offer").value<QObject *>();
        QVERIFY(offer);
        const auto shotOf = [](QQuickWindow *w, const QRect &part = {}) {
            QTest::qWait(400);
            const QImage image = w->grabWindow();
            return part.isNull() ? image : image.copy(part & QRect(QPoint(), image.size()));
        };
        const auto offerAs = [&](const char *kind) {
            offer->setProperty("names", QStringList{QStringLiteral("Episode 03.ass"), QStringLiteral("Signs.ass")});
            offer->setProperty("autosaveCount", 1);
            offer->setProperty("restorable", true);
            offer->setProperty("kind", QLatin1String(kind));
        };
        const QString d = QDir::fromNativeSeparators(dir.filePath(QStringLiteral("shots")));
        QVERIFY(application->openFile(writeAss(d + QStringLiteral("/Signs.ass"), "mine")));
        QVERIFY(application->openFile(writeAss(d + QStringLiteral("/Episode 03.ass"), "e")));
        QVERIFY(application->reviewClose(QStringLiteral("all")).isEmpty());
        application->finishClose();
        QTRY_VERIFY(application->home());
        for (const auto code : ui::theme::kCodes) {
            const QString theme = ui::theme::codeName(code);
            ui::theme::preview({{QStringLiteral("appearance.theme"), theme}, {QStringLiteral("appearance.followSystem"), false}});
            const auto save = [&](const char *part, const QImage &image) {
                QVERIFY(image.save(out + QStringLiteral("/pu1-") + QLatin1String(part) + QLatin1Char('-') + theme + QStringLiteral(".png")));
            };
            QTRY_VERIFY(application->home());
            offerAs("crash");
            save("home-crash", shotOf(window, QRect(0, 0, 1280, 420)));
            offerAs("ask");
            save("home-ask", shotOf(window, QRect(0, 0, 1280, 420)));
            offer->setProperty("kind", QString());
            const QString path = writeAss(d + QStringLiteral("/Signs.ass"), "mine");
            QVERIFY(application->openFile(path));
            offerAs("crash");
            offer->setProperty("keepWithDocuments", true);
            changeOnDisk(path, "theirs");
            QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
            application->duplicateLines();
            QTRY_VERIFY(shown("diskChangeBar") && shown("startupBar"));
            save("bars", shotOf(window, QRect(0, 0, 1280, 260)));
            offer->setProperty("kind", QString());
            QVERIFY(QMetaObject::invokeMethod(named("reloadNo"), "clicked"));
            window->close();
            QTRY_VERIFY(named("closeQuestion"));
            auto *question = qobject_cast<QQuickWindow *>(named("closeQuestion"));
            QVERIFY(QTest::qWaitForWindowExposed(question));
            save("unsaved-one", shotOf(question));
            QVERIFY(QMetaObject::invokeMethod(question, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
            QTRY_VERIFY(!question->isVisible());
            QVERIFY(application->openFile(writeAss(d + QStringLiteral("/Episode 03.ass"), "e")));
            application->selectTab(1);
            application->duplicateLines();
            application->addPage();
            application->duplicateLines();
            window->close();
            QTRY_VERIFY(named("closeReview"));
            auto *review = qobject_cast<QQuickWindow *>(named("closeReview"));
            QVERIFY(QTest::qWaitForWindowExposed(review));
            QVERIFY(QMetaObject::invokeMethod(named("closeDiscard1"), "clicked"));
            save("unsaved-window", shotOf(review));
            QVERIFY(QMetaObject::invokeMethod(named("closeCancel"), "clicked"));
            QTRY_VERIFY(!review->isVisible());
            QVERIFY(QMetaObject::invokeMethod(named("tabCommands"), "confirmCloseAll"));
            QTRY_VERIFY(named("closeAllPrompt"));
            auto *closeAll = qobject_cast<QQuickWindow *>(named("closeAllPrompt"));
            QVERIFY(QTest::qWaitForWindowExposed(closeAll));
            save("close-all", shotOf(closeAll));
            QVERIFY(QMetaObject::invokeMethod(closeAll, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
            QTRY_VERIFY(!closeAll->isVisible());
            QObject *autosaves = named("autosaves");
            QVERIFY(QMetaObject::invokeMethod(autosaves, "show", Q_ARG(QVariant, false)));
            const QDateTime now = QDateTime::currentDateTime();
            const auto row = [](const char *kind, const char *name, const QString &path, const QDateTime &when, int versions) {
                QVariantList list;
                for (int i = 0; i < versions; ++i)
                    list << QVariantMap{{QStringLiteral("label"), when.addSecs(-600 * i).toString(QStringLiteral("HH:mm"))}};
                return QVariantMap{{QStringLiteral("kind"), QLatin1String(kind)}, {QStringLiteral("key"), QLatin1String(name)},
                                   {QStringLiteral("name"), QLatin1String(name)}, {QStringLiteral("path"), path},
                                   {QStringLiteral("when"), when}, {QStringLiteral("versions"), list}};
            };
            autosaves->setProperty("entries", QVariantList{
                row("bundle", "Episode 03.ass", QStringLiteral("~/subs/show/Episode 03.ass"), now.addSecs(-1200), 3),
                row("bundle", "Untitled", QString(), now.addDays(-1), 1),
                row("bundle", "Signs.ass", QStringLiteral("~/subs/show/Signs.ass"), now.addDays(-12), 2),
                row("legacy", "Episode 02.ass", QString(), now.addDays(-40), 4)});
            QTRY_VERIFY(named("autosavesWindow"));
            auto *autosavesWindow = qobject_cast<QQuickWindow *>(named("autosavesWindow"));
            QVERIFY(QTest::qWaitForWindowExposed(autosavesWindow));
            save("autosaves", shotOf(autosavesWindow));
            QVERIFY(QMetaObject::invokeMethod(named("autosavesDeleteOlder"), "clicked"));
            QTRY_VERIFY(named("autosavesDeleteOlderPopover")->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(named("olderDate"), "setDate", Q_ARG(QVariant, now.addDays(-7))));
            save("autosaves-delete-older", shotOf(autosavesWindow));
            QVERIFY(QMetaObject::invokeMethod(autosavesWindow, "reject"));
            QTRY_VERIFY(!autosavesWindow->isVisible());
            QVariantList discard;
            for (const QVariant &r : application->reviewClose(QStringLiteral("all")))
                discard << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))},
                                       {QStringLiteral("save"), false},
                                       {QStringLiteral("path"), QString()}};
            if (discard.isEmpty())
                application->finishClose();
            else
                application->resolveClose(discard);
            QTRY_VERIFY(application->home());
        }
    }
};

QTEST_MAIN(FileSessionPopupsTests)
#include "file_session_popups_tests.moc"
