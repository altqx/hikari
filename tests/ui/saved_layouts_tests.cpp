#include "hikari/app/application.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/session_file.h"

#include "docking.h"
#include "legacy_session.h"
#include "hikari/app/hotkeys_controller.h"
#include "log_controller.h"
#include "qtest_parts.h"
#include "settings_store.h"
#include "theme.h"
#include "video_controller.h"
#include "workspace_layout.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace hikari;

namespace {

QString writeSubtitles(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n");
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

QStringList namesInFile(const QString &path)
{
    QStringList names;
    for (const auto &v : QJsonDocument::fromJson(readAll(path)).object().value(QStringLiteral("layouts")).toArray())
        names << v.toObject().value(QStringLiteral("name")).toString();
    return names;
}

}

class SavedLayoutsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString settingsFile() const { return dir.filePath(QStringLiteral("settings.toml")); }
    QString layoutsFile() const { return dir.filePath(QStringLiteral("layouts.json")); }
    QJsonObject layoutEnvelope() const
    {
        return QJsonDocument::fromJson(readAll(dir.filePath(QStringLiteral("layout.json")))).object();
    }
    void start()
    {
        app::Application::Options options;
        options.settingsFile = settingsFile();
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
        if (application->currentTab() < 0)
            application->addPage();
    }
    void stop()
    {
        application->workspaceLayout().save();
        application->endSession();
        engine.reset();
        application.reset();
    }
    ui::WorkspaceLayoutController &layouts() { return application->workspaceLayout(); }
    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    QObject *named(const QString &name) const
    {
        if (auto *found = window()->findChild<QObject *>(name))
            return found;
        for (QObject *o : window()->findChildren<QObject *>())
            if (o->objectName() == name)
                return o;
        return nullptr;
    }
    QObject *named(const char *name) const { return named(QLatin1String(name)); }
    // A ListView delegate is no QObject child of the window.
    QQuickItem *inManage(const char *name) const
    {
        auto *manage = qobject_cast<QQuickWindow *>(named("manageLayoutsDialog"));
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == QLatin1String(name))
                return item;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = find(child))
                    return found;
            return nullptr;
        };
        return manage ? find(manage->contentItem()) : nullptr;
    }
    QObject *dock(const char *name) const { return named(name); }
    bool isOpen(const char *name) const { return dock(name)->property("isOpen").toBool(); }
    void close(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(dock(name), "close"));
        QTRY_VERIFY(!isOpen(name));
    }
    void openDock(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(dock(name), "open"));
        QTRY_VERIFY(isOpen(name));
    }
    void trigger(const QString &name)
    {
        QObject *item = named(name);
        QVERIFY2(item, qPrintable(name));
        QVERIFY2(item->property("enabled").toBool(), qPrintable(name));
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        QCoreApplication::processEvents();
    }
    QString switcherText() const { return named("layoutSwitcherLabel")->property("text").toString(); }
    void openEpisode()
    {
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("episode.ass")))));
    }
    void saveAs(const QString &name)
    {
        trigger(QStringLiteral("viewLayoutSaveAs"));
        QObject *dialog = named("saveLayoutDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(qobject_cast<QQuickWindow *>(dialog)->modality(), Qt::WindowModal);
        QVERIFY(named("saveLayoutName")->setProperty("text", name));
        QTRY_VERIFY(named("saveLayoutSave")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    QObject *savedItem(const char *prefix, const QString &name) const
    {
        for (int i = 0; QObject *item = named(QLatin1String(prefix) + QStringLiteral("Saved") + QString::number(i)); ++i)
            if (item->property("text").toString() == name)
                return item;
        return nullptr;
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak", "layout.json.unrestored", "layout.json.tab.unrestored",
                                 "layouts.json", "layouts.json.unrestored", "last-session.json", "settings.toml", "hotkeys.toml", "state.json"}) {
            QFile::remove(dir.filePath(QLatin1String(name)));
            QDir().rmdir(dir.filePath(QLatin1String(name)));
        }
    }

    void theMenusListTheLayouts()
    {
        start();
        for (const char *gone : {"viewAll", "viewVideoAndSubs", "viewAudioAndSubs", "viewOnlyVideo", "viewOnlySubtitles",
                                 "editorSwitchMenuItem", "layoutPresetMenu", "resetLayout"})
            QVERIFY2(!named(gone), gone);
        QVERIFY(named("viewLayoutMenu"));
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        for (const char *prefix : {"viewLayout", "switcher"}) {
            const QString p = QLatin1String(prefix);
            for (const char *preset : {"Editing", "Timing", "Translation", "Typesetting", "Karaoke"})
                QVERIFY2(named(p + QStringLiteral("Preset") + QLatin1String(preset)), preset);
            QVERIFY(named(p + QStringLiteral("PresetEditing"))->property("checked").toBool());
            QVERIFY(!named(p + QStringLiteral("PresetTiming"))->property("checked").toBool());
            QVERIFY(!named(p + QStringLiteral("Saved0")));
            QVERIFY(!named(p + QStringLiteral("Manage"))->property("enabled").toBool());
            QVERIFY(named(p + QStringLiteral("SaveAs"))->property("enabled").toBool());
        }
        trigger(QStringLiteral("switcherPresetTiming"));
        QTRY_VERIFY(!isOpen("videoDock"));
        QCOMPARE(switcherText(), QStringLiteral("Timing"));
        QVERIFY(named("viewLayoutPresetTiming")->property("checked").toBool());
        QVERIFY(!named("viewLayoutPresetEditing")->property("checked").toBool());
        stop();
    }

    void saveAndApplyALayout()
    {
        start();
        close("audioDock");
        saveAs(QStringLiteral("Subs only"));
        QCOMPARE(layouts().layout(), QStringLiteral("Subs only"));
        QCOMPARE(switcherText(), QStringLiteral("Subs only"));
        QCOMPARE(namesInFile(layoutsFile()), QStringList{QStringLiteral("Subs only")});
        QObject *item = savedItem("viewLayout", QStringLiteral("Subs only"));
        QVERIFY(item);
        QVERIFY(item->property("checked").toBool());
        QVERIFY(!named("viewLayoutPresetEditing")->property("checked").toBool());
        QVERIFY(named("viewLayoutManage")->property("enabled").toBool());
        for (const char *prefix : {"viewLayout", "switcher"}) {
            auto *saved = qobject_cast<QQuickItem *>(savedItem(prefix, QStringLiteral("Subs only")));
            auto *preset = qobject_cast<QQuickItem *>(named(QLatin1String(prefix) + QStringLiteral("PresetTypesetting")));
            auto *saveAs = qobject_cast<QQuickItem *>(named(QLatin1String(prefix) + QStringLiteral("SaveAs")));
            QVERIFY(saved && preset && saveAs);
            // QQuickListView lays a new delegate out lazily; the shown menu would have laid it out.
            for (QQuickItem *list = saved->parentItem(); list; list = list->parentItem())
                if (list->inherits("QQuickListView")) {
                    QVERIFY(QMetaObject::invokeMethod(list, "forceLayout"));
                    break;
                }
            QTRY_VERIFY(saved->parentItem() == saveAs->parentItem());
            const auto order = saveAs->parentItem()->childItems();
            QVERIFY2(order.indexOf(preset) < order.indexOf(saved) && order.indexOf(saved) < order.indexOf(saveAs), prefix);
        }

        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("audioDock"));
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(!item->property("checked").toBool());

        QVERIFY(QMetaObject::invokeMethod(savedItem("switcher", QStringLiteral("Subs only")), "triggered"));
        QTRY_VERIFY(!isOpen("audioDock"));
        for (const char *shown : {"videoDock", "editorDock", "gridDock"})
            QVERIFY2(isOpen(shown), shown);
        QCOMPARE(switcherText(), QStringLiteral("Subs only"));
        QVERIFY(savedItem("viewLayout", QStringLiteral("Subs only"))->property("checked").toBool());

        stop();
        QString problem, preset, layout;
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(readAll(dir.filePath(QStringLiteral("layout.json"))), &problem,
                                                         &preset, &layout));
        QCOMPARE(layout, QStringLiteral("Subs only"));
    }

    void savedLayoutsAreGlobalAndPersist()
    {
        start();
        close("videoDock");
        saveAs(QStringLiteral("No video"));
        application->addPage();
        QCOMPARE(application->currentTab(), 1);
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(QMetaObject::invokeMethod(savedItem("viewLayout", QStringLiteral("No video")), "triggered"));
        QTRY_VERIFY(!isOpen("videoDock"));
        trigger(QStringLiteral("viewLayoutPresetTypesetting"));
        application->selectTab(0);
        QTRY_VERIFY(!isOpen("videoDock"));
        QCOMPARE(switcherText(), QStringLiteral("No video"));
        stop();

        start();
        QCOMPARE(layouts().savedLayouts().size(), 1);
        QCOMPARE(layouts().savedLayouts().first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("No video"));
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(QMetaObject::invokeMethod(savedItem("viewLayout", QStringLiteral("No video")), "triggered"));
        QTRY_VERIFY(!isOpen("videoDock"));
        stop();
    }

    void resetGoesBackToTheTabsLayout()
    {
        start();
        close("audioDock");
        saveAs(QStringLiteral("Quiet"));
        openDock("audioDock");
        trigger(QStringLiteral("viewLayoutReset"));
        QTRY_VERIFY(!isOpen("audioDock"));
        QCOMPARE(switcherText(), QStringLiteral("Quiet"));
        trigger(QStringLiteral("viewLayoutPresetTiming"));
        QTRY_VERIFY(!isOpen("videoDock"));
        openDock("videoDock");
        trigger(QStringLiteral("switcherReset"));
        QTRY_VERIFY(!isOpen("videoDock"));
        QCOMPARE(switcherText(), QStringLiteral("Timing"));
        stop();
    }

    void renameAndDelete()
    {
        start();
        openEpisode();
        close("audioDock");
        saveAs(QStringLiteral("First"));
        trigger(QStringLiteral("viewLayoutPresetTypesetting"));
        saveAs(QStringLiteral("Second"));
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("First"), QStringLiteral("Second")}));
        QVERIFY(QMetaObject::invokeMethod(savedItem("viewLayout", QStringLiteral("First")), "triggered"));
        QTRY_COMPARE(switcherText(), QStringLiteral("First"));

        trigger(QStringLiteral("viewLayoutManage"));
        auto *dialog = qobject_cast<QQuickWindow *>(named("manageLayoutsDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialog->modality(), Qt::WindowModal);
        QObject *list = named("manageLayoutsList");
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        QVERIFY(!inManage("manageLayoutsName"));
        QVERIFY(QMetaObject::invokeMethod(inManage("manageLayoutsRename"), "clicked"));
        QObject *name = nullptr;
        QTRY_VERIFY((name = inManage("manageLayoutsName")) != nullptr);
        QCOMPARE(name->property("text").toString(), QStringLiteral("First"));
        QTRY_VERIFY(name->property("activeFocus").toBool());
        QVERIFY(!inManage("manageLayoutsRename")->isVisible());
        const auto type = [&](const QString &text) {
            QTest::keyClick(dialog, Qt::Key_A, Qt::ControlModifier);
            for (const QChar c : text) // QTest has no keyClicks for a QWindow
                QTest::keyClick(dialog, c.toLatin1());
            QTRY_COMPARE(name->property("text").toString(), text);
        };
        type(QStringLiteral("second"));
        QTRY_VERIFY(named("manageLayoutsProblem")->property("text").toString().contains(QStringLiteral("Second")));
        QTest::keyClick(dialog, Qt::Key_Return);
        QVERIFY(inManage("manageLayoutsName"));
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("First"), QStringLiteral("Second")}));
        type(QStringLiteral("Timing"));
        QTRY_VERIFY(!named("manageLayoutsProblem")->property("text").toString().isEmpty());
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!inManage("manageLayoutsName"));
        QVERIFY(dialog->isVisible());
        QVERIFY(named("manageLayoutsProblem")->property("text").toString().isEmpty());
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("First"), QStringLiteral("Second")}));
        QVERIFY(QMetaObject::invokeMethod(inManage("manageLayoutsRename"), "clicked"));
        QTRY_VERIFY((name = inManage("manageLayoutsName")) != nullptr);
        type(QStringLiteral("  Main  "));
        QTRY_VERIFY(named("manageLayoutsProblem")->property("text").toString().isEmpty());
        QTest::keyClick(dialog, Qt::Key_Return);
        QTRY_COMPARE(switcherText(), QStringLiteral("Main"));
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("Main"), QStringLiteral("Second")}));
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        QTRY_VERIFY(!inManage("manageLayoutsName"));
        QVERIFY(savedItem("switcher", QStringLiteral("Main"))->property("checked").toBool());

        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        trigger(QStringLiteral("viewLayoutManage"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(list->property("activeFocus").toBool());
        QTest::keyClick(dialog, Qt::Key_F2);
        QTRY_VERIFY((name = inManage("manageLayoutsName")) != nullptr);
        QTRY_VERIFY(name->property("activeFocus").toBool());
        QTest::keyClick(dialog, Qt::Key_A, Qt::ControlModifier);
        for (const char c : {'l', 'e', 'a', 'd'})
            QTest::keyClick(dialog, c);
        QTest::keyClick(dialog, Qt::Key_Return);
        QTRY_COMPARE(switcherText(), QStringLiteral("lead"));
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("lead"), QStringLiteral("Second")}));
        QVERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY(list->property("activeFocus").toBool());
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        trigger(QStringLiteral("viewLayoutManage"));
        QTRY_VERIFY(dialog->property("opened").toBool());

        const auto deleteBox = [&] {
            auto *host = named("manageLayoutsMessages");
            return host ? qvariant_cast<QQuickWindow *>(host->property("box")) : nullptr;
        };
        const auto answer = [&](const QString &layout, const char *choice) {
            QQuickWindow *box = nullptr;
            QTRY_VERIFY((box = deleteBox()) && box->isVisible());
            QCOMPARE(box->title(), QStringLiteral("Delete layout"));
            QCOMPARE(box->property("text").toString(), QStringLiteral("Delete the layout “%1”?").arg(layout));
            QVERIFY(QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, QString::fromLatin1(choice))));
            QTRY_VERIFY(!box->isVisible());
        };
        QTRY_VERIFY(list->property("activeFocus").toBool());
        QTest::keyClick(dialog, Qt::Key_Delete);
        answer(QStringLiteral("lead"), "cancel");
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("lead"), QStringLiteral("Second")}));
        QTRY_VERIFY(list->property("activeFocus").toBool());
        QTest::keyClick(dialog, Qt::Key_Delete);
        answer(QStringLiteral("lead"), "delete");
        QTRY_COMPARE(namesInFile(layoutsFile()), QStringList{QStringLiteral("Second")});
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(!isOpen("audioDock"));
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(inManage("manageLayoutsDelete"), "clicked"));
        answer(QStringLiteral("Second"), "delete");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(namesInFile(layoutsFile()).isEmpty());
        QVERIFY(!named("viewLayoutManage")->property("enabled").toBool());
        stop();
    }

    void aReferenceFollowsTheGrid()
    {
        start();
        openEpisode();
        close("gridDock");
        saveAs(QStringLiteral("No grid"));
        QVERIFY(application->openReference(writeSubtitles(dir.filePath(QStringLiteral("ref.ass")))));
        QTRY_VERIFY(application->shell().hasReference());
        QTest::qWait(300);
        QVERIFY(!isOpen("referenceDock"));
        QVERIFY(!isOpen("gridDock"));
        QCOMPARE(switcherText(), QStringLiteral("No grid"));
        stop();

        QVERIFY(QFile::remove(dir.filePath(QStringLiteral("layout.json"))));
        start();
        openEpisode();
        QVERIFY(isOpen("gridDock"));
        QVERIFY(application->openReference(writeSubtitles(dir.filePath(QStringLiteral("ref.ass")))));
        QTRY_VERIFY(isOpen("referenceDock"));
        for (const QString &step : {QStringLiteral("viewLayoutPresetTiming"), QStringLiteral("viewLayoutReset"),
                                    QStringLiteral("backup")}) {
            if (step == QLatin1String("backup"))
                QVERIFY(layouts().restoreBackup());
            else
                trigger(step);
            QTest::qWait(200);
            QVERIFY2(isOpen("gridDock"), qPrintable(step));
            QVERIFY2(isOpen("referenceDock"), qPrintable(step));
        }
        stop();
    }

    void aHiddenVideoPausesAndTheFocusStaysShown()
    {
        start();
        openEpisode();
        auto &video = application->video();
        application->setAskForBadResolution(false);
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.session().lastFrame() != nullptr, 20000);
        close("videoDock");
        saveAs(QStringLiteral("No video"));
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(video.play());
        QVERIFY(QMetaObject::invokeMethod(savedItem("viewLayout", QStringLiteral("No video")), "triggered"));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(!video.playing());
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(video.play());
        trigger(QStringLiteral("viewLayoutPresetTiming"));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(!video.playing());
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(video.play());
        trigger(QStringLiteral("viewLayoutPresetTranslation"));
        QTRY_VERIFY(!isOpen("audioDock"));
        QVERIFY(video.playing());
        video.pause();
        auto *question = qobject_cast<QQuickWindow *>(named("translationModeQuestion"));
        QVERIFY(question);
        QTRY_VERIFY(question->isVisible());
        QVERIFY(QMetaObject::invokeMethod(question, "reject"));
        QTRY_VERIFY(!question->isVisible());
        QTRY_VERIFY(window()->isActive());

        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("audioDock"));
        auto *audioPanel = qobject_cast<QQuickItem *>(named("audioPanel"));
        auto *gridPanel = qobject_cast<QQuickItem *>(named("gridPanel"));
        QVERIFY(audioPanel && gridPanel);
        audioPanel->forceActiveFocus();
        QTRY_VERIFY(audioPanel->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(window(), "applyPreset", Q_ARG(QVariant, QStringLiteral("Typesetting"))));
        QTRY_VERIFY(!isOpen("audioDock"));
        QTRY_VERIFY(gridPanel->hasActiveFocus());
        stop();
    }

    void theBackupNamesItsLayout()
    {
        start();
        close("audioDock");
        saveAs(QStringLiteral("Foo"));
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("audioDock"));
        QVERIFY(QMetaObject::invokeMethod(savedItem("viewLayout", QStringLiteral("Foo")), "triggered"));
        QTRY_COMPARE(switcherText(), QStringLiteral("Foo"));
        QVERIFY(!isOpen("audioDock"));
        QVERIFY(layouts().restoreBackup());
        QTRY_VERIFY(isOpen("audioDock"));
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(layouts().layout().isEmpty());
        QVERIFY(layouts().restoreBackup());
        QTRY_VERIFY(!isOpen("audioDock"));
        QCOMPARE(switcherText(), QStringLiteral("Foo"));
        QVERIFY(layouts().restoreBackup());
        QTRY_COMPARE(switcherText(), QStringLiteral("Editing"));
        trigger(QStringLiteral("viewLayoutReset"));
        QVERIFY(isOpen("audioDock"));
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        saveAs(QStringLiteral("Bar"));
        QJsonObject backup = layoutEnvelope();
        QCOMPARE(backup.take(QStringLiteral("layout")).toString(), QStringLiteral("Bar"));
        QVERIFY(writeAll(dir.filePath(QStringLiteral("layout.json.bak")), QJsonDocument(backup).toJson()));
        QVERIFY(layouts().restoreBackup());
        QTRY_COMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(layoutEnvelope().value(QStringLiteral("layout")).toString().isEmpty());
        stop();
    }

    void aPanelLayoutNotWrittenIsSaid()
    {
        start();
        auto &l = layouts();
        const QString layoutFile = dir.filePath(QStringLiteral("layout.json"));
        QFile::remove(layoutFile);
        QVERIFY(QDir().mkdir(layoutFile));
        close("audioDock");
        saveAs(QStringLiteral("Quiet"));
        QVERIFY2(l.notice().contains(QStringLiteral("panel layout could not be written")), qPrintable(l.notice()));
        QCOMPARE(namesInFile(layoutsFile()), QStringList{QStringLiteral("Quiet")});
        QVERIFY(QDir().rmdir(layoutFile));
        QVERIFY(l.save());
        QVERIFY2(!l.notice().contains(QStringLiteral("panel layout could not be written")), qPrintable(l.notice()));
        QCOMPARE(layoutEnvelope().value(QStringLiteral("layout")).toString(), QStringLiteral("Quiet"));
        stop();
    }

    void bothRecoveryNoticesAreShown()
    {
        start();
        saveAs(QStringLiteral("Mine"));
        stop();
        QVERIFY(writeAll(layoutsFile(), QByteArray("{not json")));
        QVERIFY(writeAll(dir.filePath(QStringLiteral("layout.json")), QByteArray("{not json")));
        start();
        auto &l = layouts();
        QVERIFY2(l.notice().contains(QStringLiteral("layouts.json.unrestored")), qPrintable(l.notice()));
        QVERIFY2(l.notice().contains(QStringLiteral("layout.json.unrestored")), qPrintable(l.notice()));
        QCOMPARE(named("layoutNoticeText")->property("text").toString(), l.notice());
        trigger(QStringLiteral("viewLayoutPresetTiming"));
        QVERIFY2(l.notice().contains(QStringLiteral("layouts.json.unrestored")), qPrintable(l.notice()));
        QVERIFY2(!l.notice().contains(QStringLiteral("panel layout")), qPrintable(l.notice()));
        l.dismissNotice();
        QVERIFY(l.notice().isEmpty());
        stop();
    }

    void aMissingLayoutIsNoOrigin()
    {
        start();
        stop();
        QJsonObject envelope = layoutEnvelope();
        envelope.insert(QStringLiteral("layout"), QStringLiteral("Ghost"));
        QVERIFY(writeAll(dir.filePath(QStringLiteral("layout.json")), QJsonDocument(envelope).toJson()));
        start();
        auto &l = layouts();
        QVERIFY(l.layout().isEmpty());
        constexpr std::uint64_t other = 999999;
        l.setTabArrangement(other, ui::WorkspaceArrangement{QStringLiteral("Timing"), {}, QStringLiteral("Ghost")}.serialise());
        QVERIFY(l.tabArrangement(other));
        QVERIFY(l.tabArrangement(other)->layout.isEmpty());
        application->addPage();
        trigger(QStringLiteral("viewLayoutPresetTypesetting"));
        saveAs(QStringLiteral("Ghost"));
        QVERIFY(l.tabArrangement(other)->layout.isEmpty());
        application->selectTab(0);
        QTRY_COMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(l.layout().isEmpty());
        stop();
    }

    void presetNamesInOtherLanguagesAreRefused()
    {
        start();
        saveAs(QStringLiteral("Mine"));
        QCOMPARE(layouts().layoutNameProblem(QStringLiteral("edycja")), QStringLiteral("Edycja is a built-in layout."));
        QVERIFY(!layouts().saveLayoutAs(QStringLiteral("EDYCJA")));
        stop();
        QJsonObject file = QJsonDocument::fromJson(readAll(layoutsFile())).object();
        QJsonArray list = file.value(QStringLiteral("layouts")).toArray();
        QJsonObject polish = list.first().toObject();
        polish.insert(QStringLiteral("name"), QStringLiteral("Edycja"));
        list.append(polish);
        file.insert(QStringLiteral("layouts"), list);
        const QByteArray before = QJsonDocument(file).toJson();
        QVERIFY(writeAll(layoutsFile(), before));
        start();
        auto &l = layouts();
        QCOMPARE(l.savedLayouts().size(), 1);
        QCOMPARE(l.savedLayouts().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Mine"));
        QVERIFY2(l.notice().contains(QStringLiteral("layouts.json.unrestored")), qPrintable(l.notice()));
        QCOMPARE(readAll(layoutsFile() + QStringLiteral(".unrestored")), before);
        QVERIFY(!savedItem("viewLayout", QStringLiteral("Edycja")));
        stop();
    }

    void aChangeNotWrittenIsUndone()
    {
        start();
        auto &l = layouts();
        saveAs(QStringLiteral("First"));
        QTRY_COMPARE(switcherText(), QStringLiteral("First"));
        QVERIFY(QFile::remove(layoutsFile()));
        QVERIFY(QDir().mkdir(layoutsFile()));
        QVERIFY(!l.saveLayoutAs(QStringLiteral("Second")));
        QVERIFY2(l.notice().contains(QStringLiteral("could not be written")), qPrintable(l.notice()));
        QCOMPARE(l.savedLayouts().size(), 1);
        QCOMPARE(switcherText(), QStringLiteral("First"));
        QVERIFY(!l.renameSavedLayout(QStringLiteral("First"), QStringLiteral("Other")));
        QCOMPARE(l.savedLayoutNamed(QStringLiteral("First")), QStringLiteral("First"));
        QCOMPARE(switcherText(), QStringLiteral("First"));
        QVERIFY(!l.deleteSavedLayout(QStringLiteral("First")));
        QCOMPARE(l.savedLayouts().size(), 1);
        QCOMPARE(switcherText(), QStringLiteral("First"));
        QVERIFY(named("viewLayoutManage")->property("enabled").toBool());
        QVERIFY(QDir().rmdir(layoutsFile()));
        QVERIFY(l.deleteSavedLayout(QStringLiteral("First")));
        QVERIFY(namesInFile(layoutsFile()).isEmpty());
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY2(l.notice().isEmpty(), qPrintable(l.notice()));
        stop();

        QJsonObject padding{{QStringLiteral("name"), QStringLiteral("Padding")},
                            {QStringLiteral("arrangement"),
                             QJsonObject{{QStringLiteral("pad"),
                                          QString(ui::WorkspaceLayoutController::kMaximumBytes - 20000, QLatin1Char('x'))}}}};
        const QByteArray nearlyFull = QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1},
                                                                {QStringLiteral("layouts"), QJsonArray{padding}}})
                                          .toJson(QJsonDocument::Indented);
        QVERIFY(writeAll(layoutsFile(), nearlyFull));
        start();
        QCOMPARE(layouts().savedLayouts().size(), 1);
        QVERIFY(!layouts().saveLayoutAs(QStringLiteral("Big")));
        QVERIFY2(layouts().notice().contains(QStringLiteral("larger than")), qPrintable(layouts().notice()));
        QCOMPARE(layouts().savedLayouts().size(), 1);
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QCOMPARE(readAll(layoutsFile()), nearlyFull);
        QVERIFY(layouts().deleteSavedLayout(QStringLiteral("Padding")));
        QVERIFY(layouts().saveLayoutAs(QStringLiteral("Big")));
        stop();
    }

    void namesAndReplacing()
    {
        start();
        auto &l = layouts();
        QVERIFY(!l.layoutNameProblem(QString()).isEmpty());
        QVERIFY(!l.layoutNameProblem(QStringLiteral("   ")).isEmpty());
        QVERIFY(!l.layoutNameProblem(QStringLiteral("editing")).isEmpty());
        QVERIFY(!l.layoutNameProblem(QStringLiteral("TYPESETTING")).isEmpty());
        QVERIFY(!l.layoutNameProblem(QString(65, QLatin1Char('x'))).isEmpty());
        QVERIFY(l.layoutNameProblem(QString(64, QLatin1Char('x'))).isEmpty());
        QVERIFY(!l.layoutNameProblem(QStringLiteral("two\nlines")).isEmpty());
        QVERIFY(!l.layoutNameProblem(QStringLiteral("a\ttab")).isEmpty());
        QVERIFY(l.layoutNameProblem(QStringLiteral("Mine")).isEmpty());
        QVERIFY(!l.saveLayoutAs(QStringLiteral("Editing")));
        QVERIFY(l.savedLayouts().isEmpty());

        close("audioDock");
        saveAs(QStringLiteral("Mine"));
        trigger(QStringLiteral("viewLayoutSaveAs"));
        QObject *dialog = named("saveLayoutDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(named("saveLayoutName")->property("text").toString(), QStringLiteral("Mine"));
        QCOMPARE(named("saveLayoutSave")->property("text").toString(), QStringLiteral("Replace"));
        QVERIFY(named("saveLayoutNote")->property("text").toString().contains(QStringLiteral("Mine")));
        QVERIFY(named("saveLayoutName")->setProperty("text", QStringLiteral("Typesetting")));
        QTRY_VERIFY(!named("saveLayoutSave")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        openDock("audioDock");
        close("videoDock");
        saveAs(QStringLiteral("MINE"));
        QCOMPARE(namesInFile(layoutsFile()), QStringList{QStringLiteral("MINE")});
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(QMetaObject::invokeMethod(savedItem("viewLayout", QStringLiteral("MINE")), "triggered"));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isOpen("audioDock"));
        stop();
    }

    void unusableLayoutsAreKept()
    {
        start();
        saveAs(QStringLiteral("Good"));
        stop();
        QJsonObject file = QJsonDocument::fromJson(readAll(layoutsFile())).object();
        QJsonArray list = file.value(QStringLiteral("layouts")).toArray();
        QJsonObject newer = list.first().toObject();
        QJsonObject arrangement = newer.value(QStringLiteral("arrangement")).toObject();
        arrangement.insert(QStringLiteral("engine"), QStringLiteral("KDDockWidgets 9.0"));
        newer.insert(QStringLiteral("name"), QStringLiteral("From a newer version"));
        newer.insert(QStringLiteral("arrangement"), arrangement);
        list.append(newer);
        file.insert(QStringLiteral("layouts"), list);
        QVERIFY(writeAll(layoutsFile(), QJsonDocument(file).toJson()));

        start();
        const QVariantList saved = layouts().savedLayouts();
        QCOMPARE(saved.size(), 2);
        QVERIFY(saved[0].toMap().value(QStringLiteral("usable")).toBool());
        QVERIFY(!saved[1].toMap().value(QStringLiteral("usable")).toBool());
        QVERIFY(!saved[1].toMap().value(QStringLiteral("problem")).toString().isEmpty());
        QObject *item = savedItem("viewLayout", QStringLiteral("From a newer version"));
        QVERIFY(item);
        QVERIFY(!item->property("enabled").toBool());
        QVERIFY(!layouts().applySavedLayout(QStringLiteral("From a newer version")));
        saveAs(QStringLiteral("Another"));
        const QJsonArray written = QJsonDocument::fromJson(readAll(layoutsFile())).object().value(QStringLiteral("layouts")).toArray();
        QCOMPARE(written.size(), 3);
        QCOMPARE(written[1].toObject(), newer);
        stop();

        const QJsonValue usable = written[0].toObject().value(QStringLiteral("arrangement"));
        const auto named = [&](const QString &name) {
            return QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("arrangement"), usable}};
        };
        QJsonArray odd{written[0], QJsonObject{{QStringLiteral("arrangement"), usable}}, written[0], 42,
                       named(QStringLiteral("timing")), named(QString(65, QLatin1Char('n'))),
                       named(QStringLiteral("two\nlines"))};
        const QByteArray withOdd = QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1}, {QStringLiteral("layouts"), odd}}).toJson();
        QVERIFY(writeAll(layoutsFile(), withOdd));
        start();
        QCOMPARE(layouts().savedLayouts().size(), 1);
        QVERIFY2(layouts().notice().contains(QStringLiteral("layouts.json.unrestored")), qPrintable(layouts().notice()));
        QCOMPARE(readAll(layoutsFile() + QStringLiteral(".unrestored")), withOdd);
        saveAs(QStringLiteral("Odd ones kept"));
        QCOMPARE(readAll(layoutsFile() + QStringLiteral(".unrestored")), withOdd);
        stop();

        // No raw strings with unbalanced braces here: moc misreads them.
        const QByteArray tooLarge = QByteArray("{\"schema\": 1, \"layouts\": [], \"pad\": \"")
                                    + QByteArray(ui::WorkspaceLayoutController::kMaximumBytes + 100, 'x') + "\"}";
        for (const QByteArray &broken : {QByteArray("{not json"), QByteArray(R"({"schema": 1, "layouts": {}})"),
                                         QByteArray(R"({"schema": 1})"), tooLarge}) {
            QVERIFY(writeAll(layoutsFile(), broken));
            start();
            QVERIFY(layouts().savedLayouts().isEmpty());
            QVERIFY2(layouts().notice().contains(QStringLiteral("layouts.json.unrestored")), qPrintable(layouts().notice()));
            QCOMPARE(readAll(layoutsFile() + QStringLiteral(".unrestored")), broken);
            stop();
        }
    }

    void unreadableFilesAreNotWrittenOver()
    {
        QVERIFY(QDir().mkdir(layoutsFile()));
        start();
        QVERIFY2(layouts().notice().contains(QStringLiteral("could not be read or copied")), qPrintable(layouts().notice()));
        QVERIFY(layouts().savedLayouts().isEmpty());
        QVERIFY(!layouts().saveLayoutAs(QStringLiteral("First")));
        QVERIFY(layouts().savedLayouts().isEmpty());
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        stop();
        QVERIFY(QFileInfo(layoutsFile()).isDir());
        QVERIFY(QDir().rmdir(layoutsFile()));

        const QString layoutFile = dir.filePath(QStringLiteral("layout.json"));
        QFile::remove(layoutFile);
        QVERIFY(QDir().mkdir(layoutFile));
        start();
        QVERIFY2(layouts().notice().contains(QStringLiteral("could not be read or copied")), qPrintable(layouts().notice()));
        close("audioDock");
        QVERIFY(!layouts().save());
        stop();
        QVERIFY(QFileInfo(layoutFile).isDir());
        QVERIFY(QDir().rmdir(layoutFile));
    }

    void retiredCommandsAreGone()
    {
        const QStringList retired{QStringLiteral("GLOBAL_VIEW_ALL"), QStringLiteral("GLOBAL_VIEW_VIDEO"),
                                  QStringLiteral("GLOBAL_VIEW_AUDIO"), QStringLiteral("GLOBAL_VIEW_ONLY_VIDEO"),
                                  QStringLiteral("GLOBAL_VIEW_SUBS"), QStringLiteral("GLOBAL_EDITOR")};
        application::HotkeyMap defaults;
        application::loadDefaultHotkeys(defaults, false);
        for (const QString &symbol : retired) {
            const int id = application::hotkeyIdOf(symbol.toStdString());
            QVERIFY2(id > 0, qPrintable(symbol));
            QVERIFY2(application::isRetiredHotkey(id), qPrintable(symbol));
            QVERIFY2(application::hotkeyName(id).empty(), qPrintable(symbol));
            QVERIFY2(!defaults.contains(application::HotkeyId{id, application::GlobalHotkey}), qPrintable(symbol));
        }
        start();
        QStringList lines = application->settingsStore()->list("shortcuts.hotkeys");
        lines << QStringLiteral("GLOBAL_VIEW_ALL G=Ctrl-Alt-1") << QStringLiteral("GLOBAL_EDITOR G=Ctrl-E");
        application->settingsStore()->set("shortcuts.hotkeys", lines);
        QStringList audio = application->settingsStore()->list("shortcuts.audioHotkeys");
        audio << QStringLiteral("GLOBAL_VIEW_SUBS A=Ctrl-Alt-5");
        application->settingsStore()->set("shortcuts.audioHotkeys", audio);
        stop();
        start();
        const QString log = application->log().history();
        QVERIFY2(log.contains(QStringLiteral("The shortcut Ctrl-Alt-1 of \"View all\" was removed")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("The shortcut Ctrl-E of \"Enable / Disable editor\" was removed")),
                 qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("The shortcut Ctrl-Alt-5 of \"View only subtitles\" was removed")),
                 qPrintable(log));
        for (const char *list : {"shortcuts.hotkeys", "shortcuts.audioHotkeys"})
            for (const QString &line : application->settingsStore()->list(list))
                QVERIFY2(!line.startsWith(QLatin1String("GLOBAL_VIEW_")) && !line.startsWith(QLatin1String("GLOBAL_EDITOR ")),
                         qPrintable(line));
        stop();
        start();
        QVERIFY2(!application->log().history().contains(QStringLiteral("was removed")),
                 qPrintable(application->log().history()));
        QVERIFY(application->hotkeys().accelOf(QStringLiteral("GLOBAL_EDITOR"), 0).isEmpty());
        for (const QString &symbol : retired) {
            QVariant ran;
            QVERIFY(QMetaObject::invokeMethod(window(), "runGlobalHotkey", Q_RETURN_ARG(QVariant, ran),
                                              Q_ARG(QVariant, symbol)));
            QVERIFY2(!ran.toBool(), qPrintable(symbol));
        }
        for (const char *shown : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(isOpen(shown), shown);
        stop();
        QVERIFY(!readAll(settingsFile()).contains("editorOn"));
    }

    void aPlayerLayoutSessionTabOpensWithTheEditor()
    {
        start();
        application::SessionTab tab;
        tab.subtitles = QDir::toNativeSeparators(writeSubtitles(dir.filePath(QStringLiteral("p.ass")))).toStdString();
        const QString kls = dir.filePath(QStringLiteral("player.kls"));
        QByteArray legacy = QByteArray::fromStdString(testing::writeLegacySession("HikariSub v1", false, {tab}));
        legacy.replace("Editor: 1", "Editor: 0");
        QVERIFY(writeAll(kls, legacy));
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(kls));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        application->finishClose();
        QTRY_COMPARE(application->tabs().size(), 1);
        for (const char *shown : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(isOpen(shown), shown);
        QVERIFY(application->saveLastSession());
        const QByteArray written = readAll(application->lastSessionPath());
        QVERIFY2(written.startsWith("{"), written.constData());
        const QJsonObject writtenTab =
            QJsonDocument::fromJson(written).object().value(QStringLiteral("tabs")).toArray().at(0).toObject();
        QVERIFY(writtenTab.contains(QStringLiteral("subtitles")));
        QVERIFY(!writtenTab.contains(QStringLiteral("editor")));
        stop();
    }

    void surfaceScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        window()->resize(1280, 800);
        openEpisode();
        close("audioDock");
        QVERIFY(layouts().saveLayoutAs(QStringLiteral("Subtitles only")));
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QVERIFY(layouts().saveLayoutAs(QStringLiteral("Review with video")));
        const auto setTheme = [&](const char *code) {
            auto &settings = *application->settingsStore();
            settings.setValue(QStringLiteral("appearance.followSystem"), false);
            settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(code));
            auto *controls = window()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
        };
        const auto grab = [&](QQuickItem *item, const QString &file, int margin) {
            QTest::qWait(300);
            const QImage shot = item->window()->grabWindow();
            const qreal dpr = item->window()->effectiveDevicePixelRatio();
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            const QRect crop = QRectF(r.topLeft() * dpr, r.size() * dpr).toAlignedRect()
                                   .adjusted(-margin, -margin, margin, margin)
                                   .intersected(shot.rect());
            QVERIFY(shot.copy(crop).save(file));
        };
        // A Popup is no QQuickItem: its frame is its background's parent.
        const auto frameOf = [](QObject *popup) {
            auto *background = popup->property("background").value<QQuickItem *>();
            return background ? background->parentItem() : nullptr;
        };
        auto *switcher = qobject_cast<QQuickItem *>(named("layoutSwitcher"));
        for (const char *code : {"light", "dark"}) {
            setTheme(code);
            const QString suffix = QLatin1Char('-') + QLatin1String(code) + QStringLiteral(".png");
            QTest::qWait(300);
            QVERIFY(window()->grabWindow().save(out + QStringLiteral("/window") + suffix));
            grab(qobject_cast<QQuickItem *>(named("statusBar")), out + QStringLiteral("/status-bar") + suffix, 0);
            QVERIFY(QMetaObject::invokeMethod(switcher, "openMenu"));
            QObject *menu = named("layoutSwitcherMenu");
            QTRY_VERIFY(menu->property("opened").toBool());
            QTest::qWait(300);
            QVERIFY(window()->grabWindow().save(out + QStringLiteral("/switcher-menu") + suffix));
            QMetaObject::invokeMethod(menu, "close");
            QTRY_VERIFY(!menu->property("visible").toBool());
            QObject *view = named("viewMenu");
            QVERIFY(QMetaObject::invokeMethod(view, "popup", Q_ARG(QQuickItem *, window()->contentItem()),
                                              Q_ARG(QPointF, QPointF(200, 0))));
            QTRY_VERIFY(view->property("opened").toBool());
            QVERIFY(frameOf(view));
            grab(frameOf(view), out + QStringLiteral("/view-menu") + suffix, 4);
            QMetaObject::invokeMethod(view, "close");
            QTRY_VERIFY(!view->property("visible").toBool());
            trigger(QStringLiteral("viewLayoutSaveAs"));
            QObject *save = named("saveLayoutDialog");
            QTRY_VERIFY(save->property("opened").toBool());
            QVERIFY(named("saveLayoutName")->setProperty("text", QStringLiteral("Subtitles only")));
            QTest::qWait(300);
            QVERIFY(qobject_cast<QQuickWindow *>(save)->grabWindow().save(out + QStringLiteral("/save-layout") + suffix));
            QMetaObject::invokeMethod(save, "reject");
            QTRY_VERIFY(!save->property("visible").toBool());
            trigger(QStringLiteral("viewLayoutManage"));
            QObject *manage = named("manageLayoutsDialog");
            QTRY_VERIFY(manage->property("opened").toBool());
            QTest::qWait(300);
            QVERIFY(qobject_cast<QQuickWindow *>(manage)->grabWindow().save(out + QStringLiteral("/manage-layouts") + suffix));
            QMetaObject::invokeMethod(manage, "close");
            QTRY_VERIFY(!manage->property("visible").toBool());
        }
        setTheme("light");
        stop();
    }
};

HIKARI_QTEST_MAIN_IN_PARTS(SavedLayoutsTest)
#include "saved_layouts_tests.moc"
