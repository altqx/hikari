// P3: autosave recovery through the composition (accepted
// L58-recovery-copy and the 2026-09-29 retention choices). A child process
// edits, autosaves and dies without closing (std::_Exit, as a crash or kill
// would leave it); a new session offers the work as a new unsaved copy with
// the draft still pending and the original file unchanged. A clean close
// and a written save remove the work; a running session's work is not offered.

#include "hikari/app/application.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cstdlib>

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
    std::_Exit(0); // no destructors: the session lock stays, its process gone
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
