// P3: autosave recovery through the composition (accepted
// L58-recovery-copy and the 2026-09-29 retention choices). A child process
// edits, autosaves and dies without closing (as a crash or kill would leave
// it); a new session offers the work as a new unsaved copy with
// the draft still pending and the original file unchanged. A clean close
// and a written save remove the work; a running session's work is not offered.

#include "hikari/app/application.h"
#include "docking.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString writeFile(const QString &path, const char *text)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,");
        f.write(text);
        f.write("\nDialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n");
    }
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

std::string text(application::EditSession *s, std::size_t row)
{
    const auto &t = s->document().lines()[row]->text;
    return {t.begin(), t.end()};
}

void edit(app::Application &a, const char8_t *text)
{
    auto *s = a.files().session(*a.workspace().editingTarget());
    const auto line = s->document().lines().front()->id;
    s->run(application::Command{"Set text", s->revision(), {line}, [&](core::Document &d) { return d.setLineText(line, text); }});
}

// The child: edit, type a draft, autosave, then die without closing.
int crashChild(const QString &recovery, const QString &path)
{
    app::Application::Options options;
    options.recoveryDir = recovery;
    app::Application a(options);
    if (!a.openFile(path))
        return 2;
    edit(a, u8"committed in the crash");
    auto *s = a.files().session(*a.workspace().editingTarget());
    const auto second = s->document().lines()[1]->id;
    s->setSelection(application::Selection{second, {second}, second, {}});
    if (!s->editDraftText(second, u8"typed, never committed"))
        return 3;
    if (!a.autosaveNow())
        return 4;
    // No destructors: the session lock stays, its process gone.
#ifdef _WIN32
    // As a kill ends it. std::_Exit ends through ExitProcess, which stops the
    // other threads wherever they are and then runs the DLLs' detach code
    // (Qt's static destructors among it); there the first child hung until
    // the test gave up on it.
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    std::_Exit(0);
}

} // namespace

class RecoveryTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    app::Application::Options options()
    {
        app::Application::Options o;
        // Each test its own directory: a session that ends without quitting leaves work.
        o.recoveryDir = dir.filePath(QStringLiteral("Recovery-") + QLatin1String(QTest::currentTestFunction()));
        return o;
    }
    // A crashed session's autosave in this test's directory.
    void crashLeavesWork(const QString &path)
    {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash-child"), options().recoveryDir, path});
        QVERIFY(child.waitForFinished(60'000));
        QCOMPARE(child.exitCode(), 0);
    }
    // The main window for `a`, its own settings file.
    app::Application::Options uiOptions()
    {
        app::Application::Options o = options();
        o.settingsFile = dir.filePath(QStringLiteral("ui-") + QLatin1String(QTest::currentTestFunction()) + QStringLiteral(".ini"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        return o;
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
    QQuickWindow *load(app::Application &a, QQmlApplicationEngine &engine)
    {
        hikari::ui::attachDocking(engine);
        engine.setInitialProperties(a.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        return window && QTest::qWaitForWindowExposed(window) ? window : nullptr;
    }

private slots:
    void workLeftByAKilledProcessOpensAsAnUnsavedCopy()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("crash.ass")), "original");
        const QByteArray before = readAll(path);
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--crash-child"), options().recoveryDir, path});
        QVERIFY(child.waitForFinished(60'000));
        QCOMPARE(child.exitCode(), 0);

        app::Application a(options());
        const auto bundles = a.recoveryBundles();
        QCOMPARE(bundles.size(), qsizetype(1));
        const auto bundle = bundles.first().toMap();
        QCOMPARE(bundle.value(QStringLiteral("title")).toString(), QStringLiteral("crash.ass"));
        const auto generation = bundle.value(QStringLiteral("generations")).toList().first().toMap();
        QVERIFY(a.recoverBundle(bundle.value(QStringLiteral("key")).toString(),
                                generation.value(QStringLiteral("generation")).toULongLong()));
        auto *s = a.files().session(*a.workspace().editingTarget());
        QCOMPARE(QString::fromStdString(*a.workspace().title(*a.workspace().editingTarget())),
                 QStringLiteral("crash.ass (recovered)"));
        QVERIFY(a.targetUntitled());     // a new copy: Save asks where
        QVERIFY(s->isDirty());
        QCOMPARE(text(s, 0), std::string("committed in the crash"));
        QCOMPARE(text(s, 1), std::string("second")); // committed content unchanged...
        QVERIFY(s->draftLine());                     // ...the draft still pending
        QCOMPARE(s->draftText().value(), std::u8string(u8"typed, never committed"));
        QCOMPARE(readAll(path), before);             // the original file untouched
        // Dismissed, it is gone.
        a.dismissBundle(bundle.value(QStringLiteral("key")).toString());
        QVERIFY(a.recoveryBundles().isEmpty());
    }

    void aCleanCloseOrAWrittenSaveRemovesTheWork()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("clean.ass")), "x");
        {
            app::Application a(options());
            QVERIFY(a.openFile(path));
            edit(a, u8"discarded");
            QVERIFY(a.autosaveNow());
            const auto rows = a.reviewClose(QStringLiteral("close"));
            QVariantList choices;
            for (const QVariant &r : rows)
                choices << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))}, {QStringLiteral("save"), false}};
            a.resolveClose(choices); // explicit Discard
            QVERIFY(a.openFile(path));
            edit(a, u8"saved");
            QVERIFY(a.autosaveNow());
            QVERIFY(a.saveAs(path));
            a.waitForWrites();
        }
        app::Application b(options());
        QVERIFY(b.recoveryBundles().isEmpty());
    }

    void removingTemporaryFilesTakesOnlyEndedSessionsWork()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("temp.ass")), "x");
        for (const char *name : {"one", "two"}) {
            QProcess child;
            child.start(QCoreApplication::applicationFilePath(),
                        {QStringLiteral("--crash-child"), options().recoveryDir, path});
            QVERIFY(child.waitForFinished(60'000));
            QCOMPARE(child.exitCode(), 0);
            Q_UNUSED(name);
        }
        app::Application running(options());
        QVERIFY(running.openFile(path));
        edit(running, u8"live");
        QVERIFY(running.autosaveNow());
        app::Application a(options());
        QCOMPARE(a.recoveryBundles().size(), qsizetype(2));
        // Older than yesterday: nothing yet.
        QCOMPARE(a.removeAutosavesOlderThan(QDate::currentDate().addDays(-1)), 0);
        QCOMPARE(a.removeAutosavesOlderThan(QDate::currentDate().addDays(1)), 2);
        QVERIFY(a.recoveryBundles().isEmpty());
        // The running session's work is still there for it.
        QVERIFY(QFile::exists(options().recoveryDir));
        QCOMPARE(running.removeAutosavesOlderThan(QDate()), 0);
    }

    void aRunningSessionsWorkIsNotOffered()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("running.ass")), "x");
        app::Application running(options());
        QVERIFY(running.openFile(path));
        edit(running, u8"live");
        QVERIFY(running.autosaveNow());
        app::Application other(options());
        QVERIFY(other.recoveryBundles().isEmpty());
    }

    // S4: legacy's autosave is SubsGrid::SaveFile, whose GetAssHeader writes
    // the active Line's row as "Active Line" into the open Document's Script
    // Info (SubsGridBase.cpp:1470 at 20d647c4), so macros run after an
    // autosave see it; the autosave holds it too. No undo step, and the
    // Document stays as modified as it was. A Save does the same.
    void anAutosaveWritesTheActiveLine()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("active.ass")), "x");
        const QByteArray before = readAll(path);
        {
            app::Application a(options());
            QVERIFY(a.openFile(path));
            edit(a, u8"changed");
            auto *s = a.files().session(*a.workspace().editingTarget());
            const auto second = s->document().lines()[1]->id;
            s->setSelection(application::Selection{second, {second}, second, {}});
            const auto steps = s->historySize();
            QVERIFY(!s->document().scriptInfo(application::kActiveLineKey));
            QVERIFY(a.autosaveNow());
            QVERIFY(s->document().scriptInfo(application::kActiveLineKey) == std::u8string(u8"1"));
            QCOMPARE(s->historySize(), steps);
            QVERIFY(s->isDirty());
            QCOMPARE(readAll(path), before); // the file itself only on Save
            // The autosave holds it.
            bool held = false;
            QDirIterator files(options().recoveryDir, QDir::Files, QDirIterator::Subdirectories);
            while (!held && files.hasNext())
                held = readAll(files.next()).contains("Active Line: 1");
            QVERIFY(held);
        }
    }

    void aSaveWritesTheActiveLine()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("saved.ass")), "x");
        app::Application a(options());
        QVERIFY(a.openFile(path));
        auto *s = a.files().session(*a.workspace().editingTarget());
        const auto second = s->document().lines()[1]->id;
        s->setSelection(application::Selection{second, {second}, second, {}});
        edit(a, u8"changed");
        const auto steps = s->historySize();
        QVERIFY(a.saveAs(path));
        a.waitForWrites();
        QVERIFY(readAll(path).startsWith("[Script Info]\r\nActive Line: 1\r\n") ||
                readAll(path).startsWith("[Script Info]\nActive Line: 1\n"));
        QVERIFY(s->document().scriptInfo(application::kActiveLineKey) == std::u8string(u8"1"));
        QCOMPARE(s->historySize(), steps);
        QVERIFY(!s->isDirty());
    }

    // P10: legacy SubsGrid::OnBackupTimer writes "Autosave" in the status
    // PU1-startup-cards and PU1-autosaves-window: work left by a crash is
    // offered on the Home page, not in a window that opens by itself; its
    // Autosaves… opens the Autosaves window, whose search and date filter
    // narrow the list, and Open copy opens the work and ends the card.
    void aCrashIsOfferedOnHomeAndOpensFromAutosaves()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("card.ass")), "original");
        crashLeavesWork(path);
        app::Application a(uiOptions());
        QQmlApplicationEngine engine;
        QQuickWindow *window = load(a, engine);
        QVERIFY(window);
        const auto named = [window](const char *name) { return find(window, name); };
        QObject *card = named("homeOffer");
        QVERIFY(card);
        QTRY_VERIFY(card->property("visible").toBool());
        QCOMPARE(named("homeOfferTitle")->property("text").toString(), QStringLiteral("HikariSub did not close properly"));
        QCOMPARE(named("homeOfferDetail")->property("text").toString(), QStringLiteral("Unsaved work in 1 document was autosaved."));
        QVERIFY(!named("homeOfferRestore")->property("visible").toBool()); // no session to restore
        QVERIFY(named("homeOfferAutosaves")->property("visible").toBool());
        QVERIFY(!named("homeSession")->property("visible").toBool());
        QVERIFY(!named("autosavesWindow")); // nothing opened by itself
        QVERIFY(QMetaObject::invokeMethod(named("homeOfferAutosaves"), "clicked"));
        QTRY_VERIFY(named("autosavesWindow"));
        auto *autosaves = qobject_cast<QQuickWindow *>(named("autosavesWindow"));
        QTRY_VERIFY(autosaves->isVisible());
        QObject *list = named("autosavesList");
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(named("autosaveName0"));
        QCOMPARE(named("autosaveName0")->property("text").toString(), QStringLiteral("card.ass"));
        // Every word of the search, in the name or the path.
        named("autosavesSearch")->setProperty("text", QStringLiteral("card zzz"));
        QTRY_COMPARE(list->property("count").toInt(), 0);
        QVERIFY(named("autosavesEmpty")->property("visible").toBool());
        named("autosavesSearch")->setProperty("text", QStringLiteral("CARD ass"));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        // Saved today; none before today.
        QObject *model = named("autosaves");
        model->setProperty("range", QStringLiteral("today"));
        QCOMPARE(list->property("count").toInt(), 1);
        model->setProperty("before", QDateTime::currentDateTime());
        model->setProperty("range", QStringLiteral("before"));
        QCOMPARE(list->property("count").toInt(), 0);
        model->setProperty("range", QStringLiteral("any"));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(named("autosaveOpen0"));
        QVERIFY(QMetaObject::invokeMethod(named("autosaveOpen0"), "clicked"));
        QTRY_VERIFY(!autosaves->isVisible());
        QCOMPARE(QString::fromStdString(*a.workspace().title(*a.workspace().editingTarget())),
                 QStringLiteral("card.ass (recovered)"));
        // A Document opened from Home ends the offer; no startup bar either.
        QTRY_VERIFY(!card->property("visible").toBool());
        QVERIFY(!named("startupBar")->property("visible").toBool());
    }

    // PU1-autosaves-window: Remove temporary files opens the window with
    // Delete older than…; its button counts what goes and deletes at once.
    // Started with files, the crash is the startup bar, which ends when the
    // autosaves it offered are deleted.
    void deleteOlderThanDeletesInOneStep()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("older.ass")), "original");
        crashLeavesWork(path);
        app::Application a(uiOptions());
        a.setStartedWithPaths(true);
        QVERIFY(a.openFile(writeFile(dir.filePath(QStringLiteral("other.ass")), "other")));
        QQmlApplicationEngine engine;
        QQuickWindow *window = load(a, engine);
        QVERIFY(window);
        const auto named = [window](const char *name) { return find(window, name); };
        QObject *bar = named("startupBar");
        QTRY_VERIFY(bar->property("visible").toBool());
        QCOMPARE(bar->property("severity").toString(), QStringLiteral("warning"));
        QCOMPARE(bar->property("text").toString(),
                 QStringLiteral("HikariSub did not close properly. Unsaved work in 1 document was autosaved."));
        QVERIFY(!named("startupRestore")->property("visible").toBool());
        QVERIFY(named("startupAutosaves")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("autosaves"), "show", Q_ARG(QVariant, true)));
        QTRY_VERIFY(named("autosavesWindow"));
        auto *autosaves = qobject_cast<QQuickWindow *>(named("autosavesWindow"));
        QTRY_VERIFY(autosaves->isVisible());
        QObject *popover = named("autosavesDeleteOlderPopover");
        QTRY_VERIFY(popover->property("visible").toBool());
        QObject *now = named("autosavesDeleteOlderNow");
        // A month back keeps today's work; tomorrow ("Everything") takes it.
        QVERIFY(!now->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("olderDate"), "setDate",
                                          Q_ARG(QVariant, QDateTime(QDate::currentDate().addDays(1), QTime(12, 0)))));
        QTRY_COMPARE(now->property("text").toString(), QStringLiteral("Delete 1 autosave"));
        QVERIFY(now->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(now, "clicked"));
        QTRY_VERIFY(a.recoveryBundles().isEmpty());
        QTRY_COMPARE(named("autosavesList")->property("count").toInt(), 0);
        // Its only reason gone (no session to restore), the offer ends.
        QTRY_VERIFY(!bar->property("visible").toBool());
    }

    // bar's first field and its nullifyTimer empties the field 5 s later,
    // whatever it shows then (SubsGridBase.cpp:1636-1651, SubsGrid.cpp:54-57).
    void anAutosaveShowsInTheStatusBarForFiveSeconds()
    {
        const QString path = writeFile(dir.filePath(QStringLiteral("status.ass")), "x");
        app::Application a(options());
        QVERIFY(a.openFile(path));
        edit(a, u8"changed");
        QElapsedTimer clock;
        clock.start();
        QVERIFY(a.autosaveNow());
        QCOMPARE(a.shell().statusText(), QStringLiteral("Autosave"));
        a.shell().setStatusText(QStringLiteral("set_status_text"));
        QTRY_COMPARE_WITH_TIMEOUT(a.shell().statusText(), QString(), 10000);
        QVERIFY2(clock.elapsed() >= 4900, qPrintable(QString::number(clock.elapsed())));
    }
};

int main(int argc, char **argv)
{
    if (argc >= 4 && QByteArray(argv[1]) == "--crash-child") {
        QGuiApplication app(argc, argv);
        return crashChild(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    QGuiApplication app(argc, argv);
    RecoveryTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "recovery_tests.moc"
