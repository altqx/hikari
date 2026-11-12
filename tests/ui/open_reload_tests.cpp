#include "hikari/app/application.h"
#include "hikari/app/composition.h"

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

}

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

    void lyricsOpenAsAnUntitledAssDocument()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "kept.ass", "kept")));
        const QString lrc = dir.filePath(QStringLiteral("enhanced.lrc"));
        QFile::remove(lrc);
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_LYRICS_FIXTURES "/enhanced.lrc"), lrc));
        const auto result = a.reviewOpen(lrc);
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        a.finishClose();
        QCOMPARE(targetTitle(a), std::string("enhanced.ass"));
        QCOMPARE(targetText(a), std::string("{\\k20}{\\k70}Twinkle, {\\k70}twinkle, {\\k50}little {\\k90}star"));
        QCOMPARE(target(a)->document().lines().size(), std::size_t(6));
        QVERIFY(target(a)->isDirty());
        QVERIFY(a.targetUntitled());
        const auto values = a.saveDialogValues();
        QCOMPARE(values.value(QStringLiteral("file")).toUrl().toLocalFile(), dir.filePath(QStringLiteral("enhanced")));
        QCOMPARE(values.value(QStringLiteral("extension")).toString(), QStringLiteral("ass"));
    }

    void importedFormatsOpenAsSubtitlesFromTheCommandLineADropAndRecentlyOpened()
    {
        QStringList files;
        for (const char *name : {"decoded.krc", "decoded.qrc"}) {
            files << dir.filePath(QStringLiteral("cli-%1").arg(QLatin1String(name)));
            QFile::remove(files.back());
            QVERIFY(QFile::copy(QStringLiteral(HIKARI_LYRICS_FIXTURES "/") + QLatin1String(name), files.back()));
        }
        for (const char *name : {"cli-clip.ytt", "cli-clip.srv3"}) {
            files << dir.filePath(QLatin1String(name));
            QFile f(files.back());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"(<timedtext format="3"><body><p t="1000" d="2000">Hello</p></body></timedtext>)");
        }
        for (const QString &lyrics : files) {
            const std::string title = QFileInfo(lyrics).completeBaseName().toStdString() + ".ass";

            app::Application a;
            QVERIFY(!app::openStartFile(a, lyrics));
            QCOMPARE(targetTitle(a), title);
            QVERIFY(a.targetUntitled());

            const QVariantMap dropped = a.openDropped({QUrl::fromLocalFile(lyrics)});
            QCOMPARE(dropped.value(QStringLiteral("kind")).toString(), QStringLiteral("subtitles"));

            const QVariantList recent = a.recentSubtitles();
            QVERIFY(!recent.isEmpty());
            const QString remembered = recent.front().toMap().value(QStringLiteral("path")).toString();
            QCOMPARE(QFileInfo(remembered).fileName(), QFileInfo(lyrics).fileName());
            const int tabs = static_cast<int>(a.tabs().size());
            QVERIFY(a.openForwarded({remembered}).value(QStringLiteral("kind")).toString().isEmpty());
            QCOMPARE(static_cast<int>(a.tabs().size()), tabs + 1);
            QCOMPARE(targetTitle(a), title);
        }
    }

    void youTubeTimedTextOpensAsAnUntitledAssDocument()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "kept.ass", "kept")));
        QFile ytt(dir.filePath(QStringLiteral("clip.ytt")));
        QVERIFY(ytt.open(QIODevice::WriteOnly));
        ytt.write(R"(<timedtext format="3"><body><p t="1000" d="2000">Hello</p></body></timedtext>)");
        ytt.close();
        const auto result = a.reviewOpen(ytt.fileName());
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        a.finishClose();
        QCOMPARE(targetTitle(a), std::string("clip.ass"));
        QCOMPARE(targetText(a), std::string("{\\c&HFFFFFF&\\1a&H0&\\3c&H080808&\\3a&H3F&}Hello"));
        QVERIFY(target(a)->isDirty());
        QVERIFY(a.targetUntitled());
        const auto values = a.saveDialogValues();
        QCOMPARE(values.value(QStringLiteral("file")).toUrl().toLocalFile(), dir.filePath(QStringLiteral("clip")));
        QCOMPARE(values.value(QStringLiteral("extension")).toString(), QStringLiteral("ass"));
    }

    void refusedYouTubeTimedTextNamesTheReasonAndChangesNothing()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "kept.ass", "kept")));
        edit(a, u8"unsaved");
        QFile ytt(dir.filePath(QStringLiteral("edge.ytt")));
        QVERIFY(ytt.open(QIODevice::WriteOnly));
        ytt.write(R"(<timedtext><head><pen id="1" et="9"/></head></timedtext>)");
        ytt.close();
        const auto result = a.reviewOpen(ytt.fileName());
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("problem")).toString(),
                 QStringLiteral("Could not import edge.ytt as YouTube timed text. Specified argument was out of the range of "
                                "valid values. Nothing was imported."));
        QCOMPARE(targetTitle(a), std::string("kept.ass"));
        QCOMPARE(targetText(a), std::string("unsaved"));
        QCOMPARE(a.workspace().documents().size(), std::size_t(1));
    }

    void malformedLyricsNameTheLineAndChangeNothing()
    {
        app::Application a;
        QVERIFY(a.openFile(writeFile(dir, "kept.ass", "kept")));
        edit(a, u8"unsaved");
        const auto result = a.reviewOpen(QStringLiteral(HIKARI_LYRICS_FIXTURES "/malformed.lrc"));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("problem")).toString(),
                 QStringLiteral("Could not import malformed.lrc: line 3: the word at <00:04.20> starts before the word "
                                "before it. Nothing was imported."));
        const auto lyrics = [this](const char *name, const QByteArray &text) {
            QFile f(dir.filePath(QLatin1String(name)));
            return f.open(QIODevice::WriteOnly) && f.write(text) == text.size() ? f.fileName() : QString();
        };
        const auto outside =
            a.reviewOpen(lyrics("outside.ttml", "<tt><body><div begin=\"0\" end=\"2\"><p begin=\"3\">x</p></div></body></tt>"));
        QCOMPARE(outside.value(QStringLiteral("problem")).toString(),
                 QStringLiteral("Could not import outside.ttml: line 1: the time 3 is outside the times of the element "
                                "around it. Nothing was imported."));
        const auto cut = a.reviewOpen(
            lyrics("long.ttml", "<tt><body><p>" + QByteArray(58, 'a') + "\xF0\x9F\x8E\xA4 b c d</p></body></tt>"));
        QCOMPARE(cut.value(QStringLiteral("problem")).toString(),
                 QStringLiteral("Could not import long.ttml: line 1: “") + QString(58, QLatin1Char('a')) + QChar(0x2026) +
                     QStringLiteral("” has no time. Nothing was imported."));
        QCOMPARE(targetTitle(a), std::string("kept.ass"));
        QCOMPARE(targetText(a), std::string("unsaved"));
        QCOMPARE(a.workspace().documents().size(), std::size_t(1));
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
        a.cancelClose();
        QCOMPARE(targetText(a), std::string("unsaved"));
        a.reviewClose(QStringLiteral("close"));
        a.cancelClose();
        QCOMPARE(targetTitle(a), std::string("work.ass"));
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
        const QString settings = own.filePath(QStringLiteral("settings.toml"));
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
        QVERIFY(QFile::remove(b1));
        const auto rows = again.recentSubtitles();
        QCOMPARE(rows.size(), qsizetype(2));
        QCOMPARE(rows[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("1 c.ass"));
        QCOMPARE(rows[1].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("2 a.ass"));
        app::Application::Options third;
        third.settingsFile = settings;
        {
            app::Application kept(third);
            QCOMPARE(kept.recentEntries().size(), std::size_t(3));
            kept.removeMissingRecent();
        }
        QCOMPARE(app::Application(third).recentEntries().size(), std::size_t(2));
    }

    void hiddenGridColumnsPersist()
    {
        QTemporaryDir own;
        app::Application::Options options;
        options.settingsFile = own.filePath(QStringLiteral("settings.toml"));
        {
            app::Application a(options);
            QCOMPARE(a.shell().hiddenColumns(), 0);
            a.shell().toggleColumn(8192);
            a.shell().toggleColumn(1);
        }
        app::Application again(options);
        QCOMPARE(again.shell().hiddenColumns(), 8193);
    }

    void filteringPreferencesPersistAndFilterAfterLoading()
    {
        QTemporaryDir own;
        app::Application::Options options;
        options.settingsFile = own.filePath(QStringLiteral("settings.toml"));
        const QString path = own.filePath(QStringLiteral("comments.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,shown\n"
                    "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,note\n");
        }
        {
            app::Application a(options);
            a.gridFilter().setFilterBy(4, true);
            a.gridFilter().setAfterLoad(true);
            a.gridFilter().setInverted(false);
        }
        app::Application again(options);
        QCOMPARE(again.gridFilter().filterBy(), 4);
        QVERIFY(again.gridFilter().afterLoad());
        QVERIFY(again.openFile(path));
        const auto lines = target(again)->document().lines();
        QCOMPARE(lines[1]->visibility, core::LineVisibility::Hidden);
        QCOMPARE(target(again)->history().back().name, std::string("Filtering"));
        QVERIFY(again.shell().filtered());
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
        QCOMPARE(a.externalChange(), QString());
        QVERIFY(a.reloadTarget());
        QCOMPARE(targetText(a), std::string("theirs"));
        QVERIFY(!target(a)->isDirty());
        QCOMPARE(target(a)->historySize(), std::size_t(1));
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
        auto kind = [](const QVariantMap &r) { return r.value(QStringLiteral("kind")).toString(); };
        QVariantMap one = a.openDropped({QUrl::fromLocalFile(c)});
        QCOMPARE(kind(one), QStringLiteral("subtitles"));
        QCOMPARE(one.value(QStringLiteral("path")).toString(), c);
        QCOMPARE(kind(a.openDropped({QUrl::fromLocalFile(dir.filePath(QStringLiteral("x.mkv")))})), QStringLiteral("video"));
        QCOMPARE(kind(a.openDropped({QUrl::fromLocalFile(dir.filePath(QStringLiteral("x.7z")))})), QString());
        QCOMPARE(kind(a.openDropped({QUrl::fromLocalFile(dir.filePath(QStringLiteral("k_keyframes.txt")))})), QString());
        QCOMPARE(kind(a.openDropped({QUrl(QStringLiteral("https://example.com/a.ass"))})), QString());
        const QVariantMap several = a.openDropped({QUrl::fromLocalFile(b), QUrl::fromLocalFile(c),
                                                   QUrl::fromLocalFile(dir.filePath(QStringLiteral("x.zip")))});
        QCOMPARE(kind(several), QStringLiteral("files"));
        QVERIFY(several.value(QStringLiteral("rows")).toList().isEmpty());
        QStringList titles;
        for (const QVariant &tab : a.tabs())
            titles << tab.toMap().value(QStringLiteral("title")).toString();
        QCOMPARE(titles, (QStringList{QStringLiteral("drop-a.ass"), QStringLiteral("drop-b.srt")}));
        QCOMPARE(a.currentTab(), 1);
    }
};

QTEST_MAIN(OpenReloadTests)
#include "open_reload_tests.moc"
