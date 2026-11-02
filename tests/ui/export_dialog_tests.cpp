#include "hikari/app/application.h"
#include "hikari/application/edit_session.h"

#include "docking.h"
#include "line_editor_controller.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;

namespace {

constexpr char kSubtitles[] =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00101010,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}first{\\i0}\n"
    "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,note\n"
    "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\clip(0,0,10,10)}second\n"
    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\fad(100,100)\\frz10}third\n";

QString written(const QString &path, const char *text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(text);
    return path;
}

QByteArray contents(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}

class ExportDialogTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    QObject *dialog() const { return window()->findChild<QObject *>(QStringLiteral("exportDialog")); }
    template <typename T = QQuickItem> T *part(const char *name) const
    {
        return dialog()->findChild<T *>(QLatin1String(name));
    }
    static QQuickItem *findVisual(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findVisual(child, name))
                return found;
        return nullptr;
    }
    application::EditSession *session() const
    {
        return application->files().session(*application->workspace().editingTarget());
    }
    bool targets(const QString &path)
    {
        const QString name = QFileInfo(path).fileName();
        const QVariantList tabs = application->tabs();
        for (int i = 0; i < tabs.size(); ++i)
            if (tabs[i].toMap().value(QStringLiteral("title")).toString().contains(name))
                application->selectTab(i);
        const QString base = QFileInfo(path).completeBaseName();
        return QTest::qWaitFor([&] { return application->exportSetup().value(QStringLiteral("name")).toString() == base; });
    }
    QString open(const char *name)
    {
        const QString path = written(dir.filePath(QLatin1String(name)), kSubtitles);
        return application->openFile(path) && targets(path) ? path : QString();
    }
    void openDialog()
    {
        auto *item = window()->findChild<QObject *>(QStringLiteral("exportAsMenuItem"));
        QVERIFY(item);
        auto *action = item->property("action").value<QObject *>();
        QVERIFY(action && action->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QTRY_VERIFY(dialog() && dialog()->property("visible").toBool());
    }
    void choose(const char *format, const QString &path)
    {
        const QStringList ids{"srt", "vtt", "sbv", "txt", "mdvd", "mpl2", "tmp"};
        part("exportFormat")->setProperty("currentIndex", ids.indexOf(QLatin1String(format)));
        part("exportDestination")->setProperty("text", path);
    }
    void run()
    {
        QVERIFY(part("exportRun")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(part("exportRun"), "click"));
    }

private slots:
    void init()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        application->applySettings({{QStringLiteral("editor.spellchecker"), false}});
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
    }
    void cleanup()
    {
        application->endSession();
        engine.reset();
        application.reset();
        QFile::remove(dir.filePath(QStringLiteral("settings.toml")));
    }

    void exportAsComesRightAfterSaveAs()
    {
        auto *exportItem = window()->findChild<QObject *>(QStringLiteral("exportAsMenuItem"));
        auto *saveAs = window()->findChild<QObject *>(QStringLiteral("saveAsMenuItem"));
        QVERIFY(exportItem && saveAs);
        QCOMPARE(exportItem->property("command").toString(), QStringLiteral("GLOBAL_EXPORT_SUBS_AS"));
        QVERIFY(!exportItem->property("enabled").toBool());
        QVERIFY(!open("one.ass").isEmpty());
        QTRY_VERIFY(exportItem->property("enabled").toBool());
    }

    void writesACopyAndLeavesTheDocumentAsItWas()
    {
        const QString source = open("episode.ass");
        QVERIFY(!source.isEmpty());
        const auto revision = session()->revision();
        openDialog();
        QCOMPARE(part("exportDestination")->property("text").toString(), dir.filePath(QStringLiteral("episode.srt")));
        const QString out = dir.filePath(QStringLiteral("episode.vtt"));
        choose("vtt", out);
        part<QObject>("exportVttStyles")->setProperty("checked", false);
        run();
        QTRY_VERIFY(dialog()->property("reported").toBool());
        QCOMPARE(contents(out), QByteArray("WEBVTT\n\n00:00:01.000 --> 00:00:02.000\n<i>first</i>\n"
                                           "\n00:00:02.000 --> 00:00:03.000\nsecond\n"
                                           "\n00:00:03.000 --> 00:00:04.000\nthird\n"));
        QCOMPARE(part("exportSummary")->property("text").toString(), QStringLiteral("Exported 3 lines to episode.vtt."));
        QCOMPARE(part("exportSkipped")->property("text").toString(), QStringLiteral("1 comment line skipped."));
        QCOMPARE(session()->revision(), revision);
        QCOMPARE(session()->document().format(), core::SubtitleFormat::Ass);
        QCOMPARE(contents(source), QByteArray(kSubtitles));
    }

    void theReportShowsEachLine()
    {
        QVERIFY(!open("report.ass").isEmpty());
        openDialog();
        choose("srt", dir.filePath(QStringLiteral("report.srt")));
        run();
        QTRY_VERIFY(dialog()->property("reported").toBool());
        const QVariantList notes = dialog()->property("report").toMap().value(QStringLiteral("notes")).toList();
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes[0].toMap().value(QStringLiteral("number")).toInt(), 3);
        QCOMPARE(notes[0].toMap().value(QStringLiteral("losses")).toString(), QStringLiteral("Clip removed"));
        QCOMPARE(notes[1].toMap().value(QStringLiteral("number")).toInt(), 4);
        QCOMPARE(notes[1].toMap().value(QStringLiteral("losses")).toString(),
                 QStringLiteral("Fade removed; Rotation or shearing removed"));
        auto *list = part("exportNotes");
        QTRY_COMPARE(list->property("count").toInt(), 2);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = [&]() -> QQuickItem * {
            for (QQuickItem *child : list->property("contentItem").value<QQuickItem *>()->childItems())
                if (child->property("index").toInt() == 1 && child->property("modelData").isValid())
                    return child;
            return nullptr;
        }()));
        QVERIFY(QMetaObject::invokeMethod(row, "clicked"));
        const auto lines = session()->document().lines();
        QTRY_COMPARE(session()->selection().active, std::optional(lines[3]->id));
    }

    void anExistingFileIsReplacedOnlyWhenAsked()
    {
        QVERIFY(!open("replace.ass").isEmpty());
        const QString out = written(dir.filePath(QStringLiteral("replace.txt")), "keep me");
        openDialog();
        choose("txt", out);
        run();
        QObject *question = nullptr;
        QTRY_VERIFY((question = dialog()->findChild<QObject *>(QStringLiteral("exportReplaceQuestion"))) &&
                    question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QCOMPARE(contents(out), QByteArray("keep me"));
        QVERIFY(!dialog()->property("reported").toBool());
        run();
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "answer", Q_ARG(QVariant, QStringLiteral("replace"))));
        QTRY_VERIFY(dialog()->property("reported").toBool());
        QCOMPARE(contents(out), QByteArray("first\nsecond\nthird\n"));
    }

    void onlyTheSelectedLines()
    {
        QVERIFY(!open("selected.ass").isEmpty());
        const auto lines = session()->document().lines();
        session()->setSelection(application::Selection{lines[2]->id, {lines[2]->id}, lines[2]->id, {}});
        application->editor().reloadFromSession();
        openDialog();
        const QString out = dir.filePath(QStringLiteral("selected.txt"));
        choose("txt", out);
        part("exportLines")->setProperty("currentIndex", 1);
        run();
        QTRY_VERIFY(dialog()->property("reported").toBool());
        QCOMPARE(contents(out), QByteArray("second\n"));
    }

    void theChoiceIsRememberedPerDocument()
    {
        QVERIFY(!open("remembered.ass").isEmpty());
        openDialog();
        choose("mdvd", dir.filePath(QStringLiteral("remembered.sub")));
        part("exportFps")->setProperty("text", QStringLiteral("25"));
        part<QObject>("exportMerge")->setProperty("checked", true);
        run();
        QTRY_VERIFY(dialog()->property("reported").toBool());
        QCOMPARE(contents(dir.filePath(QStringLiteral("remembered.sub"))),
                 QByteArray("{25}{50}{y:i}first\r\n{50}{75}second\r\n{75}{100}third\r\n"));
        QVERIFY(QMetaObject::invokeMethod(part("exportClose"), "click"));
        QTRY_VERIFY(!dialog()->property("visible").toBool());

        QVERIFY(!open("other.ass").isEmpty());
        openDialog();
        choose("sbv", dir.filePath(QStringLiteral("other.sbv")));
        run();
        QTRY_VERIFY(dialog()->property("reported").toBool());
        QVERIFY(QMetaObject::invokeMethod(part("exportClose"), "click"));

        const QString remembered = dir.filePath(QStringLiteral("remembered.ass"));
        QVERIFY(targets(remembered));
        openDialog();
        QCOMPARE(dialog()->property("format").toString(), QStringLiteral("mdvd"));
        QCOMPARE(part("exportFps")->property("text").toString(), QStringLiteral("25"));
        QVERIFY(part<QObject>("exportMerge")->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(part("exportClose"), "click"));

        QVERIFY(!open("new.ass").isEmpty());
        openDialog();
        QCOMPARE(dialog()->property("format").toString(), QStringLiteral("sbv"));
        QCOMPARE(part("exportDestination")->property("text").toString(), dir.filePath(QStringLiteral("new.sbv")));
    }

    void aFrameRateIsNeededForMicroDvd()
    {
        QVERIFY(!open("fps.ass").isEmpty());
        openDialog();
        choose("mdvd", dir.filePath(QStringLiteral("fps.sub")));
        part("exportFps")->setProperty("text", QStringLiteral("0"));
        QVERIFY(!part("exportRun")->property("enabled").toBool());
        choose("srt", dir.filePath(QStringLiteral("fps.srt")));
        QVERIFY(part("exportRun")->property("enabled").toBool());
    }

    void aRelativePathIsRefused()
    {
        QVERIFY(!open("relative.ass").isEmpty());
        openDialog();
        choose("srt", QStringLiteral("relative.srt"));
        run();
        QTRY_VERIFY(part("exportProblem")->isVisible());
        QVERIFY(!dialog()->property("reported").toBool());
    }
};

QTEST_MAIN(ExportDialogTest)
#include "export_dialog_tests.moc"
