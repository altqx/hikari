// P1: close review through the composition (accepted L58-write-close): a
// Save-dependent close proceeds only after an acknowledged write; Discard
// covers only the work it was chosen for; Cancel keeps everything; one quit
// review covers every Document; New leaves an Untitled Document that saves
// through Save As.

#include "hikari/app/application.h"
#include "docking.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString writeFile(const QTemporaryDir &dir, const char *name, const char *text)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,");
    f.write(text);
    f.write("\n");
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QVariantList choose(const QVariantList &rows, bool save, const QString &path = {})
{
    QVariantList out;
    for (const QVariant &r : rows)
        out << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))},
                           {QStringLiteral("save"), save},
                           {QStringLiteral("path"), path}};
    return out;
}

} // namespace

class CloseReviewTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    application::EditSession *target(app::Application &a)
    {
        const auto t = a.workspace().editingTarget();
        return t ? a.files().session(*t) : nullptr;
    }
    void edit(app::Application &a, const char8_t *text)
    {
        auto *s = target(a);
        const auto line = s->document().lines().front()->id;
        QVERIFY(s->run(application::Command{"Set text", s->revision(), {line},
                                            [&](core::Document &d) { return d.setLineText(line, text); }}));
    }

    // SH2-last-tab-home: closing the last tab leaves no Document, and the
    // Home page shows (legacy Notebook::DeletePage made a new Untitled tab).
    bool closedToHome(app::Application &a)
    {
        return !a.workspace().editingTarget() && a.workspace().documents().empty() && a.tabs().isEmpty();
    }

    // An object by name: a QObject child, else an item (a view's rows).
    static QQuickItem *itemNamed(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = itemNamed(child, name))
                return found;
        return nullptr;
    }
    static QObject *find(QQuickWindow *window, const char *name)
    {
        if (QObject *found = window->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (QQuickItem *found = itemNamed(quick->contentItem(), QLatin1String(name)))
                    return found;
        return nullptr;
    }

private slots:
    void aCleanDocumentClosesWithoutReview()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "clean.ass", "x")));
        QVERIFY(a.reviewClose(QStringLiteral("close")).isEmpty());
        a.finishClose();
        QVERIFY(closedToHome(a));
    }

    void discardClosesWithoutWriting()
    {
        app::Application a;
        const QString path = writeFile(dir, "discard.ass", "x");
        const QByteArray before = readAll(path);
        QVERIFY(a.openFile(path));
        edit(a, u8"changed");
        const auto rows = a.reviewClose(QStringLiteral("close"));
        QCOMPARE(rows.size(), qsizetype(1));
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(choose(rows, false));
        QCOMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QVERIFY(closedToHome(a));
        QCOMPARE(readAll(path), before);
    }

    void saveClosesOnlyAfterTheWriteIsAcknowledged()
    {
        app::Application a;
        const QString path = writeFile(dir, "save.ass", "x");
        QVERIFY(a.openFile(path));
        edit(a, u8"saved");
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(choose(a.reviewClose(QStringLiteral("close")), true));
        QVERIFY(a.workspace().editingTarget()); // still open while the write runs
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QVERIFY(closedToHome(a));
        QVERIFY(readAll(path).contains(",,saved"));
    }

    // PU1-unsaved-window: one Document's question closes at its answer, so
    // another tab can be chosen while the save is written: the reviewed
    // tab closes, the chosen one stays.
    void theReviewedTabClosesThoughAnotherWasChosenMeanwhile()
    {
        app::Application a;
        const QString path = writeFile(dir, "reviewed.ass", "x");
        QVERIFY(a.openFile(path));
        const auto reviewed = *a.workspace().editingTarget();
        edit(a, u8"saved");
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(choose(a.reviewClose(QStringLiteral("close")), true));
        a.addPage();
        const auto other = *a.workspace().editingTarget();
        QVERIFY(other != reviewed);
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QVERIFY(!a.files().session(reviewed));
        QVERIFY(a.files().session(other));
        QCOMPARE(a.workspace().editingTarget(), std::optional(other));
        QVERIFY(readAll(path).contains(",,saved"));
    }

    // KA3-close-review: the Kanji timer's done pairs not applied are unsaved
    // work of their Document (Codex review on #352): its close asks though
    // the Document is unchanged; Don't save drops them, Save writes them
    // into the file first.
    void kanjiTimerPairsNotAppliedAreReviewed()
    {
        const QString path = dir.filePath(QStringLiteral("kanji.ass"));
        const auto write = [&] {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[V4+ Styles]\n"
                    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                    "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                    "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Song,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                    "10,10,10,1\n"
                    "Style: Song kanji,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,"
                    "2,10,10,10,1\n\n"
                    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Song,,0,0,0,,{\\k50}yu{\\k50}me\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Song kanji,,0,0,0,,\xE5\xA4\xA2\n"); // 夢
        };
        write();
        const QByteArray before = readAll(path);
        app::Application a;
        auto &timer = a.kanjiTimer();
        QVERIFY(a.openFile(path));
        timer.open();
        QCOMPARE(timer.acceptAll(), 1);
        timer.close(); // (the window asks itself; another Document's pairs wait unasked)
        QVERIFY(!target(a)->isDirty());
        auto rows = a.reviewClose(QStringLiteral("close"));
        QCOMPARE(rows.size(), qsizetype(1));
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(choose(rows, false));
        QCOMPARE(finished.size(), 1);
        QVERIFY(closedToHome(a));
        QCOMPARE(readAll(path), before);
        QCOMPARE(timer.pending(), 0);

        // Save: the kanji Line written, then the file.
        QVERIFY(a.openFile(path));
        timer.open();
        QCOMPARE(timer.acceptAll(), 1);
        timer.close();
        rows = a.reviewClose(QStringLiteral("quit"));
        QCOMPARE(rows.size(), qsizetype(1));
        a.resolveClose(choose(rows, true));
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(finished.last().at(0).toBool());
        QVERIFY(readAll(path).contains(",,{\\k100}\xE5\xA4\xA2"));
    }

    // A Document's pairs kept while another was the target are read again
    // before Save writes them: a kanji Line edited since is not written over
    // (Codex review on #352). Suggest groupings changed on the other
    // Document holds for this one's timer too.
    void kanjiTimerPairsKeptForAnotherDocumentAreReadAgain()
    {
        const QString path = dir.filePath(QStringLiteral("kept.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[V4+ Styles]\n"
                    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                    "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                    "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Song,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                    "10,10,10,1\n"
                    "Style: Song kanji,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,"
                    "2,10,10,10,1\n\n"
                    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Song,,0,0,0,,{\\k50}yu{\\k50}me\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Song kanji,,0,0,0,,\xE5\xA4\xA2\n"         // 夢
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Song,,0,0,0,,{\\k50}yu{\\k50}me\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Song kanji,,0,0,0,,\xE5\xA4\xA2\n");
        }
        app::Application a;
        auto &timer = a.kanjiTimer();
        const bool suggest = timer.suggest();
        timer.setSuggest(true);
        QVERIFY(a.openFile(path));
        const int kanjiTab = a.currentTab();
        timer.open();
        QCOMPARE(timer.acceptAll(), 2);
        QCOMPARE(timer.pending(), 2);
        // another Document as the target, Suggest groupings off there
        QVERIFY(a.openFile(writeFile(dir, "kept-other.ass", "x")));
        a.selectTab(static_cast<int>(a.workspace().tabs().size()) - 1);
        QVERIFY(a.currentTab() != kanjiTab);
        timer.open();
        timer.setSuggest(false);
        timer.close();
        // back, the window closed: the second kanji Line edited
        a.selectTab(kanjiTab);
        auto *s = target(a);
        const auto lines = s->document().lines();
        const auto edited = lines[3]->id;
        QVERIFY(s->run(application::Command{"Set text", s->revision(), {edited},
                                            [&](core::Document &d) { return d.setLineText(edited, u8"edited"); }}));
        const auto rows = a.reviewClose(QStringLiteral("close"));
        QCOMPARE(rows.size(), qsizetype(1));
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(choose(rows, true));
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(0).toBool());
        const QByteArray saved = readAll(path);
        QVERIFY(saved.contains(",,{\\k100}\xE5\xA4\xA2")); // the first, unchanged
        QVERIFY(saved.contains(",,edited"));                // the second, not written over
        QCOMPARE(saved.count("{\\k100}"), qsizetype(1));

        // the setting holds on a kept timer when it is the window's again
        QVERIFY(a.openFile(path));
        const int again = static_cast<int>(a.workspace().tabs().size()) - 1; // (opened in a tab of its own)
        a.selectTab(again);
        timer.open();
        QVERIFY(a.openFile(writeFile(dir, "kept-other2.ass", "x")));
        a.selectTab(static_cast<int>(a.workspace().tabs().size()) - 1);
        timer.open();
        timer.setSuggest(true);
        a.selectTab(again);
        timer.open();
        QVERIFY(!timer.pairs().isEmpty());
        QCOMPARE(timer.pairs().first().toMap().value(QStringLiteral("status")).toString(), QStringLiteral("suggested"));
        timer.setSuggest(false);
        timer.close();
        timer.setSuggest(suggest);
    }

    // KA8 (#354): Multi-highlight (#) turned on while another Document is the
    // target holds when a kept Document's pairs are saved at quit.
    void kanjiTimerPairsKeptAreSavedWithMultiHighlightAsItIsNow()
    {
        const QString path = dir.filePath(QStringLiteral("kept-multi.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[V4+ Styles]\n"
                    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                    "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                    "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Song,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                    "10,10,10,1\n"
                    "Style: Song kanji,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,"
                    "2,10,10,10,1\n\n"
                    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:01.60,Song,,0,0,0,,{\\k20}ge{\\k10}ki{\\k30}no\n"
                    "Dialogue: 0,0:00:01.00,0:00:01.60,Song kanji,,0,0,0,,\xE6\x92\x83\xE3\x81\xAE\n"); // 撃の
        }
        app::Application a;
        auto &timer = a.kanjiTimer();
        const bool multi = timer.multi();
        timer.setMulti(false);
        QVERIFY(a.openFile(path));
        timer.open();
        QCOMPARE(timer.acceptAll(), 1);
        timer.close();
        // another Document as the target, Multi-highlight turned on there
        QVERIFY(a.openFile(writeFile(dir, "kept-multi-other.ass", "x")));
        a.selectTab(static_cast<int>(a.workspace().tabs().size()) - 1);
        timer.open();
        timer.setMulti(true);
        timer.close();
        const auto rows = a.reviewClose(QStringLiteral("quit"));
        QCOMPARE(rows.size(), qsizetype(1));
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(choose(rows, true));
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QVERIFY(readAll(path).contains(",,{\\k20}\xE6\x92\x83{\\k10}#{\\k30}\xE3\x81\xAE")); // 撃 ge, # ki, の no
        timer.setMulti(multi);
    }

    void newerEditsAfterTheChoiceKeepTheDocumentOpen()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "newer.ass", "x")));
        edit(a, u8"first");
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(choose(a.reviewClose(QStringLiteral("close")), true));
        edit(a, u8"newer"); // after Save was authorized
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(a.workspace().editingTarget());
        QVERIFY(target(a)->isDirty());
    }

    void aFailedWriteKeepsTheDocumentOpen()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "fail.ass", "x")));
        a.closeEditingTarget();
        a.reviewClose(QStringLiteral("new"));
        a.finishClose(); // an Untitled Document
        edit(a, u8"unsaved");
        QSignalSpy finished(&a, &app::Application::closeFinished);
        const QString missing = dir.filePath(QStringLiteral("no-such-dir/out.ass"));
        a.resolveClose(choose(a.reviewClose(QStringLiteral("close")), true, missing));
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(a.workspace().editingTarget());
    }

    // A save that failed shows the review again: its retry closes the
    // reviewed tab, not one chosen meanwhile.
    void aRetryAfterAFailedSaveClosesTheReviewedTab()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "retry.ass", "x")));
        a.closeEditingTarget();
        a.reviewClose(QStringLiteral("new"));
        a.finishClose(); // an Untitled Document
        edit(a, u8"unsaved");
        const auto reviewed = *a.workspace().editingTarget();
        QSignalSpy finished(&a, &app::Application::closeFinished);
        const QVariantList rows = a.reviewClose(QStringLiteral("close"));
        a.resolveClose(choose(rows, true, dir.filePath(QStringLiteral("no-such-dir/retry.ass"))));
        a.addPage();
        const auto other = *a.workspace().editingTarget();
        edit(a, u8"other work");
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        const QString good = dir.filePath(QStringLiteral("retried.ass"));
        a.resolveClose(choose(rows, true, good));
        a.waitForWrites();
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(finished.at(1).at(0).toBool());
        QVERIFY(!a.files().session(reviewed));
        QVERIFY(a.files().session(other));
        QVERIFY(target(a)->isDirty());
        QVERIFY(readAll(good).contains(",,unsaved"));
    }

    // Two Untitled rows: the second's picker doesn't start on the file
    // picked for the first (both would save to it).
    void eachUntitledRowStartsWithoutTheFileBefore()
    {
        app::Application a;
        a.addPage();
        edit(a, u8"first work");
        a.addPage();
        edit(a, u8"second work");
        QQmlApplicationEngine engine;
        hikari::ui::attachDocking(engine);
        engine.setInitialProperties(a.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->close();
        QTRY_VERIFY(window->findChild<QQuickWindow *>(QStringLiteral("closeReview")));
        const auto named = [window](const char *name) { return find(window, name); };
        QTRY_VERIFY(named("closeSaveAndClose"));
        QVERIFY(QMetaObject::invokeMethod(named("closeSaveAndClose"), "clicked"));
        QObject *picker = named("closeSaveAs");
        QTRY_VERIFY(picker->property("visible").toBool());
        const QUrl first = QUrl::fromLocalFile(dir.filePath(QStringLiteral("first.ass")));
        picker->setProperty("selectedFile", first);
        QVERIFY(QMetaObject::invokeMethod(picker, "accepted"));
        QTRY_VERIFY(picker->property("visible").toBool()); // the second row's turn
        QVERIFY(picker->property("selectedFile").toUrl() != first);
        picker->setProperty("selectedFile", QUrl::fromLocalFile(dir.filePath(QStringLiteral("second.ass"))));
        QVERIFY(QMetaObject::invokeMethod(picker, "accepted"));
        QMetaObject::invokeMethod(picker, "close");
        QTRY_VERIFY(a.quitApproved());
        a.waitForWrites();
        QVERIFY(readAll(dir.filePath(QStringLiteral("first.ass"))).contains("first work"));
        QVERIFY(readAll(dir.filePath(QStringLiteral("second.ass"))).contains("second work"));
    }

    void cancelKeepsEverything()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "cancel.ass", "x")));
        edit(a, u8"kept");
        QVERIFY(!a.reviewClose(QStringLiteral("close")).isEmpty());
        a.cancelClose();
        QVERIFY(a.workspace().editingTarget());
        QVERIFY(target(a)->isDirty());
    }

    void oneQuitReviewCoversEveryDocument()
    {
        app::Application a;
        const QString one = writeFile(dir, "quit1.ass", "x"), two = writeFile(dir, "quit2.ass", "y");
        QVERIFY(a.openFile(one));
        edit(a, u8"one!");
        QVERIFY(a.openFile(two)); // a second Document; the first stays the editing target
        const auto second = a.workspace().documents().back();
        auto *s2 = a.files().session(second);
        const auto line = s2->document().lines().front()->id;
        QVERIFY(s2->run(application::Command{"Set text", s2->revision(), {line},
                                             [&](core::Document &d) { return d.setLineText(line, u8"two!"); }}));
        const auto rows = a.reviewClose(QStringLiteral("quit"));
        QCOMPARE(rows.size(), qsizetype(2));
        QSignalSpy approved(&a, &app::Application::quitApprovedChanged);
        a.resolveClose(choose(rows, true));
        a.waitForWrites();
        QTRY_VERIFY(a.quitApproved());
        QVERIFY(readAll(one).contains(",,one!"));
        QVERIFY(readAll(two).contains(",,two!"));
    }

    void newGivesAnUntitledDocumentThatSavesAs()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "before-new.ass", "x")));
        QVERIFY(a.reviewClose(QStringLiteral("new")).isEmpty());
        a.finishClose();
        QVERIFY(a.targetUntitled());
        QCOMPARE(QString::fromStdString(*a.workspace().title(*a.workspace().editingTarget())), QStringLiteral("Untitled"));
        QVERIFY(!target(a)->isDirty()); // untouched: nothing to review
        QCOMPARE(target(a)->document().lines().size(), std::size_t(1));
        edit(a, u8"hello");
        const QString path = dir.filePath(QStringLiteral("untitled.ass"));
        QVERIFY(a.saveAs(path));
        a.waitForWrites();
        QTRY_VERIFY(!a.targetUntitled());
        QTRY_COMPARE(QString::fromStdString(*a.workspace().title(*a.workspace().editingTarget())),
                     QStringLiteral("untitled.ass"));
        const QByteArray bytes = readAll(path);
        QVERIFY(bytes.contains("Title: HikariSub Ass File"));
        QVERIFY(bytes.contains("Style: Default,Garamond,40,"));
        QVERIFY(bytes.contains(",,hello"));
    }

    // The window close is intercepted while there is unsaved work.
    void closingTheWindowShowsTheReview()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "window.ass", "x")));
        edit(a, u8"unsaved");
        QQmlApplicationEngine engine;
        hikari::ui::attachDocking(engine);
        engine.setInitialProperties(a.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->close();
        QVERIFY(window->isVisible());
        // PU1-unsaved-window: one Document is asked about in a message box.
        QTRY_VERIFY(window->findChild<QQuickWindow *>(QStringLiteral("closeQuestion")));
        auto *review = window->findChild<QQuickWindow *>(QStringLiteral("closeQuestion"));
        QTRY_VERIFY(review->isVisible());
        QVERIFY(QMetaObject::invokeMethod(review, "answer", Q_ARG(QVariant, QStringLiteral("discard"))));
        QTRY_VERIFY(a.quitApproved());
        QTRY_VERIFY(!window->isVisible());
    }

    // The review holds the close back before the docking engine sees it: the
    // engine closes every panel on a close the window receives, which left
    // the window empty while the review showed and after Cancel.
    void cancellingTheReviewKeepsThePanels()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "panels.ass", "x")));
        edit(a, u8"unsaved");
        QQmlApplicationEngine engine;
        hikari::ui::attachDocking(engine);
        engine.setInitialProperties(a.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->property("arrangementSettled").toBool());
        const auto panelsOpen = [window] {
            for (const char *name : {"videoDock", "audioDock", "editorDock", "gridDock"})
                if (!window->findChild<QObject *>(QLatin1String(name))->property("isOpen").toBool())
                    return false;
            return true;
        };
        QVERIFY(panelsOpen());
        window->close();
        QTRY_VERIFY(window->findChild<QQuickWindow *>(QStringLiteral("closeQuestion")));
        auto *review = window->findChild<QQuickWindow *>(QStringLiteral("closeQuestion"));
        QTRY_VERIFY(review->isVisible());
        QVERIFY(window->isVisible());
        QVERIFY(panelsOpen());
        QVERIFY(QMetaObject::invokeMethod(review, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!review->isVisible());
        QVERIFY(!a.quitApproved());
        QVERIFY(window->isVisible());
        QVERIFY(panelsOpen());
    }

    // PU1-unsaved-window: several Documents are reviewed in one window, a
    // row each with its folder and a Save / Don't save switch; the count says
    // what the button does, and the buttons are named for the command.
    void severalDocumentsAreReviewedInTheWindow()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "first.ass", "x")));
        edit(a, u8"one");
        QVERIFY(a.openFile(writeFile(dir, "second.ass", "x")));
        a.selectTab(1);
        edit(a, u8"two");
        QQmlApplicationEngine engine;
        hikari::ui::attachDocking(engine);
        engine.setInitialProperties(a.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->close();
        QTRY_VERIFY(window->findChild<QQuickWindow *>(QStringLiteral("closeReview")));
        auto *review = window->findChild<QQuickWindow *>(QStringLiteral("closeReview"));
        QTRY_VERIFY(review->isVisible());
        QCOMPARE(review->title(), QStringLiteral("Quit"));
        const auto named = [review](const char *name) { return find(review, name); };
        QCOMPARE(named("closeReviewText")->property("text").toString(),
                 QStringLiteral("Save the changes to 2 documents before quitting?"));
        QTRY_VERIFY(named("closeFolder1"));
        QCOMPARE(named("closeFolder0")->property("text").toString(), QDir::toNativeSeparators(dir.path()));
        QCOMPARE(named("closeReviewCount")->property("text").toString(), QStringLiteral("Saves 2 · Discards 0"));
        QCOMPARE(named("closeSaveAndClose")->property("text").toString(), QStringLiteral("Save and quit"));
        QCOMPARE(named("closeWithoutSaving")->property("text").toString(), QStringLiteral("Quit without saving"));
        // The second row set to Don't save: only the first is written.
        QVERIFY(QMetaObject::invokeMethod(named("closeDiscard1"), "clicked"));
        QTRY_COMPARE(named("closeReviewCount")->property("text").toString(), QStringLiteral("Saves 1 · Discards 1"));
        QVERIFY(QMetaObject::invokeMethod(named("closeSaveAndClose"), "clicked"));
        QTRY_VERIFY(a.quitApproved());
        a.waitForWrites();
        QVERIFY(readAll(dir.filePath(QStringLiteral("first.ass"))).contains("one"));
        QVERIFY(!readAll(dir.filePath(QStringLiteral("second.ass"))).contains("two"));
    }

    // An Untitled Document set to Save asks for its file with the picker,
    // titled for it, when Save and quit is pressed; then everything is written.
    void anUntitledRowAsksForItsFileInTurn()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "named.ass", "x")));
        edit(a, u8"named work");
        a.addPage();
        edit(a, u8"new work");
        QQmlApplicationEngine engine;
        hikari::ui::attachDocking(engine);
        engine.setInitialProperties(a.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->close();
        QTRY_VERIFY(window->findChild<QQuickWindow *>(QStringLiteral("closeReview")));
        auto *review = window->findChild<QQuickWindow *>(QStringLiteral("closeReview"));
        QTRY_VERIFY(review->isVisible());
        const auto named = [window](const char *name) { return find(window, name); };
        QTRY_VERIFY(named("closeFolder1"));
        QCOMPARE(named("closeFolder1")->property("text").toString(), QStringLiteral("Not saved yet"));
        QVERIFY(named("closeChooseFile1")->property("visible").toBool());
        QVERIFY(!named("closeChooseFile0")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("closeSaveAndClose"), "clicked"));
        QObject *picker = named("closeSaveAs");
        QTRY_VERIFY(picker->property("visible").toBool());
        QCOMPARE(picker->property("title").toString(), QStringLiteral("Save “Untitled” as"));
        QVERIFY(!a.quitApproved()); // nothing written before the file is chosen
        // A name without the extension gets the format's, as Save as gives it.
        const QString chosen = dir.filePath(QStringLiteral("chosen.ass"));
        picker->setProperty("selectedFile", QUrl::fromLocalFile(dir.filePath(QStringLiteral("chosen"))));
        QVERIFY(QMetaObject::invokeMethod(picker, "accepted"));
        QMetaObject::invokeMethod(picker, "close");
        QTRY_VERIFY(a.quitApproved());
        a.waitForWrites();
        QVERIFY(readAll(chosen).contains("new work"));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("chosen"))));
        QVERIFY(readAll(dir.filePath(QStringLiteral("named.ass"))).contains("named work"));
    }
};

QTEST_MAIN(CloseReviewTests)
#include "close_review_tests.moc"
