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
#include "workspace_layout.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

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

    QString settingsFile() const { return dir.filePath(QStringLiteral("hikari.ini")); }
    QString layoutsFile() const { return dir.filePath(QStringLiteral("layouts.json")); }
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
    // A Document open in the first tab. No dictionary here: the spelling
    // notice is closed.
    void openEpisode()
    {
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("episode.ass")))));
        QObject *spelling = named("spellingNotice");
        if (spelling && QTest::qWaitFor([&] { return spelling->property("visible").toBool(); }, 2000)) {
            QMetaObject::invokeMethod(spelling, "close");
            QTRY_VERIFY(!spelling->property("visible").toBool());
        }
    }
    // Save layout as… from View > Layout, typed into its dialog and saved.
    void saveAs(const QString &name)
    {
        trigger(QStringLiteral("viewLayoutSaveAs"));
        QObject *dialog = named("saveLayoutDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
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
                                 "layouts.json", "layouts.json.unrestored", "LastSession.txt", "hikari.ini"})
            QFile::remove(dir.filePath(QLatin1String(name)));
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
            for (const char *preset : {"Editing", "Timing", "Translation", "Typesetting"})
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
    // its arrangement under its preset's name.
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
        QObject *dialog = named("manageLayoutsDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QObject *list = named("manageLayoutsList");
        QCOMPARE(list->property("currentIndex").toInt(), 0); // the tab's layout
        QObject *name = named("manageLayoutsName");
        QCOMPARE(name->property("text").toString(), QStringLiteral("First"));
        QVERIFY(!named("manageLayoutsRename")->property("enabled").toBool()); // nothing to rename yet
        // A name in use, or a preset's, is refused with the reason.
        QVERIFY(name->setProperty("text", QStringLiteral("second")));
        QTRY_VERIFY(!named("manageLayoutsRename")->property("enabled").toBool());
        QVERIFY(named("manageLayoutsProblem")->property("text").toString().contains(QStringLiteral("Second")));
        QVERIFY(name->setProperty("text", QStringLiteral("Timing")));
        QTRY_VERIFY(!named("manageLayoutsRename")->property("enabled").toBool());
        QVERIFY(name->setProperty("text", QStringLiteral("  Main  ")));
        QTRY_VERIFY(named("manageLayoutsRename")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("manageLayoutsRename"), "clicked"));
        QTRY_COMPARE(switcherText(), QStringLiteral("Main"));
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("Main"), QStringLiteral("Second")}));
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        QVERIFY(savedItem("switcher", QStringLiteral("Main"))->property("checked").toBool());

        // From the keyboard, as the dialog opens: F2 goes to the name, Return
        // renames; the list keeps the layout and Delete deletes from there.
        // Both are global hotkeys' keys too (F2: GLOBAL_SELECT_FROM_VIDEO),
        // live while a Document is open: the list takes them first.
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        trigger(QStringLiteral("viewLayoutManage"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(list->property("activeFocus").toBool());
        QTest::keyClick(window(), Qt::Key_F2);
        QTRY_VERIFY(name->property("activeFocus").toBool());
        QTest::keyClick(window(), Qt::Key_A, Qt::ControlModifier);
        for (const char c : {'l', 'e', 'a', 'd'})
            QTest::keyClick(window(), c);
        QTest::keyClick(window(), Qt::Key_Return);
        QTRY_COMPARE(switcherText(), QStringLiteral("lead"));
        QCOMPARE(namesInFile(layoutsFile()), (QStringList{QStringLiteral("lead"), QStringLiteral("Second")}));
        QVERIFY(dialog->property("visible").toBool()); // Return renamed: the dialog stays
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        trigger(QStringLiteral("viewLayoutManage"));
        QTRY_VERIFY(dialog->property("opened").toBool());

        // Delete: the tab keeps its arrangement, named by its preset now.
        QTRY_VERIFY(list->property("activeFocus").toBool());
        QTest::keyClick(window(), Qt::Key_Delete);
        QTRY_COMPARE(namesInFile(layoutsFile()), QStringList{QStringLiteral("Second")});
        QCOMPARE(switcherText(), QStringLiteral("Editing"));
        QVERIFY(!isOpen("audioDock"));
        QVERIFY(dialog->property("visible").toBool()); // one is left
        QCOMPARE(name->property("text").toString(), QStringLiteral("Second"));
        QVERIFY(QMetaObject::invokeMethod(named("manageLayoutsDelete"), "clicked"));
        QTRY_VERIFY(!dialog->property("visible").toBool()); // none is left
        QVERIFY(namesInFile(layoutsFile()).isEmpty());
        QVERIFY(!named("viewLayoutManage")->property("enabled").toBool());
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

        QVERIFY(writeAll(layoutsFile(), "{not json"));
        start();
        QVERIFY(layouts().savedLayouts().isEmpty());
        QVERIFY2(layouts().notice().contains(QStringLiteral("layouts.json.unrestored")), qPrintable(layouts().notice()));
        QCOMPARE(readAll(layoutsFile() + QStringLiteral(".unrestored")), QByteArray("{not json"));
        stop();
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
            QVERIFY(frameOf(save));
            grab(frameOf(save), out + QStringLiteral("/save-layout") + suffix, 8);
            QMetaObject::invokeMethod(save, "reject");
            QTRY_VERIFY(!save->property("visible").toBool());
            trigger(QStringLiteral("viewLayoutManage"));
            QObject *manage = named("manageLayoutsDialog");
            QTRY_VERIFY(manage->property("opened").toBool());
            QVERIFY(frameOf(manage));
            grab(frameOf(manage), out + QStringLiteral("/manage-layouts") + suffix, 8);
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
