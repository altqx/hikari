// P2 through the composition: opening subtitles stages the file before the
// review replaces the editing target (L58-staged-replacement); the recent
// list follows opens and saves and persists; a file changed by another
// program is offered for reload once, and a removed one makes the Document
// unsaved; dropped files follow the legacy extension rules.

#include "hikari/app/application.h"

#include <QDateTime>
#include <QFile>
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

void touchLater(const QString &path, int seconds)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(seconds), QFileDevice::FileModificationTime));
}

QVariantList discardAll(const QVariantList &rows)
{
    QVariantList out;
    for (const QVariant &r : rows)
        out << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))},
                           {QStringLiteral("save"), false},
                           {QStringLiteral("path"), QString()}};
    return out;
}

} // namespace

class OpenReloadTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    application::EditSession *target(app::Application &a)
    {
        const auto t = a.workspace().editingTarget();
        return t ? a.files().session(*t) : nullptr;
    }
    std::string targetText(app::Application &a)
    {
        const auto &text = target(a)->document().lines().front()->text;
        return {text.begin(), text.end()};
    }
    std::string targetTitle(app::Application &a)
    {
        const auto t = a.workspace().editingTarget();
        const std::string *title = t ? a.workspace().title(*t) : nullptr;
        return title ? *title : std::string();
    }
    void edit(app::Application &a, const char8_t *text)
    {
        auto *s = target(a);
        const auto line = s->document().lines().front()->id;
        QVERIFY(s->run(application::Command{"Set text", s->revision(), {line},
                                            [&](core::Document &d) { return d.setLineText(line, text); }}));
    }

