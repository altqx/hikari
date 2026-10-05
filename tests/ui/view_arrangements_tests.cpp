// D2: legacy's View menu (GLOBAL_VIEW_ALL, GLOBAL_VIEW_VIDEO,
// GLOBAL_VIEW_AUDIO, GLOBAL_VIEW_ONLY_VIDEO, GLOBAL_VIEW_SUBS) as named panel
// arrangements over the docked Workspace, and GLOBAL_EDITOR ("Enable /
// Disable editor", Ctrl+E), the video player layout. Read against
// HikariSubFrame::OnMenuSelected, OnMenuOpened and HideEditor at 20d647c4.

#include "hikari/app/application.h"

#include "docking.h"
#include "icon_theme.h"
#include "workspace_layout.h"

#include <QAccessible>
#include <QFile>
#include <QPalette>
#include <QPointer>
#include <QScopeGuard>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace hikari;

namespace {

QString writeFile(const QTemporaryDir &dir, const char *name, const char *events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    // ScriptType: libass reads a track only with it (or a Styles section).
    f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    f.write(events);
    return path;
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// K1: a theme's palette in visual-language.md's tokens, as shell_tests'
// surface screenshots take it.
QPalette themePalette(const QPalette &base, ui::icons::Appearance appearance)
{
    using ui::icons::Slot;
    QPalette palette = base;
    const QColor text = ui::icons::defaultColour(appearance, Slot::Normal);
    const QColor disabled = ui::icons::defaultColour(appearance, Slot::Disabled);
    const auto surfaces = ui::icons::surfaces(appearance);
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const QColor ink = group == QPalette::Disabled ? disabled : text;
        palette.setColor(group, QPalette::Window, surfaces[0]);
        palette.setColor(group, QPalette::Button, surfaces[2]);
        palette.setColor(group, QPalette::Base, surfaces[3]);
        palette.setColor(group, QPalette::WindowText, ink);
        palette.setColor(group, QPalette::ButtonText, ink);
        palette.setColor(group, QPalette::Text, ink);
        palette.setColor(group, QPalette::Accent, ui::icons::defaultColour(appearance, Slot::Accent));
    }
    return palette;
}

const QStringList kCore{QStringLiteral("videoDock"), QStringLiteral("audioDock"), QStringLiteral("editorDock"),
                        QStringLiteral("gridDock")};

} // namespace

class ViewArrangementsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString episode;
    QString original;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    QString layoutFile() const { return dir.filePath(QStringLiteral("layout.json")); }
    QString settingsFile() const { return dir.filePath(QStringLiteral("hikari.ini")); }
    void start()
    {
        // No spell checker: its missing-dictionary notice is not D2's.
        QSettings(settingsFile(), QSettings::IniFormat).setValue(QStringLiteral("profile/editor.spellchecker"), false);
        app::Application::Options options;
        options.settingsFile = settingsFile();
        options.playbackAudio = false; // CI runners without an audio device
        application = std::make_unique<app::Application>(options);
        // CheckLastKeyEvent ignores the same action within 100 ms: each key
        // here comes a second after the last.
        auto t = std::make_shared<qint64>(0);
        application->hotkeys().setKeyClock([t] { return *t += 1000; });
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // The saved layout (and the player layout) once the shell laid out.
        QTest::qWait(200);
    }
    void stop()
    {
        if (!application)
            return;
        application->workspaceLayout().save();
        engine.reset();
        application.reset();
        window = nullptr;
    }
    QObject *root() const { return engine->rootObjects().first(); }
    QObject *named(const QString &name) const
    {
        if (auto *found = root()->findChild<QObject *>(name))
            return found;
        for (QObject *o : root()->findChildren<QObject *>())
            if (o->objectName() == name)
                return o;
        return nullptr;
    }
    QObject *dock(const QString &name) const { return named(name); }
    bool isOpen(const QString &name) const { return dock(name)->property("isOpen").toBool(); }
    QStringList openCore() const
    {
        QStringList open;
        for (const QString &name : kCore)
            if (isOpen(name))
                open << name;
        return open;
    }
    QQuickItem *panel(const char *name) const
    {
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *q = qobject_cast<QQuickWindow *>(w))
                if (auto *found = q->contentItem()->findChild<QQuickItem *>(QLatin1String(name)))
                    return found;
        return root()->findChild<QQuickItem *>(QLatin1String(name));
    }
    // Where each open core panel is, in the main window's scene.
    QMap<QString, QRectF> placement() const
    {
        QMap<QString, QRectF> out;
        for (const char *name : {"videoPanel", "audioPanel", "editorPanel", "gridPanel"}) {
            QQuickItem *p = panel(name);
            if (p && p->isVisible())
                out.insert(QLatin1String(name), p->mapRectToScene(QRectF(0, 0, p->width(), p->height())));
        }
        return out;
    }
    bool menuEnabled(const char *name) const { return named(QLatin1String(name))->property("enabled").toBool(); }
    void trigger(const char *name)
    {
        QObject *item = named(QLatin1String(name));
        QVERIFY2(item, name);
        QVERIFY2(item->property("enabled").toBool(), name);
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        QCoreApplication::processEvents();
    }
    QString focusedPanel() const
    {
        QWindow *w = QGuiApplication::focusWindow();
        auto *q = qobject_cast<QQuickWindow *>(w);
        for (QQuickItem *p = q ? q->activeFocusItem() : nullptr; p; p = p->parentItem())
            if (p->objectName().endsWith(QLatin1String("Panel")))
                return p->objectName();
        return {};
    }
    void openVideo()
    {
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        // The resolution question the video may ask a moment later.
        auto *mismatch = named(QStringLiteral("mismatchDialog"));
        if (mismatch && QTest::qWaitFor([&] { return mismatch->property("visible").toBool(); }, 3000)) {
            QMetaObject::invokeMethod(mismatch, "close");
            QTRY_VERIFY(!mismatch->property("visible").toBool());
        }
    }
    // A draft in the Line editor: the first Line's text with "!" typed.
    void typeDraft()
    {
        // The keys go to the main window (a floated panel may have taken it).
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        auto *grid = panel("editingGrid");
        grid->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Home);
        auto *text = panel("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_End);
        QTest::keyClick(window, Qt::Key_Exclam, Qt::ShiftModifier);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first!"));
    }
    // The Video panel's visual tool items (rail, values, overlay) shown.
    int visualToolItems(bool shown) const
    {
        int n = 0;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            const QByteArray type = item->metaObject()->className();
            if ((type.startsWith("VisualToolRail") || type.startsWith("VisualToolValues") ||
                 type.startsWith("VisualOverlay")) &&
                item->isVisible() == shown)
                ++n;
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(panel("videoPanel"));
        return n;
    }
    // The shown title bar over a panel: its group's, or its floating
    // window's when that shows the only one.
    QQuickItem *titleBarOver(const char *panelName) const
    {
        std::function<QQuickItem *(QQuickItem *, int)> bar = [&](QQuickItem *item, int depth) -> QQuickItem * {
            if (item->objectName() == QLatin1String("dockTitleBar"))
                return item->isVisible() ? item : nullptr;
            if (depth == 0 || item->objectName().endsWith(QLatin1String("Panel")))
                return nullptr;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = bar(child, depth - 1))
                    return found;
            return nullptr;
        };
        for (QQuickItem *p = panel(panelName); p; p = p->parentItem())
            if (QQuickItem *found = bar(p, 4))
                return found;
        return nullptr;
    }
    // A title bar button pressed as the pointer and as assistive
    // technology press it.
    void pressTitleBarButton(QQuickItem *button)
    {
        const QPoint centre = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
        const QPointer<QQuickItem> guard(button);
        QTest::mouseClick(button->window(), Qt::LeftButton, {}, centre);
        QCoreApplication::processEvents();
        if (!guard || !guard->isVisible())
            return; // it closed its panel
        if (QAccessibleInterface *a = QAccessible::queryAccessibleInterface(button))
            if (QAccessibleActionInterface *actions = a->actionInterface())
                actions->doAction(QAccessibleActionInterface::pressAction());
        QCoreApplication::processEvents();
    }
    application::EditSession *session() const
    {
        return application->files().session(*application->workspace().editingTarget());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        episode = writeFile(dir, "episode.ass",
                            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                            "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n");
        original = writeFile(dir, "original.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,ref\n");
    }
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const QString &f : {layoutFile(), layoutFile() + QStringLiteral(".bak"), settingsFile()})
            QFile::remove(f);
    }

    // HikariSubFrame::OnMenuSelected (HikariSubFrame.cpp:849-892): which of
    // the video, the audio box, the Line editor and the Grid each view shows.
    void arrangementsShowLegacysPanels()
    {
        using W = ui::WorkspaceLayoutController;
        QCOMPARE(W::arrangements(), (QStringList{"GLOBAL_VIEW_ALL", "GLOBAL_VIEW_VIDEO", "GLOBAL_VIEW_AUDIO",
                                                 "GLOBAL_VIEW_ONLY_VIDEO", "GLOBAL_VIEW_SUBS"}));
        QCOMPARE(W::arrangementPanels("GLOBAL_VIEW_ALL"), (QStringList{"Video", "Audio", "Editor", "Grid"}));
        QCOMPARE(W::arrangementPanels("GLOBAL_VIEW_VIDEO"), (QStringList{"Video", "Editor", "Grid"}));
        QCOMPARE(W::arrangementPanels("GLOBAL_VIEW_AUDIO"), (QStringList{"Audio", "Editor", "Grid"}));
        QCOMPARE(W::arrangementPanels("GLOBAL_VIEW_ONLY_VIDEO"), (QStringList{"Video"}));
        QCOMPARE(W::arrangementPanels("GLOBAL_VIEW_SUBS"), (QStringList{"Editor", "Grid"}));
        QVERIFY(W::arrangementPanels("GLOBAL_EDITOR").isEmpty());
    }

    // The View menu: legacy's items first, with their icons, enabled as
    // OnMenuOpened's ViewMenu (HikariSubFrame.cpp:2413-2437).
    void viewMenuItemsAreEnabledAsLegacys()
    {
        start();
        QVERIFY(application->openFile(episode));
        const QStringList items{"viewAll", "viewVideoAndSubs", "viewAudioAndSubs", "viewOnlyVideo", "viewOnlySubtitles"};
        const QStringList labels{"All", "Video and subs", "Audio and subs", "Only video", "Only subtitles"};
        const QStringList icons{"view-all", "view-video-subs", "view-audio-subs", "view-only-video", "view-only-subs"};
        auto *menu = named(QStringLiteral("viewMenu"));
        for (int i = 0; i < items.size(); ++i) {
            QObject *item = named(items[i]);
            QVERIFY2(item, qPrintable(items[i]));
            QCOMPARE(item->property("text").toString(), labels[i]);
            QCOMPARE(item->property("iconRole").toString(), icons[i]);
            QQuickItem *at = nullptr;
            QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, at), Q_ARG(int, i)));
            QCOMPARE(static_cast<QObject *>(at), item);
        }
        // No video and no audio: Only subtitles alone.
        QVERIFY(menuEnabled("viewOnlySubtitles"));
        for (const char *name : {"viewAll", "viewVideoAndSubs", "viewOnlyVideo", "viewAudioAndSubs"})
            QVERIFY2(!menuEnabled(name), name);
        // A disabled view's hotkey does nothing (the item's state is checked).
        QVERIFY(QMetaObject::invokeMethod(root(), "runGlobalHotkey", Q_ARG(QVariant, QStringLiteral("GLOBAL_VIEW_ONLY_VIDEO"))));
        QCOMPARE(openCore(), kCore);
        application->audio().openDummy();
        QTRY_VERIFY(menuEnabled("viewAudioAndSubs"));
        QVERIFY(!menuEnabled("viewAll"));
        // A video without audio closes the box (A1, legacy changeAudio):
        // the blank audio comes after it.
        openVideo();
        QTRY_VERIFY(!menuEnabled("viewAudioAndSubs"));
        QVERIFY(menuEnabled("viewAll"));
        application->audio().openDummy();
        QTRY_VERIFY(application->audio().hasAudio());
        for (const QString &name : items)
            QVERIFY2(menuEnabled(qPrintable(name)), qPrintable(name));
        stop();
    }

    // Each view shows its panels and hides the others; the Reference tray
    // goes with the Grid, the Timing tool comes back after Only video, a
    // hidden video pauses, the draft stays a draft, the focus stays on a
    // shown panel, and All returns every panel to its place.
    void arrangementsShowHideAndRoundTrip()
    {
        start();
        QVERIFY(application->openFile(episode));
        openVideo();
        application->audio().openDummy(); // after the video (A1 closes the box for a video without audio)
        QTRY_VERIFY(application->audio().hasAudio());
        QVERIFY(application->openReference(original));
        QTRY_VERIFY(isOpen(QStringLiteral("referenceDock")));
        const auto editing = placement();
        QCOMPARE(editing.size(), 4);
        typeDraft();
        QVERIFY(session()->draftLine());
        QCOMPARE(focusedPanel(), QStringLiteral("editorPanel"));

        trigger("viewVideoAndSubs");
        QTRY_COMPARE(openCore(), (QStringList{"videoDock", "editorDock", "gridDock"}));
        QCOMPARE(focusedPanel(), QStringLiteral("editorPanel")); // still shown: the focus stays
        QVERIFY(isOpen(QStringLiteral("referenceDock"))); // the tray goes with the Grid

        trigger("viewAudioAndSubs");
        QTRY_COMPARE(openCore(), (QStringList{"audioDock", "editorDock", "gridDock"}));
        QCOMPARE(focusedPanel(), QStringLiteral("editorPanel"));

        // The Timing tool is hidden by Only video and comes back after it.
        QVERIFY(QMetaObject::invokeMethod(dock(QStringLiteral("timingDock")), "open"));
        QTRY_VERIFY(isOpen(QStringLiteral("timingDock")));
        panel("lineText")->forceActiveFocus();
        trigger("viewOnlyVideo");
        QTRY_COMPARE(openCore(), (QStringList{"videoDock"}));
        QVERIFY(!isOpen(QStringLiteral("timingDock")));
        QVERIFY(!isOpen(QStringLiteral("referenceDock"))); // no Grid, no tray
        QTRY_COMPARE(focusedPanel(), QStringLiteral("videoPanel")); // the focused panel went: the next shown one
        QVERIFY(session()->draftLine()); // hiding the Line editor keeps the draft
        QVERIFY(application->workspace().editingTarget());

        // A video playing when a view hides it pauses (legacy Pause()).
        QVERIFY(application->video().play());
        QTRY_VERIFY(application->video().playing());
        trigger("viewOnlySubtitles");
        QTRY_COMPARE(openCore(), (QStringList{"editorDock", "gridDock"}));
        QTRY_VERIFY(!application->video().playing());
        QVERIFY(isOpen(QStringLiteral("timingDock")));
        QVERIFY(isOpen(QStringLiteral("referenceDock"))); // the Grid back, the tray with it
        QTRY_COMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        QCOMPARE(panel("lineText")->property("text").toString(), QStringLiteral("first!"));
        QVERIFY(QMetaObject::invokeMethod(dock(QStringLiteral("timingDock")), "close"));

        // All: every panel back where it was.
        trigger("viewAll");
        QTRY_COMPARE(openCore(), kCore);
        QTRY_COMPARE(placement(), editing);
        QCOMPARE(panel("lineText")->property("text").toString(), QStringLiteral("first!"));
        QVERIFY(session()->draftLine());

        // The hotkeys run the same arrangements (OnMenuSelected).
        QVERIFY(QMetaObject::invokeMethod(root(), "runGlobalHotkey", Q_ARG(QVariant, QStringLiteral("GLOBAL_VIEW_SUBS"))));
        QTRY_COMPARE(openCore(), (QStringList{"editorDock", "gridDock"}));
        QVERIFY(QMetaObject::invokeMethod(root(), "runGlobalHotkey", Q_ARG(QVariant, QStringLiteral("GLOBAL_VIEW_ALL"))));
        QTRY_COMPARE(openCore(), kCore);
        QTRY_COMPARE(placement(), editing);
        application->editor().discard();
        stop();
    }

    // An arrangement is saved as the layout and comes back next session.
    void anArrangementIsTheSavedLayout()
    {
        start();
        QVERIFY(application->openFile(episode));
        trigger("viewOnlySubtitles");
        QTRY_COMPARE(openCore(), (QStringList{"editorDock", "gridDock"}));
        stop();
        start();
        QTRY_COMPARE(openCore(), (QStringList{"editorDock", "gridDock"}));
        stop();
    }

    // GLOBAL_EDITOR (HikariSubFrame::HideEditor): Ctrl+E switches to the
    // player layout and back to the arrangement it held.
    void editorSwitchHoldsTheArrangement()
    {
        start();
        QVERIFY(application->openFile(episode));
        QVERIFY(application->editorOn());
        // The Subtitles menu's first item, with K1's icon.
        auto *item = named(QStringLiteral("editorSwitchMenuItem"));
        QVERIFY(item);
        QCOMPARE(item->property("text").toString(), QStringLiteral("Enable / Disable editor"));
        QCOMPARE(item->property("iconRole").toString(), QStringLiteral("editor"));
        QQuickItem *first = nullptr;
        QVERIFY(QMetaObject::invokeMethod(named(QStringLiteral("subtitlesMenu")), "itemAt",
                                          Q_RETURN_ARG(QQuickItem *, first), Q_ARG(int, 0)));
        QCOMPARE(static_cast<QObject *>(first), item);
        // A floated Audio panel, the layout of record.
        QVERIFY(dock(QStringLiteral("audioDock"))->setProperty("isFloating", true));
        QTRY_VERIFY(dock(QStringLiteral("audioDock"))->property("isFloating").toBool());
        typeDraft();
        // What is on disk and shown when the player layout takes over.
        const auto editing = placement();
        application->workspaceLayout().save();
        const QByteArray saved = readFile(layoutFile());
        QVERIFY(!saved.isEmpty());

        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        QTRY_VERIFY(!application->editorOn());
        QTRY_COMPARE(openCore(), (QStringList{"videoDock"}));
        QVERIFY(application->workspaceLayout().holding());
        QTRY_COMPARE(focusedPanel(), QStringLiteral("videoPanel"));
        // HideVideoToolbar and RemoveVisual(false, true): no visual tool
        // rail or values, no overlay.
        QCOMPARE(visualToolItems(true), 0);
        // Nothing is saved while the player layout holds the arrangement.
        QVERIFY(!application->workspaceLayout().save());
        QCOMPARE(readFile(layoutFile()), saved);
        // OnMenuOpened: the editing commands are off (FileMenu, EditMenu,
        // VidMenu, AudMenu, SubsMenu, the automation menu, ViewMenu).
        for (const char *name : {"saveAllMenuItem", "saveAsMenuItem", "newMenuItem", "historyMenuItem", "findMenuItem",
                                 "selectLinesMenuItem", "openAudioMenuItem", "showShiftTimes", "assProperties",
                                 "conversionMenu", "loadScriptMenuItem", "viewOnlySubtitles", "panelsMenu", "resetLayout",
                                 "layoutPresetMenu"})
            QVERIFY2(!menuEnabled(name), name);
        QVERIFY(menuEnabled("editorSwitchMenuItem"));
        QVERIFY(menuEnabled("openVideoMenuItem"));
        QVERIFY(session()->draftLine()); // the draft waits in the hidden Line editor

        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        QTRY_VERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        QVERIFY(!application->workspaceLayout().holding());
        QTRY_VERIFY(dock(QStringLiteral("audioDock"))->property("isFloating").toBool());
        QTRY_COMPARE(placement(), editing);
        QTRY_COMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        QCOMPARE(panel("lineText")->property("text").toString(), QStringLiteral("first!"));
        QVERIFY(session()->draftLine());
        QVERIFY(menuEnabled("saveAsMenuItem"));
        QVERIFY(menuEnabled("panelsMenu"));
        QCOMPARE(visualToolItems(true), 3);
        application->editor().discard();
        stop();
    }

    // HideEditor's last lines (HikariSubFrame.cpp:2082-2087): the editor off
    // hides the Find and replace window (FR, the Search tool), Select lines
    // (SL) and the Style manager (StyleStore); the Reference tray goes with
    // the Grid. The editor back on shows the Grid and its tray again, but
    // not the tools legacy hid (nothing shows FR, SL or StyleStore again).
    void editorOffClosesTheEditingTools()
    {
        start();
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        QTRY_VERIFY(isOpen(QStringLiteral("referenceDock")));
        QVERIFY(QMetaObject::invokeMethod(root(), "openSearch", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(isOpen(QStringLiteral("searchDock")));
        QVERIFY(QMetaObject::invokeMethod(named(QStringLiteral("selectLinesMenuItem"))->property("action").value<QObject *>(),
                                          "trigger"));
        QObject *selectLines = named(QStringLiteral("selectLinesDialog"));
        QVERIFY(selectLines);
        QTRY_VERIFY(selectLines->property("visible").toBool());
        trigger("styleManagerMenuItem");
        QObject *styles = named(QStringLiteral("styleManager"));
        QVERIFY(styles);
        QTRY_VERIFY(styles->property("visible").toBool());

        application->toggleEditor();
        QTRY_VERIFY(!application->editorOn());
        QTRY_COMPARE(openCore(), (QStringList{"videoDock"}));
        for (const char *name : {"referenceDock", "timingDock", "searchDock"})
            QVERIFY2(!isOpen(QLatin1String(name)), name);
        QTRY_VERIFY(!selectLines->property("visible").toBool());
        QTRY_VERIFY(!styles->property("visible").toBool());

        application->toggleEditor();
        QTRY_VERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        QTRY_VERIFY(isOpen(QStringLiteral("referenceDock")));
        QVERIFY(!isOpen(QStringLiteral("searchDock")));
        QVERIFY(!selectLines->property("visible").toBool());
        QVERIFY(!styles->property("visible").toBool());
        stop();
    }

    // A reference opened in the player layout waits for the Grid: no tray
    // over the video, and it shows when the editor comes back.
    void aReferenceOpenedInThePlayerLayoutWaitsForTheGrid()
    {
        start();
        QVERIFY(application->openFile(episode));
        QVERIFY(!isOpen(QStringLiteral("referenceDock")));
        application->toggleEditor();
        QTRY_VERIFY(!application->editorOn());
        QTRY_COMPARE(openCore(), (QStringList{"videoDock"}));
        QVERIFY(application->openReference(original));
        QTest::qWait(200);
        QVERIFY(!isOpen(QStringLiteral("referenceDock")));
        QCOMPARE(openCore(), (QStringList{"videoDock"}));
        application->toggleEditor();
        QTRY_VERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        QTRY_VERIFY(isOpen(QStringLiteral("referenceDock")));
        stop();
    }

    // The player layout with a video: the switch is off (above) and so are
    // the Panels menu and the arrangements, so the Video panel, legacy's
    // only content of the frame, cannot be closed from its title bar,
    // docked or floating. With the editor back it closes as any panel.
    void playerLayoutKeepsTheVideoPanel()
    {
        start();
        QVERIFY(application->openFile(episode));
        application->toggleEditor();
        QTRY_VERIFY(!application->editorOn());
        openVideo();
        QTRY_COMPARE(openCore(), (QStringList{"videoDock"}));
        for (const char *name : {"editorSwitchMenuItem", "panelsMenu", "viewAll", "resetLayout"})
            QVERIFY2(!menuEnabled(name), name);
        QQuickItem *bar = titleBarOver("videoPanel");
        QVERIFY(bar);
        auto *close = bar->findChild<QQuickItem *>(QStringLiteral("dockCloseButton"));
        QVERIFY(close);
        QTRY_VERIFY(!close->isEnabled());
        QVERIFY(!close->isVisible());
        pressTitleBarButton(close);
        QTest::qWait(200);
        QVERIFY(isOpen(QStringLiteral("videoDock")));
        QCOMPARE(openCore(), (QStringList{"videoDock"}));
        // Floated, its window's title bar cannot close it either.
        QVERIFY(dock(QStringLiteral("videoDock"))->setProperty("isFloating", true));
        QTRY_VERIFY(dock(QStringLiteral("videoDock"))->property("isFloating").toBool());
        QTRY_VERIFY(titleBarOver("videoPanel"));
        bar = titleBarOver("videoPanel");
        close = bar->findChild<QQuickItem *>(QStringLiteral("dockCloseButton"));
        QTRY_VERIFY(!close->isEnabled());
        QVERIFY(!close->isVisible());
        pressTitleBarButton(close);
        QTest::qWait(200);
        QVERIFY(isOpen(QStringLiteral("videoDock")));
        QVERIFY(dock(QStringLiteral("videoDock"))->setProperty("isFloating", false));
        QTRY_VERIFY(!dock(QStringLiteral("videoDock"))->property("isFloating").toBool());

        application->toggleEditor();
        QTRY_VERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        QTRY_VERIFY(titleBarOver("videoPanel"));
        close = titleBarOver("videoPanel")->findChild<QQuickItem *>(QStringLiteral("dockCloseButton"));
        QTRY_VERIFY(close->isEnabled());
        QVERIFY(close->isVisible());
        pressTitleBarButton(close);
        QTRY_VERIFY(!isOpen(QStringLiteral("videoDock")));
        stop();
    }

    // OnMenuOpened: the switch is enabled with a DirectShow video or none
    // (HikariSubFrame.cpp:2377-2379); the rewrite's videos are FFMS2's.
    // A video opened in the player layout names the window and the tab
    // (VideoBox's Label(0, true)), shows no subtitles (OpenSubs
    // CLOSE_SUBTITLES) and the times field has no ms from the Line
    // (VideoBox::ShowTimes); the editor back on shows them again, and
    // subtitles opened turn it on (HikariSubFrame::OpenFile).
    void playerLayoutWithAVideo()
    {
        start();
        QVERIFY(application->openFile(episode));
        trigger("editorSwitchMenuItem");
        QTRY_VERIFY(!application->editorOn());
        openVideo();
        QVERIFY(!menuEnabled("editorSwitchMenuItem"));
        QTRY_COMPARE(window->title(), QStringLiteral("cfr.mkv - HikariSub"));
        const QVariantMap tab = application->tabs().value(application->currentTab()).toMap();
        QCOMPARE(tab.value(QStringLiteral("label")).toString(), QStringLiteral("cfr.mkv"));
        QVERIFY(!application->video().session().hasSubtitles());
        QTRY_VERIFY(!application->video().times().isEmpty());
        QVERIFY2(!application->video().times().contains(QLatin1String(" ms")), qPrintable(application->video().times()));
        // A disabled switch's hotkey does nothing either.
        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QVERIFY(!application->editorOn());

        // The switch back on, as a DirectShow video (W1) allows: the
        // subtitles over the video again (ChangeVobsub) and the Line's ms.
        application->toggleEditor();
        QTRY_VERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        QTRY_COMPARE(window->title(), QStringLiteral("episode.ass - HikariSub"));
        QVERIFY(application->video().hasVideo());
        QTRY_VERIFY(application->video().session().hasSubtitles());
        QTRY_VERIFY(application->video().times().contains(QLatin1String(" ms")));
        application->toggleEditor();
        QTRY_VERIFY(!application->video().session().hasSubtitles());

        // File > Open into the tab turns the editor on (legacy OpenFile's
        // `if (!tab->editor ...) HideEditor()`).
        QVERIFY(QMetaObject::invokeMethod(root(), "openSubtitles", Q_ARG(QVariant, episode)));
        QTRY_VERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        QTRY_COMPARE(window->title(), QStringLiteral("episode.ass - HikariSub"));
        stop();
    }

    // EDITOR_ON: the player layout comes back next session (the frame's
    // `if (!EDITOR_ON) HideEditor(false)`) over the saved editing layout,
    // which the editor then shows; the session file writes "Editor: 0".
    void playerLayoutPersists()
    {
        start();
        QVERIFY(application->openFile(episode));
        QVERIFY(QMetaObject::invokeMethod(dock(QStringLiteral("gridDock")), "close"));
        QTRY_VERIFY(!isOpen(QStringLiteral("gridDock")));
        QVERIFY(application->workspaceLayout().save());
        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        QTRY_VERIFY(!application->editorOn());
        QVERIFY(application->saveLastSession());
        QFile last(application->lastSessionPath());
        QVERIFY(last.open(QIODevice::ReadOnly));
        QVERIFY(last.readAll().contains("Editor: 0"));
        stop();

        start();
        QVERIFY(!application->editorOn());
        QTRY_COMPARE(openCore(), (QStringList{"videoDock"}));
        QVERIFY(application->workspaceLayout().holding());
        trigger("editorSwitchMenuItem");
        QTRY_VERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), (QStringList{"videoDock", "audioDock", "editorDock"})); // the saved layout
        stop();
    }

    // Notebook::LoadSession: each loaded tab starts with the editor and its
    // "Editor: 0" turns it off when the tab has a video (Notebook.cpp:
    // 1458-1459); the last tab, the active one, sets the shared switch. Two
    // tabs saved off leave it off (not switched twice), and a session whose
    // last tab is on, or has no video, turns it back on.
    void sessionLoadSetsTheSwitchFromTheActiveTab()
    {
        start();
        const QString video = QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv");
        const QString subs = QDir::fromNativeSeparators(episode);
        auto sessionText = [&](int lastEditor, bool lastVideo) {
            const QString tab = QStringLiteral("Tab: %1\r\nVideo: %2\r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: %3\r\n"
                                               "Active: 0\r\nScroll: 0\r\nEditor: %4\r\n");
            return (QStringLiteral("﻿[HikariSub v0.0.1]\r\n") + tab.arg(0).arg(video, subs).arg(0) +
                    tab.arg(1).arg(lastVideo ? video : QString(), subs).arg(lastEditor))
                .toUtf8();
        };
        auto load = [&](const QByteArray &text) {
            QFile f(application->lastSessionPath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
            f.close();
            const QVariantMap review = application->reviewSession();
            QVERIFY(review.value(QStringLiteral("ok")).toBool());
            QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
            application->finishClose();
            QCoreApplication::processEvents();
        };
        QVERIFY(application->editorOn());
        load(sessionText(0, true));
        QVERIFY(!application->editorOn());
        QTRY_COMPARE(openCore(), (QStringList{"videoDock"}));
        load(sessionText(0, true)); // already off: stays off
        QVERIFY(!application->editorOn());
        load(sessionText(1, true));
        QVERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        load(sessionText(0, true));
        QVERIFY(!application->editorOn());
        load(sessionText(0, false)); // no video: HideEditor is not called
        QVERIFY(application->editorOn());
        QTRY_COMPARE(openCore(), kCore);
        stop();
    }

    // The View and Subtitles menus and the player layout in the light and
    // dark palettes (HIKARI_SURFACE_SHOT_DIR, as shell_tests' surfaces).
    void surfaceScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        const QPalette before = QGuiApplication::palette();
        auto restore = qScopeGuard([&] { QGuiApplication::setPalette(before); });
        window->resize(1280, 800);
        QVERIFY(application->openFile(episode));
        openVideo();
        application->audio().openDummy();
        QTRY_VERIFY(application->audio().hasAudio());
        application->video().stepFrames(24);
        auto shootMenu = [&](const char *name, const QString &file) {
            auto *menu = named(QLatin1String(name));
            QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QQuickItem *, window->contentItem()),
                                              Q_ARG(QPointF, QPointF(200, 0))));
            QTRY_VERIFY(menu->property("opened").toBool());
            auto *content = menu->property("contentItem").value<QQuickItem *>();
            QTest::qWait(300);
            const QImage shot = content->window()->grabWindow();
            const qreal dpr = content->window()->effectiveDevicePixelRatio();
            const QRectF r = content->mapRectToScene(QRectF(0, 0, content->width(), content->height()));
            QRect crop = QRectF(r.topLeft() * dpr, r.size() * dpr).toAlignedRect();
            crop = (content->window() == window ? crop.adjusted(-4, -4, 4, 4) : shot.rect()).intersected(shot.rect());
            QVERIFY(shot.copy(crop).save(file));
            QMetaObject::invokeMethod(menu, "close");
            QTRY_VERIFY(!menu->property("visible").toBool());
        };
        for (const auto appearance : {ui::icons::Appearance::Light, ui::icons::Appearance::Dark}) {
            const QPalette palette = themePalette(before, appearance);
            QGuiApplication::setPalette(palette);
            auto *controls = root()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), palette.color(QPalette::Window));
            const QString suffix = QLatin1Char('-') + ui::icons::appearanceName(appearance) + QStringLiteral(".png");
            QTest::qWait(200);
            shootMenu("viewMenu", out + QStringLiteral("/view-menu") + suffix);
            shootMenu("subtitlesMenu", out + QStringLiteral("/subtitles-menu") + suffix);
            trigger("viewVideoAndSubs");
            QTest::qWait(300);
            QVERIFY(window->grabWindow().save(out + QStringLiteral("/view-video-and-subs") + suffix));
            trigger("viewAll");
            QTest::qWait(300);
        }
        // The player layout: no video yet allows the switch, then a video.
        QGuiApplication::setPalette(before);
        stop();
        start();
        window->resize(1280, 800);
        QVERIFY(application->openFile(episode));
        trigger("editorSwitchMenuItem");
        QTRY_VERIFY(!application->editorOn());
        openVideo();
        application->video().stepFrames(24);
        for (const auto appearance : {ui::icons::Appearance::Light, ui::icons::Appearance::Dark}) {
            const QPalette palette = themePalette(before, appearance);
            QGuiApplication::setPalette(palette);
            auto *controls = root()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), palette.color(QPalette::Window));
            const QString suffix = QLatin1Char('-') + ui::icons::appearanceName(appearance) + QStringLiteral(".png");
            QTest::qWait(300);
            QVERIFY(window->grabWindow().save(out + QStringLiteral("/player-layout") + suffix));
            shootMenu("viewMenu", out + QStringLiteral("/player-view-menu") + suffix);
        }
        stop();
    }
};

QTEST_MAIN(ViewArrangementsTest)
#include "view_arrangements_tests.moc"
