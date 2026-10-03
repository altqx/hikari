// P1: close review through the composition (accepted L58-write-close): a
// Save-dependent close proceeds only after an acknowledged write; Discard
// covers only the work it was chosen for; Cancel keeps everything; one quit
// review covers every Document; New leaves an Untitled Document that saves
// through Save As.

#include "hikari/app/application.h"
#include "docking.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
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

private slots:
    void aCleanDocumentClosesWithoutReview()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "clean.ass", "x")));
        QVERIFY(a.reviewClose(QStringLiteral("close")).isEmpty());
        a.finishClose();
        QVERIFY(!a.workspace().editingTarget());
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
        QVERIFY(!a.workspace().editingTarget());
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
        QVERIFY(!a.workspace().editingTarget());
        QVERIFY(readAll(path).contains(",,saved"));
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
        auto *review = window->findChild<QQuickWindow *>(QStringLiteral("closeReview"));
        QVERIFY(review);
        QTRY_VERIFY(review->isVisible());
        QVERIFY(QMetaObject::invokeMethod(review->findChild<QObject *>(QStringLiteral("closeDiscardAll")), "clicked"));
        QTRY_VERIFY(a.quitApproved());
        QTRY_VERIFY(!window->isVisible());
    }
};

QTEST_MAIN(CloseReviewTests)
#include "close_review_tests.moc"