private slots:
    void aFailedOpenChangesNothing()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "kept.ass", "kept")));
        edit(a, u8"unsaved");
        const auto result = a.reviewOpen(dir.filePath(QStringLiteral("missing.ass")));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(!result.value(QStringLiteral("problem")).toString().isEmpty());
        QCOMPARE(targetTitle(a), std::string("kept.ass"));
        QCOMPARE(targetText(a), std::string("unsaved"));
        QVERIFY(target(a)->isDirty());
    }

    void aCleanTargetIsReplacedAtOnce()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "first.ass", "one")));
        const auto result = a.reviewOpen(writeFile(dir, "second.ass", "two"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("rows")).toList().isEmpty());
        a.finishClose();
        QCOMPARE(targetTitle(a), std::string("second.ass"));
        QCOMPARE(targetText(a), std::string("two"));
        QCOMPARE(a.workspace().documents().size(), std::size_t(1));
    }

    void unsavedWorkIsReviewedBeforeTheReplacement()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "work.ass", "one")));
        edit(a, u8"unsaved");
        auto result = a.reviewOpen(writeFile(dir, "other.ass", "two"));
        const auto rows = result.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), qsizetype(1));
        // Cancel keeps the work and forgets the staged file.
        a.cancelClose();
        QCOMPARE(targetText(a), std::string("unsaved"));
        a.reviewClose(QStringLiteral("close"));
        a.cancelClose();
        QCOMPARE(targetTitle(a), std::string("work.ass"));
        // Discard publishes the replacement.
        result = a.reviewOpen(dir.filePath(QStringLiteral("other.ass")));
        QSignalSpy finished(&a, &app::Application::closeFinished);
        a.resolveClose(discardAll(result.value(QStringLiteral("rows")).toList()));
        QCOMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(targetTitle(a), std::string("other.ass"));
        QCOMPARE(targetText(a), std::string("two"));
    }

    void theRecentListFollowsOpensAndSavesAndPersists()
    {
        QTemporaryDir own;
        const QString settings = own.filePath(QStringLiteral("hikari.ini"));
        const QString a1 = writeFile(own, "a.ass", "a"), b1 = writeFile(own, "b.ass", "b");
        const QString saved = own.filePath(QStringLiteral("c.ass"));
        {
            app::Application::Options options;
            options.settingsFile = settings;
            app::Application a(options);
            QSignalSpy changed(&a, &app::Application::recentChanged);
            QVERIFY(a.openFile(a1));
            a.reviewOpen(b1);
            a.finishClose();
            QVERIFY(a.saveAs(saved));
            a.waitForWrites();
            QTRY_COMPARE(a.recentEntries().size(), std::size_t(3));
            QVERIFY(changed.size() >= 3);
            QCOMPARE(QString::fromStdString(a.recentEntries()[0]), QFileInfo(saved).absoluteFilePath());
            QCOMPARE(QString::fromStdString(a.recentEntries()[1]), QFileInfo(b1).absoluteFilePath());
            QCOMPARE(QString::fromStdString(a.recentEntries()[2]), QFileInfo(a1).absoluteFilePath());
        }
        app::Application::Options options;
        options.settingsFile = settings;
        app::Application again(options);
        QCOMPARE(again.recentEntries().size(), std::size_t(3));
        // Shown: missing files are pruned, labels number the file names.
        QVERIFY(QFile::remove(b1));
        const auto rows = again.recentSubtitles();
        QCOMPARE(rows.size(), qsizetype(2));
        QCOMPARE(rows[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("1 c.ass"));
        QCOMPARE(rows[1].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("2 a.ass"));
        app::Application::Options third;
        third.settingsFile = settings;
        QCOMPARE(app::Application(third).recentEntries().size(), std::size_t(2));
    }

    void aChangedFileIsOfferedOnceAndReloads()
    {
        app::Application a;
        const QString path = writeFile(dir, "changed.ass", "old");
        QVERIFY(a.openFile(path));
        QCOMPARE(a.externalChange(), QString());
        edit(a, u8"mine");
        writeFile(dir, "changed.ass", "theirs");
        touchLater(path, 10);
        QCOMPARE(a.externalChange(), QStringLiteral("modified"));
        QCOMPARE(a.externalChange(), QString()); // asked once per change
        QVERIFY(a.reloadTarget());
        QCOMPARE(targetText(a), std::string("theirs"));
        QVERIFY(!target(a)->isDirty());
        QCOMPARE(target(a)->historySize(), std::size_t(1)); // fresh history
        QVERIFY(target(a)->selection().active);
    }

    void aFailedReloadKeepsTheDocument()
    {
        app::Application a;
        const QString path = writeFile(dir, "vanish.ass", "old");
        QVERIFY(a.openFile(path));
        edit(a, u8"mine");
        QVERIFY(QFile::remove(path));
        QVERIFY(!a.reloadTarget());
        QCOMPARE(targetText(a), std::string("mine"));
        QVERIFY(target(a)->isDirty());
    }

    void aRemovedFileMakesTheDocumentUnsavedOnce()
    {
        app::Application a;
        const QString path = writeFile(dir, "removed.ass", "x");
        QVERIFY(a.openFile(path));
        QVERIFY(!target(a)->isDirty());
        QVERIFY(QFile::remove(path));
        QCOMPARE(a.externalChange(), QStringLiteral("removed"));
        QVERIFY(target(a)->isDirty());
        QCOMPARE(a.externalChange(), QString());
    }

    void droppedFilesFollowTheLegacyRules()
    {
        app::Application a;
        const QString b = writeFile(dir, "drop-b.srt", "b"), c = writeFile(dir, "drop-a.ass", "a");
        // Several: sorted, the first subtitles open; archives are refused.
        QCOMPARE(a.openDropped({QUrl::fromLocalFile(b), QUrl::fromLocalFile(c),
                                QUrl::fromLocalFile(dir.filePath(QStringLiteral("x.zip")))}),
                 c);
        QCOMPARE(a.openDropped({QUrl::fromLocalFile(dir.filePath(QStringLiteral("x.7z")))}), QString());
        QCOMPARE(a.openDropped({QUrl::fromLocalFile(dir.filePath(QStringLiteral("k_keyframes.txt")))}), QString());
        QCOMPARE(a.openDropped({QUrl(QStringLiteral("https://example.com/a.ass"))}), QString());
    }
};

QTEST_MAIN(OpenReloadTests)
#include "open_reload_tests.moc"
