// PU1 (#303): the file and session popups on the popup kit, in the main
// window. A file changed by another program is a bar on its Document
// (PU1-disk-change-bar); what a session restore could not find is a bar
// each on its tab (PU1-session-entries-bar); "Load last session?" is a card
// on the Home page (PU1-startup-cards).

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
        const auto documents = application->workspace().documents().size();
        QVERIFY(QMetaObject::invokeMethod(named("diskChangeCompare"), "clicked"));
        QTRY_VERIFY(application->workspace().reference().has_value());
        QVERIFY(bar->property("visible").toBool());
        QCOMPARE(application->workspace().documents().size(), documents + 1);
        // Again: the earlier copy goes, no tab is left of it.
        const auto tabs = application->workspace().tabs().size();
        QVERIFY(QMetaObject::invokeMethod(named("diskChangeCompare"), "clicked"));
        QTRY_VERIFY(application->workspace().reference().has_value());
        QCOMPARE(application->workspace().tabs().size(), tabs);
        QCOMPARE(application->workspace().documents().size(), documents + 1);
        // Keep mine: the bar goes; the work stays. A second change made while
        // the bar showed is the one accepted: it isn't asked about again.
        changeOnDisk(path, "theirs twice");
        QVERIFY(QMetaObject::invokeMethod(named("reloadNo"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(target()->document().lines().size(), std::size_t(2));
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QVERIFY(!bar->property("visible").toBool()); // asked once per change
        // and a save writes over their change.
        QVERIFY(QMetaObject::invokeMethod(window, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        application->waitForWrites();
        QTRY_COMPARE(readAll(path).count("mine"), qsizetype(2));
        // Changed again: Reload reads it.
        changeOnDisk(path, "theirs again");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(bar->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("reloadYes"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(firstLine(), QStringLiteral("theirs again"));
        // A save while the bar shows asks first; Cancel writes nothing.
        changeOnDisk(path, "theirs third");
        QVERIFY(QMetaObject::invokeMethod(window, "checkExternalChange"));
        QTRY_VERIFY(bar->property("visible").toBool());
        application->duplicateLines(); // work to save
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
        // Save over it: the bar goes and the file is this copy.
        QVERIFY(save());
        QTRY_VERIFY(shown("diskSaveQuestion"));
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("saveOver"))));
        QTRY_VERIFY(!bar->property("visible").toBool());
        application->waitForWrites();
        QTRY_COMPARE(readAll(path).count("theirs again"), qsizetype(2));
        QVERIFY(!readAll(path).contains("theirs third"));
    }

    // Compared, then reloaded: the comparison is made again with the file as
    // it now is (the reloaded Document matches the copy from disk).
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

    // PU1-picker-titles: a picker with no folder of its own (no video, no
    // recent file) starts where its kind was last left.
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
    // Closing a Document whose file another program changed, choosing Save:
    // Save over it is asked first; Cancel cancels the close.
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

    // The file gone after its bar showed: Reload, Keep mine and Save over
    // it can't use it, and the bar stays for Save as.
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

    // A save that won't write over the file (a read-only file's question,
    // a Save As) doesn't ask about it: Save all's question leaves it out,
    // and the change on disk stays unaccepted.
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
        // Writable again, a save still asks first.
        writable.dismiss();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(QMetaObject::invokeMethod(window, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(shown("diskSaveQuestion"));
        QVERIFY(QMetaObject::invokeMethod(named("diskSaveQuestion"), "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!shown("diskSaveQuestion"));
        QVERIFY(readAll(path).contains("theirs"));
    }

    // An MKV load's Save and load over a file changed on disk (Y9): the load
    // goes on once the save is written; a write that fails cancels it, so
    // the work it was to save is not replaced.
private:
    // The MKV load's states once Save and load was chosen: 1 (listing) when
    // it went on.
    QList<int> mkvStates;
    QObject *saveOverForAnMkvLoad(const QString &path, const std::function<void()> &beforeAnswer)
    {
        start();
        QObject *flow = named("matroskaSubtitles");
        if (!flow || !application->openFile(path))
            return nullptr;
        // File > Open's MKV (none there: a load that goes on lists nothing).
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
        flow->setProperty("waitingForSave", true); // Save and load was chosen
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
        QVERIFY(flow->property("waitingForSave").toBool()); // the write is not done yet
        application->waitForWrites();
        QTRY_VERIFY(!flow->property("waitingForSave").toBool());
        QCOMPARE(readAll(path).count("mine"), qsizetype(2));
        QVERIFY(mkvStates.contains(1));
    }
    // Another tab chosen while the save is written, or work typed after it
    // started: the load is cancelled (it would act on another tab, or
    // replace what wasn't saved).
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
        application->duplicateLines(); // after the save started
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
            // The folder can't be written: the save fails.
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

    // Saved under another name, the bar about the file it came from goes.
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

    // A crash's autosaved work stays offered once the session is restored:
    // the startup bar keeps Autosaves…, without Restore.
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
        offer->setProperty("restoring", true); // Restore chosen
        QVERIFY(application->openFile(writeAss(dir.filePath(QStringLiteral("restored/kept.ass")), "kept")));
        QVERIFY(QMetaObject::invokeMethod(application.get(), "sessionRestored", Q_ARG(int, 0)));
        QTRY_VERIFY(shown("startupBar"));
        QCOMPARE(offer->property("kind").toString(), QStringLiteral("crash"));
        QVERIFY(!shown("startupRestore"));
        QVERIFY(shown("startupAutosaves"));
        // On start: Ask, restored: nothing more to offer.
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

    // The popups in the four themes, for review: written to
    // HIKARI_SURFACE_SHOT_DIR when it is set (pu1-<part>-<theme>.png).
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
            QTest::qWait(400); // laid out and painted
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
        // Home's recent files, as after some work.
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
            // Home: the crash card and the Ask card.
            QTRY_VERIFY(application->home());
            offerAs("crash");
            save("home-crash", shotOf(window, QRect(0, 0, 1280, 420)));
            offerAs("ask");
            save("home-ask", shotOf(window, QRect(0, 0, 1280, 420)));
            offer->setProperty("kind", QString());
            // A Document: the startup bar, changed on disk, a restore's missing video.
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
            // One Document with unsaved work: the message box.
            window->close();
            QTRY_VERIFY(named("closeQuestion"));
            auto *question = qobject_cast<QQuickWindow *>(named("closeQuestion"));
            QVERIFY(QTest::qWaitForWindowExposed(question));
            save("unsaved-one", shotOf(question));
            QVERIFY(QMetaObject::invokeMethod(question, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
            QTRY_VERIFY(!question->isVisible());
            // Several: the Unsaved changes window, one row Untitled.
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
            // Close all tabs.
            QVERIFY(QMetaObject::invokeMethod(named("tabCommands"), "confirmCloseAll"));
            QTRY_VERIFY(named("closeAllPrompt"));
            auto *closeAll = qobject_cast<QQuickWindow *>(named("closeAllPrompt"));
            QVERIFY(QTest::qWaitForWindowExposed(closeAll));
            save("close-all", shotOf(closeAll));
            QVERIFY(QMetaObject::invokeMethod(closeAll, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
            QTRY_VERIFY(!closeAll->isVisible());
            // The Autosaves window, with Delete older than… open.
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
            // Back to Home for the next theme.
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
