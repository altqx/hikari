// WS3 (#217): layout presets and the user's saved layouts replace legacy's
// View arrangements (GLOBAL_VIEW_ALL..GLOBAL_VIEW_SUBS) and the editor switch
// (GLOBAL_EDITOR). The layout switcher and View > Layout list the presets and
// the saved layouts; Save layout as…, Manage layouts… (rename, delete) and
// applying a layout change the current Document tab; saved layouts are global
// and persist across restarts. The retired commands are in no menu, have no
// name or default binding, and a stored binding is dropped with a notice.

#include "hikari/app/application.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/session_file.h"

#include "docking.h"
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

// The saved layouts' names as the file holds them.
QStringList namesInFile(const QString &path)
{
    QStringList names;
    for (const auto &v : QJsonDocument::fromJson(readAll(path)).object().value(QStringLiteral("layouts")).toArray())
        names << v.toObject().value(QStringLiteral("name")).toString();
    return names;
}

} // namespace

class SavedLayoutsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString settingsFile() const { return dir.filePath(QStringLiteral("settings.toml")); }
    QString layoutsFile() const { return dir.filePath(QStringLiteral("layouts.json")); }
    // The saved layout file's envelope (layout.json, beside layouts.json).
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
    // PU7: a part of Manage layouts' current row (a ListView delegate is no
    // QObject child of the window).
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
    // A Document open in the first tab (no dictionary here: a toast, NT1).
    void openEpisode()
    {
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("episode.ass")))));
    }
    // Save layout as… from View > Layout, typed into its dialog and saved.
    void saveAs(const QString &name)
    {
        trigger(QStringLiteral("viewLayoutSaveAs"));
        QObject *dialog = named("saveLayoutDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        // Modal to the main window: Save takes the arrangement it opened for.
        QCOMPARE(qobject_cast<QQuickWindow *>(dialog)->modality(), Qt::WindowModal);
        QVERIFY(named("saveLayoutName")->setProperty("text", name));
        QTRY_VERIFY(named("saveLayoutSave")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    // The saved layout `name`'s item in a LayoutMenu (prefix "viewLayout" or "switcher").
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
                                 "layouts.json", "layouts.json.unrestored", "LastSession.txt", "settings.toml", "hotkeys.toml", "state.json"}) {
            QFile::remove(dir.filePath(QLatin1String(name)));
            QDir().rmdir(dir.filePath(QLatin1String(name))); // a test's folder in a file's place
        }
    }

    // The View menu has no arrangements and the Subtitles menu no editor
    // switch; View > Layout and the switcher list the four presets, the
    // current one checked, and no saved layout yet (Manage layouts… off).
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
        // A preset from the switcher's menu: checked there and in View > Layout.
        trigger(QStringLiteral("switcherPresetTiming"));
        QTRY_VERIFY(!isOpen("videoDock"));
        QCOMPARE(switcherText(), QStringLiteral("Timing"));
        QVERIFY(named("viewLayoutPresetTiming")->property("checked").toBool());
        QVERIFY(!named("viewLayoutPresetEditing")->property("checked").toBool());
        stop();
    }

    // Save layout as… keeps the tab's arrangement under a name; another
    // layout replaces it, and choosing the saved layout brings it back. The
    // tab's layout is the saved one: the switcher names it, the menus check it.
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
        // A layout saved now is read in the menu's order too: accessibility
        // lists a menu's items in their parent's order (the native gate's
        // keyboard walk counts on it).
        for (const char *prefix : {"viewLayout", "switcher"}) {
            auto *saved = qobject_cast<QQuickItem *>(savedItem(prefix, QStringLiteral("Subs only")));
            auto *preset = qobject_cast<QQuickItem *>(named(QLatin1String(prefix) + QStringLiteral("PresetTypesetting")));
            auto *saveAs = qobject_cast<QQuickItem *>(named(QLatin1String(prefix) + QStringLiteral("SaveAs")));
            QVERIFY(saved && preset && saveAs);
            // As the menu shows: its list view lays the new item out.
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

        // The saved file names the layout the arrangement started from.
        stop();
        QString problem, preset, layout;
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(readAll(dir.filePath(QStringLiteral("layout.json"))), &problem,
                                                         &preset, &layout));
        QCOMPARE(layout, QStringLiteral("Subs only"));
    }

    // Saved layouts are global: every tab can use them, a tab keeps the one
    // it chose, and they persist across restarts.
    void savedLayoutsAreGlobalAndPersist()
    {
        start();
        close("videoDock");
        saveAs(QStringLiteral("No video"));
        application->addPage(); // a copy of the arrangement shown
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

    // Reset this layout goes back to the saved layout the tab started from,
    // or to its preset.
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

    // Manage layouts… renames and deletes. A tab whose arrangement started
    // from the layout follows the new name; once it is deleted the tab keeps
    // its arrangement under its preset's name. PU7 (#309): the row renames
    // in place and Delete asks first.
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
        QCOMPARE(dialog->modality(), Qt::WindowModal); // no layout chosen meanwhile
        QObject *list = named("manageLayoutsList");
        QCOMPARE(list->property("currentIndex").toInt(), 0); // the tab's layout
        // PU7: Rename turns the current row into its name field.
        QVERIFY(!inManage("manageLayoutsName"));
        QVERIFY(QMetaObject::invokeMethod(inManage("manageLayoutsRename"), "clicked"));
        QObject *name = nullptr;
        QTRY_VERIFY((name = inManage("manageLayoutsName")) != nullptr);
        QCOMPARE(name->property("text").toString(), QStringLiteral("First"));
        QTRY_VERIFY(name->property("activeFocus").toBool());
        QVERIFY(!inManage("manageLayoutsRename")->isVisible()); // the row's buttons give way to the field
        const auto type = [&](const QString &text) {
            QTest::keyClick(dialog, Qt::Key_A, Qt::ControlModifier);
            for (const QChar c : text) // (no keyClicks for a QWindow)
                QTest::keyClick(dialog, c.toLatin1());
            QTRY_COMPARE(name->property("text").toString(), text);
        };
        // A name in use, or a preset's, is refused with the reason, and
        // Enter leaves the field as it is.
        type(QStringLiteral("second"));
        QTRY_VERIFY(named("manageLayoutsProblem")->property("text").toString().contains(QStringLiteral("Second")));
        QTest::keyClick(dialog, Qt::Key_Return);
        QVERIFY(inManage("manageLayoutsName"));
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("First"), QStringLiteral("Second")}));
        type(QStringLiteral("Timing"));
        QTRY_VERIFY(!named("manageLayoutsProblem")->property("text").toString().isEmpty());
        // Esc puts the name back and keeps the window.
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

        // From the keyboard, as the dialog opens: F2 goes to the name, Return
        // renames; the list keeps the layout and Delete asks, then deletes.
        // Both are global hotkeys' keys too (F2: GLOBAL_SELECT_FROM_VIDEO),
        // live while a Document is open: the list takes them first.
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
        QVERIFY(dialog->property("visible").toBool()); // Return renamed: the dialog stays
        QTRY_VERIFY(list->property("activeFocus").toBool());
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        trigger(QStringLiteral("viewLayoutManage"));
        QTRY_VERIFY(dialog->property("opened").toBool());

        // Delete asks first: Cancel keeps the layout, Delete deletes it; the
        // tab keeps its arrangement, named by its preset now.
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
        QTRY_VERIFY(list->property("activeFocus").toBool()); // back from the question
        QTest::keyClick(dialog, Qt::Key_Delete);
        answer(QStringLiteral("lead"), "delete");
        QTRY_COMPARE(namesInFile(layoutsFile()), QStringList{QStringLiteral("Second")});
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(!isOpen("audioDock"));
        QVERIFY(dialog->property("visible").toBool()); // one is left
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(inManage("manageLayoutsDelete"), "clicked"));
        answer(QStringLiteral("Second"), "delete");
        QTRY_VERIFY(!dialog->property("visible").toBool()); // none is left
        QVERIFY(namesInFile(layoutsFile()).isEmpty());
        QVERIFY(!named("viewLayoutManage")->property("enabled").toBool());
        stop();
    }

    // The Reference tray is part of the Grid: a comparison opened in a layout
    // with the Grid closed leaves the tray closed; with the Grid, it shows.
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
        // A preset, Reset this layout and Restore the previous layout bring
        // back panels without the tray: it comes back with the Grid.
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

    // A layout, a preset or Reset that hides the Video panel pauses a
    // playing video (no transport shows it playing), and a panel with the
    // focus that it hides gives the focus to the Grid.
    void aHiddenVideoPausesAndTheFocusStaysShown()
    {
        start();
        openEpisode();
        auto &video = application->video();
        application->setAskForBadResolution(false); // PU6: the resolution question is a message box with the keyboard
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
        // A preset that keeps Video leaves it playing.
        trigger(QStringLiteral("viewLayoutPresetEditing"));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(video.play());
        trigger(QStringLiteral("viewLayoutPresetTranslation"));
        QTRY_VERIFY(!isOpen("audioDock"));
        QVERIFY(video.playing());
        video.pause();
        // Not now to TM1's question, a message box in its own window that
        // has the keyboard (PU2-translation-mode-off).
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

    // Restore the previous layout takes back the arrangement the backup
    // names too: the switcher and Reset this layout follow it.
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
        QVERIFY(layouts().restoreBackup()); // Editing, as it was before Foo
        QTRY_VERIFY(isOpen("audioDock"));
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(layouts().layout().isEmpty());
        QVERIFY(layouts().restoreBackup()); // Foo again (each restore saves: the backup is the other)
        QTRY_VERIFY(!isOpen("audioDock"));
        QCOMPARE(switcherText(), QStringLiteral("Foo"));
        QVERIFY(layouts().restoreBackup());
        QTRY_COMPARE(switcherText(), QStringLiteral("Editing"));
        trigger(QStringLiteral("viewLayoutReset"));
        QVERIFY(isOpen("audioDock")); // Editing's, not Foo's
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        // The same panels under another name (the backup is the saved file
        // without its layout): the saved file names the restored one too, as
        // the next start reads it.
        saveAs(QStringLiteral("Bar"));
        QJsonObject backup = layoutEnvelope();
        QCOMPARE(backup.take(QStringLiteral("layout")).toString(), QStringLiteral("Bar"));
        QVERIFY(writeAll(dir.filePath(QStringLiteral("layout.json.bak")), QJsonDocument(backup).toJson()));
        QVERIFY(layouts().restoreBackup());
        QTRY_COMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(layoutEnvelope().value(QStringLiteral("layout")).toString().isEmpty());
        stop();
    }

    // The panel layout's file not written (here its name is a folder's) after
    // a saved layout is chosen: the notice says so, and the next save writes
    // the layout and its origin once it can.
    void aPanelLayoutNotWrittenIsSaid()
    {
        start();
        auto &l = layouts();
        const QString layoutFile = dir.filePath(QStringLiteral("layout.json"));
        QFile::remove(layoutFile); // none yet, or the first frame's
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

    // Both files refused at start: each notice names its kept copy, side by
    // side. A layout chosen answers the panel layout's; the saved layouts'
    // stays until it is dismissed.
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

    // A layout the profile has not saved (a saved file or a Session from
    // another profile names it) is not the tab's origin: one saved later
    // under that name, from another tab, is not it, and Reset this layout
    // gives the tab's preset.
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
        constexpr std::uint64_t other = 999999; // a tab the Session gives
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

    // A saved layout under a preset's name in another shipped language
    // (Polish "Edycja": Editing) is not offered: a switch to that language
    // would list it twice. The file as it was is kept aside, and Save layout
    // as… refuses the name.
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

    // A change the layouts file cannot take (here the file's name is a
    // folder's) is undone with the notice: the saved layouts stay as the file
    // has them, so the change can be made again once the file can be written.
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
        QVERIFY2(l.notice().isEmpty(), qPrintable(l.notice())); // the write went through
        stop();

        // Nor is a file larger than the next start reads: the layouts would be lost.
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

    // The names Save layout as… and Manage layouts… refuse, and saving under
    // a name in use (in any case) replaces that layout.
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
        // The dialog starts with the tab's layout's name: saving replaces it.
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
        // A replacement under another case keeps the place and takes the name.
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

    // A saved layout this version cannot use is listed, disabled, says why,
    // and is written back as it was; a layouts file that cannot be read is
    // kept beside it with a notice.
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
        QCOMPARE(written[1].toObject(), newer); // as it was read
        stop();

        // Records the list cannot offer (not a record, no name, a name listed
        // before, a name Save layout as… refuses: a preset's, too long, a line
        // break) are not written away: the file as it was is kept aside, and
        // the other layouts are used.
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

        // A broken file, one without its list of layouts, or one too large
        // to read is kept aside whole.
        // (No raw strings with unbalanced braces here: moc misreads them.)
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

    // A layouts file or a saved layout file that is there but cannot be
    // read (here the name is a folder's) is neither used nor written over.
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

    // The retired commands: no name in the shortcut editor, no default
    // binding, a stored binding dropped with a notice as the hotkeys are
    // read (the list written without it), and the importer marks them retired
    // (settings_import_tests). Running one by its symbol does nothing.
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
        // The shortcut editor binds any command for the Audio window too: that
        // binding is in the audio list.
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
        // Given once: the next start reads the lists without them.
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
        // The editor switch's setting is gone from the settings file.
        QVERIFY(!readAll(settingsFile()).contains("editorOn"));
    }

    // A legacy Session tab saved in the player layout ("Editor: 0") opens
    // with the editor (there is no player layout); the Session is written
    // with "Editor: 1" for legacy.
    void aPlayerLayoutSessionTabOpensWithTheEditor()
    {
        start();
        application::SessionTab tab;
        tab.subtitles = QDir::toNativeSeparators(writeSubtitles(dir.filePath(QStringLiteral("p.ass")))).toStdString();
        tab.editor = false;
        const QString kls = dir.filePath(QStringLiteral("player.kls"));
        QVERIFY(writeAll(kls, QByteArray::fromStdString(application::writeSession("HikariSub v1", false, {tab}))));
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(kls));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        application->finishClose();
        QTRY_COMPARE(application->tabs().size(), 1);
        for (const char *shown : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(isOpen(shown), shown);
        QVERIFY(application->saveLastSession());
        const QByteArray written = readAll(application->lastSessionPath());
        QVERIFY2(written.contains("Editor: 1"), written.constData());
        QVERIFY(!written.contains("Editor: 0"));
        stop();
    }

    // The switcher, its menu, View > Layout and both dialogs in the Light and
    // Dark themes (HIKARI_SURFACE_SHOT_DIR, as shell_tests' surfaces).
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
        // A Popup is no item: its frame is its background's parent.
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

// Two processes, one per part (tests/ui/CMakeLists.txt PARTS).
HIKARI_QTEST_MAIN_IN_PARTS(SavedLayoutsTest)
#include "saved_layouts_tests.moc"
