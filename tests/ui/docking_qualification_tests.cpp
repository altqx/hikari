// D1: qualification of the pinned docking engine (KDDockWidgets 2.4.1,
// QtQuick frontend, docs/qt/docking.md) with the provisioned Qt: the
// default arrangement, float/redock, tabs, close/reopen, layout save and
// restore through the public LayoutSaver, and a corrupt layout refused
// without breaking the live one. Platform checks (screen readers, Wayland
// compositors, mixed DPI) are native observations, not these tests.
//
// D3: the engine runs behind Hikari's adapter (ui/docking.h) with its
// MuseScore-style chrome: one header row per group (a lone panel's title
// bar, tabs for a group or a horizontal panel, the toolbar slot), the "⋯"
// button and its keyboard reach, double-click floating, the drop highlight
// over the area a drop takes, 1-pixel separators, borderless floating tool
// windows and the panels' minimum sizes. The menu's items are the shell's
// (hikari_ui_shell_tests).

#include "docking.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/qtquick/Platform.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <QAccessible>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStyleHints>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

namespace {

constexpr auto kShell = R"(
import QtQuick
import QtQuick.Controls
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW
ApplicationWindow {
    width: 1000; height: 700; visible: true
    // D3: a floating window of two groups (Video's, then Audio beside it).
    function pairFloating() {
        video.addDockWidgetToContainingWindow(audio, KDDW.KDDockWidgets.Location_OnRight)
    }
    // D3: Audio and Editor as tabs of Video's group, the Grid right of it.
    function tabIntoVideo() {
        video.addDockWidgetAsTab(audio)
        video.addDockWidgetAsTab(editor)
        area.addDockWidget(grid, KDDW.KDDockWidgets.Location_OnRight, video)
    }
    KDDW.DockingArea {
        id: area
        objectName: "area"
        anchors.fill: parent
        uniqueName: "Classic"
        KDDW.DockWidget { id: video; objectName: "Video"; uniqueName: "Video"; title: "Video"; Rectangle { objectName: "videoBody"; anchors.fill: parent; color: "black" } }
        KDDW.DockWidget { id: audio; objectName: "Audio"; uniqueName: "Audio"; title: "Audio"; Rectangle { anchors.fill: parent } }
        KDDW.DockWidget {
            id: editor; objectName: "Editor"; uniqueName: "Editor"; title: "Editor"
            Column {
                anchors.fill: parent
                Button { objectName: "editorButton"; text: "Apply" }
                TextArea { objectName: "editorText"; width: parent.width; height: 100 }
            }
        }
        KDDW.DockWidget { id: grid; objectName: "Grid"; uniqueName: "Grid"; title: "Grid"; Rectangle { objectName: "gridBody"; anchors.fill: parent } }
        Component.onCompleted: {
            // D3: the Grid is a horizontal panel with a toolbar in its header.
            Docking.setPanelHeader("Grid", true, gridToolbar)
            addDockWidget(video, KDDW.KDDockWidgets.Location_OnLeft)
            addDockWidget(audio, KDDW.KDDockWidgets.Location_OnRight)
            addDockWidget(editor, KDDW.KDDockWidgets.Location_OnBottom)
            addDockWidget(grid, KDDW.KDDockWidgets.Location_OnBottom)
        }
    }
    Row {
        id: gridToolbar
        objectName: "gridToolbar"
        visible: parent !== null && parent.objectName === "dockToolbarSlot"
        ToolButton { objectName: "gridTool"; text: "Tool" }
    }
}
)";

// The arrangement a layout describes, without the engine's frame ids: each
// visible frame's panels, current tab and geometry, in layout order.
QStringList arrangement(const QByteArray &layout)
{
    const QJsonObject main = QJsonDocument::fromJson(layout).object().value(QStringLiteral("mainWindows")).toArray().first().toObject();
    const QJsonObject splitter = main.value(QStringLiteral("multiSplitterLayout")).toObject();
    const QJsonObject frames = splitter.value(QStringLiteral("frames")).toObject();
    QStringList out;
    std::function<void(const QJsonObject &)> walk = [&](const QJsonObject &item) {
        if (item.value(QStringLiteral("isContainer")).toBool()) {
            for (const auto &child : item.value(QStringLiteral("children")).toArray())
                walk(child.toObject());
            return;
        }
        const QJsonObject frame = frames.value(item.value(QStringLiteral("guestId")).toString()).toObject();
        const QJsonObject g = frame.value(QStringLiteral("geometry")).toObject();
        QStringList docks;
        for (const auto &d : frame.value(QStringLiteral("dockWidgets")).toArray())
            docks.append(d.toString());
        out.append(QStringLiteral("%1 tab%2 %3,%4 %5x%6")
                       .arg(docks.join(QLatin1Char('+')))
                       .arg(frame.value(QStringLiteral("currentTabIndex")).toInt())
                       .arg(g.value(QStringLiteral("x")).toInt()).arg(g.value(QStringLiteral("y")).toInt())
                       .arg(g.value(QStringLiteral("width")).toInt()).arg(g.value(QStringLiteral("height")).toInt()));
    };
    walk(splitter.value(QStringLiteral("layout")).toObject());
    return out;
}

} // namespace

class DockingQualification : public QObject {
    Q_OBJECT
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QByteArray initial; // the arrangement the shell starts with

    QObject *dock(const char *name) const { return engine->rootObjects().first()->findChild<QObject *>(QLatin1String(name)); }
    QByteArray layout() const { return KDDockWidgets::LayoutSaver().serializeLayout(); }
    hikari::ui::Docking *docking() const { return engine->singletonInstance<hikari::ui::Docking *>("Hikari.Ui", "Docking"); }

    // The visible items named `name` in `w` (the engine's views are not
    // QObject children of the window: walk the items).
    static QList<QQuickItem *> itemsNamed(QQuickWindow *w, const QString &name)
    {
        QList<QQuickItem *> out;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *i) {
            if (i->objectName() == name && i->isVisible())
                out << i;
            for (QQuickItem *c : i->childItems())
                walk(c);
        };
        walk(w->contentItem());
        return out;
    }
    static QQuickItem *childNamed(QQuickItem *from, const QString &name)
    {
        for (QQuickItem *c : from->childItems()) {
            if (c->objectName() == name)
                return c;
            if (QQuickItem *found = childNamed(c, name))
                return found;
        }
        return nullptr;
    }
    // D3: the header of the group whose current panel is `panel`, in any window.
    QQuickItem *header(const QString &panel) const
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (auto *qw = qobject_cast<QQuickWindow *>(w); qw && qw->isVisible())
                for (QQuickItem *h : itemsNamed(qw, QStringLiteral("dockHeader")))
                    if (h->property("currentName").toString() == panel)
                        return h;
        return nullptr;
    }
    static QQuickItem *groupOf(QQuickItem *item)
    {
        for (QQuickItem *p = item; p; p = p->parentItem())
            if (p->objectName() == QLatin1String("dockGroup"))
                return p;
        return nullptr;
    }
    static QPoint centreOf(QQuickItem *item)
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
    void restoreInitial()
    {
        QVERIFY(KDDockWidgets::LayoutSaver().restoreLayout(initial));
        for (const char *name : {"Video", "Audio", "Editor", "Grid"})
            QTRY_VERIFY2(dock(name)->property("isOpen").toBool() && !dock(name)->property("isFloating").toBool(), name);
        QTRY_VERIFY(header(QStringLiteral("Video")) && header(QStringLiteral("Grid")));
    }
    // A mouse event at a global point, to the window that holds it, with the
    // cursor there (the engine reads the cursor while dragging).
    static void sendMouse(QWindow *w, QEvent::Type type, QPoint global, Qt::MouseButtons buttons)
    {
        QCursor::setPos(global);
        const QPointF local = QPointF(global - w->position());
        QMouseEvent event(type, local, local, QPointF(global), Qt::LeftButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &event);
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        engine = new QQmlApplicationEngine(this);
        QVERIFY(hikari::ui::attachDocking(*engine));
        engine->loadData(QByteArray(kShell));
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(dock("Grid")->property("isOpen").toBool());
        initial = layout();
    }

    void defaultArrangementHasEveryPanelOpenAndDocked()
    {
        for (const char *name : {"Video", "Audio", "Editor", "Grid"}) {
            QObject *d = dock(name);
            QVERIFY2(d, name);
            QTRY_VERIFY2(d->property("isOpen").toBool(), name);
            QVERIFY2(!d->property("isFloating").toBool(), name);
        }
        const auto json = QJsonDocument::fromJson(layout());
        QVERIFY(json.isObject());
        const QByteArray text = layout();
        for (const char *name : {"Video", "Audio", "Editor", "Grid"})
            QVERIFY2(text.contains(name), name);
    }

    void floatsAndRedocks()
    {
        QObject *audio = dock("Audio");
        const auto windowsBefore = QGuiApplication::topLevelWindows().size();
        QVERIFY(audio->setProperty("isFloating", true));
        QTRY_VERIFY(audio->property("isFloating").toBool());
        QTRY_VERIFY(QGuiApplication::topLevelWindows().size() > windowsBefore); // its floating window
        QVERIFY(audio->setProperty("isFloating", false));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        QVERIFY(audio->property("isOpen").toBool());
    }

    void closesAndReopensWithoutLosingTheGuest()
    {
        QObject *editor = dock("Editor");
        auto *text = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("editorText"));
        QVERIFY(text);
        text->setProperty("text", QStringLiteral("draft survives"));
        QVERIFY(QMetaObject::invokeMethod(editor, "close"));
        QTRY_VERIFY(!editor->property("isOpen").toBool());
        QVERIFY(QMetaObject::invokeMethod(editor, "open"));
        QTRY_VERIFY(editor->property("isOpen").toBool());
        QCOMPARE(text->property("text").toString(), QStringLiteral("draft survives"));
    }

    void tabsTwoPanels()
    {
        QObject *grid = dock("Grid");
        auto *video = qobject_cast<QQuickItem *>(dock("Video"));
        QVERIFY(QMetaObject::invokeMethod(grid, "addDockWidgetAsTab", Q_ARG(QQuickItem *, video),
                                          Q_ARG(KDDockWidgets::InitialVisibilityOption, KDDockWidgets::InitialVisibilityOption{})));
        QTRY_VERIFY(video->property("isOpen").toBool());
        QVERIFY(QMetaObject::invokeMethod(video, "setAsCurrentTab"));
        QVERIFY(grid->property("isOpen").toBool());
    }

    // The keyboard placement dialog moves a docked panel through the same
    // calls the default arrangement uses.
    void movesADockedPanel()
    {
        QObject *area = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("area"));
        auto *audio = qobject_cast<QQuickItem *>(dock("Audio"));
        auto *grid = qobject_cast<QQuickItem *>(dock("Grid"));
        QVERIFY(area && audio && grid);
        QVERIFY(QMetaObject::invokeMethod(area, "addDockWidget", Q_ARG(QQuickItem *, audio),
                                          Q_ARG(KDDockWidgets::Location, KDDockWidgets::Location_OnLeft),
                                          Q_ARG(QQuickItem *, grid), Q_ARG(QSize, QSize()),
                                          Q_ARG(KDDockWidgets::InitialVisibilityOption, {})));
        QTRY_VERIFY(audio->property("isOpen").toBool());
        // Audio now shares the Grid's row, on its left.
        const QStringList layoutRows = arrangement(layout());
        int audioRow = -1, gridRow = -1;
        for (int i = 0; i < layoutRows.size(); ++i) {
            if (layoutRows[i].startsWith(QStringLiteral("Audio ")))
                audioRow = i;
            if (layoutRows[i].contains(QStringLiteral("Grid")))
                gridRow = i;
        }
        QVERIFY2(audioRow >= 0 && gridRow == audioRow + 1, qPrintable(layoutRows.join(QLatin1Char('|'))));
        // Tab with: into the Grid's tab group.
        QVERIFY(QMetaObject::invokeMethod(grid, "addDockWidgetAsTab", Q_ARG(QQuickItem *, audio),
                                          Q_ARG(KDDockWidgets::InitialVisibilityOption, {})));
        QTRY_VERIFY(arrangement(layout()).join(QLatin1Char('|')).contains(QStringLiteral("Audio")));
        bool tabbed = false;
        for (const QString &row : arrangement(layout()))
            tabbed = tabbed || (row.contains(QStringLiteral("Grid")) && row.contains(QStringLiteral("Audio")));
        QVERIFY2(tabbed, qPrintable(arrangement(layout()).join(QLatin1Char('|'))));
    }

    void savedLayoutRestores()
    {
        const QByteArray saved = layout();
        QObject *audio = dock("Audio");
        QObject *editor = dock("Editor");
        QVERIFY(audio->setProperty("isFloating", true));
        QVERIFY(QMetaObject::invokeMethod(editor, "close"));
        QTRY_VERIFY(!editor->property("isOpen").toBool());
        QVERIFY(KDDockWidgets::LayoutSaver().restoreLayout(saved));
        QTRY_VERIFY(editor->property("isOpen").toBool());
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        // The same arrangement comes back (frame ids are the engine's own).
        QCOMPARE(arrangement(layout()), arrangement(saved));
    }

    void corruptLayoutIsRefusedAndTheLiveOneStays()
    {
        const QByteArray before = layout();
        QVERIFY(!KDDockWidgets::LayoutSaver().restoreLayout(QByteArray("{not json")));
        QVERIFY(!KDDockWidgets::LayoutSaver().restoreLayout(QByteArray(R"({"serializationVersion": 9999})")));
        for (const char *name : {"Video", "Audio", "Editor", "Grid"})
            QVERIFY2(dock(name)->property("isOpen").toBool(), name);
        QCOMPARE(arrangement(layout()), arrangement(before));
    }

    // D3: one header row per group (MuseScore's DockFrame): a lone panel's
    // title bar (its bold title and the "⋯" button, no tab), a horizontal
    // panel's one-tab bar with its toolbar right of the tab, and a group's
    // tabs with the "⋯" button on the selected one. Each is 35 high, and the
    // content starts 8 below a tab bar (12 with the panel's margin).
    void oneHeaderRowPerGroup()
    {
        restoreInitial();
        QQuickItem *video = header(QStringLiteral("Video"));
        QVERIFY(video);
        QVERIFY(video->property("titleMode").toBool());
        QCOMPARE(video->height(), 35.0);
        QCOMPARE(video->property("contentGap").toInt(), 0);
        QQuickItem *title = childNamed(video, QStringLiteral("dockTitleText"));
        QVERIFY(title && title->isVisible());
        QCOMPARE(title->property("text").toString(), QStringLiteral("Video"));
        QVERIFY(title->property("font").value<QFont>().bold());
        QQuickItem *menu = childNamed(video, QStringLiteral("dockMenuButton"));
        QVERIFY(menu && menu->isVisible());
        QCOMPARE(menu->width(), 20.0);
        QCOMPARE(menu->height(), 20.0);
        // 12 from the header's right end
        QCOMPARE(menu->mapToItem(video, QPointF(menu->width(), 0)).x(), video->width() - 12);
        // No engine title bar shows above it.
        for (QQuickItem *bar : itemsNamed(window, QStringLiteral("dockTitleBar")))
            QVERIFY2(!bar->isVisible(), "a group shows the engine's title bar");

        // The Grid: horizontal, so a tab even alone, and its toolbar in the header.
        QQuickItem *grid = header(QStringLiteral("Grid"));
        QVERIFY(grid);
        QVERIFY(!grid->property("titleMode").toBool());
        QCOMPARE(grid->property("contentGap").toInt(), 8);
        QQuickItem *gridTab = childNamed(grid, QStringLiteral("dockTab0"));
        QVERIFY(gridTab && gridTab->isVisible());
        QCOMPARE(gridTab->property("text").toString(), QStringLiteral("Grid"));
        QVERIFY(gridTab->property("selected").toBool());
        auto *toolbar = window->findChild<QQuickItem *>(QStringLiteral("gridToolbar"));
        QVERIFY(toolbar);
        QTRY_COMPARE(toolbar->parentItem(), childNamed(grid, QStringLiteral("dockToolbarSlot")));
        QVERIFY(toolbar->isVisible());
        QVERIFY(toolbar->mapToItem(grid, QPointF()).x() > gridTab->mapToItem(grid, QPointF(gridTab->width(), 0)).x());
        // The content starts below the header and the gap.
        QQuickItem *gridGroup = groupOf(grid);
        auto *gridBody = window->findChild<QQuickItem *>(QStringLiteral("gridBody"));
        QVERIFY(gridGroup && gridBody);
        QCOMPARE(gridBody->mapToItem(gridGroup, QPointF()).y(), 35.0 + 8.0);
        auto *videoBody = window->findChild<QQuickItem *>(QStringLiteral("videoBody"));
        QCOMPARE(videoBody->mapToItem(groupOf(video), QPointF()).y(), 35.0);

        // A group: tabs only, the "⋯" button on the selected tab.
        auto *editor = dock("Editor");
        auto *audio = qobject_cast<QQuickItem *>(dock("Audio"));
        QVERIFY(QMetaObject::invokeMethod(editor, "addDockWidgetAsTab", Q_ARG(QQuickItem *, audio),
                                          Q_ARG(KDDockWidgets::InitialVisibilityOption, {})));
        QTRY_VERIFY(header(QStringLiteral("Audio")) || header(QStringLiteral("Editor")));
        QQuickItem *group = header(QStringLiteral("Audio")) ? header(QStringLiteral("Audio")) : header(QStringLiteral("Editor"));
        QTRY_COMPARE(group->property("count").toInt(), 2);
        QVERIFY(!group->property("titleMode").toBool());
        QVERIFY(!childNamed(group, QStringLiteral("dockTitleText"))->isVisible());
        QQuickItem *tab0 = childNamed(group, QStringLiteral("dockTab0"));
        QQuickItem *tab1 = childNamed(group, QStringLiteral("dockTab1"));
        QVERIFY(tab0 && tab1);
        QQuickItem *selected = tab0->property("selected").toBool() ? tab0 : tab1;
        QQuickItem *other = selected == tab0 ? tab1 : tab0;
        QVERIFY(!other->property("selected").toBool());
        QQuickItem *groupMenu = childNamed(group, QStringLiteral("dockMenuButton"));
        const QRectF onSelected = selected->mapRectToItem(group, QRectF(0, 0, selected->width(), selected->height()));
        const QRectF button = groupMenu->mapRectToItem(group, QRectF(0, 0, groupMenu->width(), groupMenu->height()));
        QVERIFY2(onSelected.contains(button), "the ⋯ button sits on the selected tab");
        // tab padding: 10 | label | 6 | ⋯ | 6 + 1
        QCOMPARE(onSelected.right() - button.right(), 7.0);
        // Selecting the other tab moves the button there.
        group->setProperty("currentTabIndex", other == tab0 ? 0 : 1);
        QTRY_VERIFY(other->property("selected").toBool());
        const QRectF onOther = other->mapRectToItem(group, QRectF(0, 0, other->width(), other->height()));
        QTRY_VERIFY(onOther.contains(groupMenu->mapRectToItem(group, QRectF(0, 0, 20, 20))));
    }

    // D3: the header names itself, its tabs and its button for assistive
    // technology: a title bar by its panel, the tabs as page tabs with their
    // selection, the button "<panel> options".
    void headersAreAccessible()
    {
        restoreInitial();
        QAccessible::setActive(true);
        const auto inactive = qScopeGuard([] { QAccessible::setActive(false); });
        QQuickItem *video = header(QStringLiteral("Video"));
        QAccessibleInterface *bar = QAccessible::queryAccessibleInterface(video);
        QVERIFY(bar);
        QCOMPARE(bar->role(), QAccessible::TitleBar);
        QCOMPARE(bar->text(QAccessible::Name), QStringLiteral("Video"));
        QAccessibleInterface *menu = QAccessible::queryAccessibleInterface(childNamed(video, QStringLiteral("dockMenuButton")));
        QVERIFY(menu);
        QCOMPARE(menu->role(), QAccessible::Button);
        QCOMPARE(menu->text(QAccessible::Name), QStringLiteral("Video options"));
        QVERIFY(menu->actionInterface());
        // A lone panel's header is not a tab list: its tab is not shown.
        QVERIFY(!childNamed(video, QStringLiteral("dockTab0"))->isVisible());

        QQuickItem *grid = header(QStringLiteral("Grid"));
        QAccessibleInterface *list = QAccessible::queryAccessibleInterface(grid);
        QCOMPARE(list->role(), QAccessible::PageTabList);
        QAccessibleInterface *tab = QAccessible::queryAccessibleInterface(childNamed(grid, QStringLiteral("dockTab0")));
        QVERIFY(tab);
        QCOMPARE(tab->role(), QAccessible::PageTab);
        QCOMPARE(tab->text(QAccessible::Name), QStringLiteral("Grid"));
        QVERIFY(tab->state().checked);
        QCOMPARE(QAccessible::queryAccessibleInterface(childNamed(grid, QStringLiteral("dockMenuButton")))->text(QAccessible::Name),
                 QStringLiteral("Grid options"));
    }

    // D3: the "⋯" button, a right click and the keyboard ask for the panel's
    // menu (Main.qml owns it). The keyboard reaches the header from the
    // panel (Shift+Tab: the selected tab of a group, a title bar's button),
    // Right goes from the selected tab to the button, Left back, and Space,
    // Enter, the menu key and Shift+F10 open it.
    void theMenuIsReachedByPointerAndKeyboard()
    {
        restoreInitial();
        QSignalSpy asked(docking(), &hikari::ui::Docking::menuRequested);
        // the button
        QQuickItem *video = header(QStringLiteral("Video"));
        QQuickItem *videoMenu = childNamed(video, QStringLiteral("dockMenuButton"));
        QTest::mouseClick(window, Qt::LeftButton, {}, centreOf(videoMenu));
        QTRY_COMPARE(asked.size(), 1);
        QCOMPARE(asked.at(0).at(0).toString(), QStringLiteral("Video"));
        QCOMPARE(asked.at(0).at(1).value<QQuickItem *>(), videoMenu);
        // a right click on the header
        QTest::mouseClick(window, Qt::RightButton, {}, video->mapToScene(QPointF(60, video->height() / 2)).toPoint());
        QTRY_COMPARE(asked.size(), 2);
        QCOMPARE(asked.at(1).at(0).toString(), QStringLiteral("Video"));

        // A title bar: Shift+Tab from the panel's first control reaches its button.
        QQuickItem *editorButton = window->findChild<QQuickItem *>(QStringLiteral("editorButton"));
        QVERIFY(editorButton);
        editorButton->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
        QQuickItem *editor = header(QStringLiteral("Editor"));
        QQuickItem *editorMenu = childNamed(editor, QStringLiteral("dockMenuButton"));
        QTRY_VERIFY(editorMenu->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(asked.size(), 3);
        QCOMPARE(asked.at(2).at(0).toString(), QStringLiteral("Editor"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(asked.size(), 4);

        // A group: Shift+Tab reaches the selected tab, Right the button.
        auto *audio = qobject_cast<QQuickItem *>(dock("Audio"));
        QVERIFY(QMetaObject::invokeMethod(dock("Editor"), "addDockWidgetAsTab", Q_ARG(QQuickItem *, audio),
                                          Q_ARG(KDDockWidgets::InitialVisibilityOption, {})));
        QVERIFY(QMetaObject::invokeMethod(dock("Editor"), "setAsCurrentTab"));
        QTRY_VERIFY(header(QStringLiteral("Editor")) && header(QStringLiteral("Editor"))->property("count").toInt() == 2);
        QTRY_VERIFY(editorButton->isVisible());
        editorButton->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
        QQuickItem *group = header(QStringLiteral("Editor"));
        QQuickItem *selected = nullptr;
        for (const char *name : {"dockTab0", "dockTab1"})
            if (QQuickItem *tab = childNamed(group, QLatin1String(name)); tab->property("selected").toBool())
                selected = tab;
        QVERIFY(selected);
        QTRY_VERIFY2(selected->hasActiveFocus(), window->activeFocusItem()
                     ? qPrintable(QStringLiteral("%1 %2").arg(QString::fromLatin1(window->activeFocusItem()->metaObject()->className()),
                                                            window->activeFocusItem()->objectName()))
                     : "nothing");
        QVERIFY(childNamed(selected, QStringLiteral("tabFocusRing"))->isVisible());
        QQuickItem *groupMenu = childNamed(group, QStringLiteral("dockMenuButton"));
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(groupMenu->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(selected->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_F10, Qt::ShiftModifier);
        QTRY_COMPARE(asked.size(), 5);
        QCOMPARE(asked.at(4).at(0).toString(), QStringLiteral("Editor"));
        QTest::keyClick(window, Qt::Key_Menu);
        QTRY_COMPARE(asked.size(), 6);
        // Left and Right on the tabs select the neighbouring panel.
        const int before = group->property("currentTabIndex").toInt();
        QTest::keyClick(window, before == 0 ? Qt::Key_Right : Qt::Key_Left);
        if (before == 0) { // from tab 0, Right went to the button first
            QTRY_VERIFY(groupMenu->hasActiveFocus());
            QTest::keyClick(window, Qt::Key_Right);
        }
        QTRY_VERIFY(group->property("currentTabIndex").toInt() != before);
    }

    // D3: a double-click on a header floats its panel, and on the floating
    // panel's header docks it again (MuseScore's DockTitleBar/DockTabBar).
    void doubleClickFloatsAndDocks()
    {
        restoreInitial();
        auto *video = dock("Video");
        QQuickItem *bar = header(QStringLiteral("Video"));
        QTest::mouseDClick(window, Qt::LeftButton, {}, bar->mapToScene(QPointF(60, bar->height() / 2)).toPoint());
        QTRY_VERIFY(video->property("isFloating").toBool());
        QQuickItem *floatingBar = nullptr;
        QTRY_VERIFY((floatingBar = header(QStringLiteral("Video"))) && floatingBar->window() != window);
        QTRY_VERIFY(floatingBar->window()->isExposed());
        QTest::mouseDClick(floatingBar->window(), Qt::LeftButton, {},
                           floatingBar->mapToScene(QPointF(60, floatingBar->height() / 2)).toPoint());
        QTRY_VERIFY(!video->property("isFloating").toBool());
        QTRY_COMPARE(header(QStringLiteral("Video"))->window(), window);
        // A one-tab bar (the Grid) floats from its tab too.
        QQuickItem *grid = header(QStringLiteral("Grid"));
        QTest::mouseDClick(window, Qt::LeftButton, {}, centreOf(childNamed(grid, QStringLiteral("dockTab0"))));
        QTRY_VERIFY(dock("Grid")->property("isFloating").toBool());
        QVERIFY(docking()->toggleFloating(QStringLiteral("Grid")));
        QTRY_VERIFY(!dock("Grid")->property("isFloating").toBool());
    }

    // D3: a floating panel is a borderless tool window around its header,
    // with a drawn shadow, on every platform (MuseScore's DockFloatingWindow).
    void floatingPanelsAreBorderlessToolWindows()
    {
        // where the window system composites (X11 without a compositing
        // manager: floatingPanelsWithoutCompositingHaveNoShadow)
        hikari::ui::dockchrome::overrideCompositing(true);
        const auto unset = qScopeGuard([] { hikari::ui::dockchrome::overrideCompositing(std::nullopt); });
        restoreInitial();
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        QWindow *floating = bar->window();
        QVERIFY(floating->flags().testFlag(Qt::Tool));
        QCOMPARE(floating->transientParent(), window);
        const int shadow = hikari::ui::dockchrome::floatingShadow();
        QVERIFY(floating->flags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(shadow, 8);
        QCOMPARE(docking()->property("floatingShadow").toInt(), 8);
        // transparent around the frame, where the shadow is drawn
        QCOMPARE(qobject_cast<QQuickWindow *>(floating)->color().alpha(), 0);
        // The header keeps its look: a lone panel's title bar, inside the
        // shadow and the 1-pixel frame.
        QVERIFY(bar->property("titleMode").toBool());
        QCOMPARE(bar->mapToScene(QPointF()).toPoint(), QPoint(shadow + 1, shadow + 1));
        QVERIFY(bar->property("floating").toBool());
        QVERIFY(audio->setProperty("isFloating", false));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
    }

    // Native gate (X11 without a compositing manager): a transparent
    // window's margin shows black there, so a floating panel draws no
    // shadow; its frame fills the window and a band inside the frame's edge
    // still resizes it.
    void floatingPanelsWithoutCompositingHaveNoShadow()
    {
        hikari::ui::dockchrome::overrideCompositing(false);
        const auto unset = qScopeGuard([] { hikari::ui::dockchrome::overrideCompositing(std::nullopt); });
        QCOMPARE(hikari::ui::dockchrome::floatingShadow(), 0);
        restoreInitial();
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        auto *floating = qobject_cast<QQuickWindow *>(bar->window());
        QVERIFY(floating);
        QQuickItem *frame = itemsNamed(floating, QStringLiteral("dockFloatingFrame")).value(0);
        QVERIFY(frame);
        QTRY_COMPARE(frame->size(), QSizeF(floating->size()));
        QCOMPARE(frame->mapToScene(QPointF()), QPointF());
        QCOMPARE(frame->property("radius").toReal(), 0.0);
        QTRY_COMPARE(bar->mapToScene(QPointF()).toPoint(), QPoint(1, 1));
        for (const char *name : {"resizeLeft", "resizeRight", "resizeTop", "resizeBottom"}) {
            QQuickItem *edge = itemsNamed(floating, QLatin1String(name)).value(0);
            QVERIFY2(edge && edge->isEnabled() && edge->isVisible(), name);
            QCOMPARE(qMin(edge->width(), edge->height()), qreal(hikari::ui::dockchrome::kResizeGrip));
        }
        QQuickItem *right = itemsNamed(floating, QStringLiteral("resizeRight")).first();
        QCOMPARE(right->mapToScene(QPointF(right->width(), 0)).x(), qreal(floating->width()));
        QVERIFY(audio->setProperty("isFloating", false));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
    }

    // D3: separators are 1 pixel wide, in the theme's boundary colour.
    void separatorsAreOnePixel()
    {
        restoreInitial();
        const QList<QQuickItem *> separators = itemsNamed(window, QStringLiteral("dockSeparator"));
        QVERIFY(!separators.isEmpty());
        for (QQuickItem *separator : separators)
            QVERIFY2(qMin(separator->width(), separator->height()) == 1.0,
                     qPrintable(QStringLiteral("%1x%2").arg(separator->width()).arg(separator->height())));
        // a hit area 5 wider on each side
        QQuickItem *handle = childNamed(separators.first(), QStringLiteral("dockSeparatorHandle"));
        QVERIFY(handle);
        QCOMPARE(qMin(handle->width(), handle->height()), 11.0);
    }

    // D3: a panel's minimum size (Docking.setMinimumSize) holds its group:
    // the layout gives it room, and a resize cannot take that away.
    void minimumSizesHoldTheLayout()
    {
        restoreInitial();
        QVERIFY(docking()->setMinimumSize(QStringLiteral("Editor"), QSize(300, 260)));
        QCOMPARE(docking()->minimumSize(QStringLiteral("Editor")), QSize(300, 260));
        QQuickItem *editor = header(QStringLiteral("Editor"));
        QQuickItem *group = groupOf(editor);
        QTRY_VERIFY2(group->height() >= 260 + 35, qPrintable(QString::number(group->height())));
        QVERIFY(group->width() >= 300);
        QVERIFY(docking()->resizeInLayout(QStringLiteral("Editor"), 0, -200, 0, -200));
        QTest::qWait(50);
        QVERIFY2(group->height() >= 260 + 35, qPrintable(QString::number(group->height())));
        // A restored layout takes the minimum again.
        QVERIFY(KDDockWidgets::LayoutSaver().restoreLayout(initial));
        hikari::ui::refreshDockConstraints();
        QTRY_VERIFY(groupOf(header(QStringLiteral("Editor"))) && groupOf(header(QStringLiteral("Editor")))->height() >= 260 + 35);
        QVERIFY(docking()->setMinimumSize(QStringLiteral("Editor"), QSize()));
        hikari::ui::refreshDockConstraints();
    }

    // D3: while a panel is dragged, the accent highlight covers the area a
    // drop takes (MuseScore's: 1-pixel border, 30 % fill): the half of the
    // group under the pointer for a side, all of it over its header (a tab).
    // The drop lands there. Synthetic drags across windows need the
    // offscreen platform's cursor; the native gate drags for real.
    void dragShowsTheDropHighlight()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("a synthetic drag between windows needs the offscreen platform (the native gate drags for real)");
        restoreInitial();
        QQuickItem *audio = header(QStringLiteral("Audio"));
        QQuickItem *gridGroup = groupOf(header(QStringLiteral("Grid")));
        QVERIFY(audio && gridGroup);
        const QRectF g = gridGroup->mapRectToScene(QRectF(0, 0, gridGroup->width(), gridGroup->height()));
        const QPoint start = window->mapToGlobal(audio->mapToScene(QPointF(60, audio->height() / 2)).toPoint());
        const QPoint left = window->mapToGlobal(QPointF(g.left() + g.width() / 8, g.center().y()).toPoint());
        const QPoint tab = window->mapToGlobal(QPointF(g.center().x(), g.top() + 10).toPoint());
        QWindow *receiver = window;
        const auto moveTo = [&](QPoint from, QPoint to) {
            for (int i = 1; i <= 12; ++i) {
                sendMouse(receiver, QEvent::MouseMove, from + (to - from) * i / 12, Qt::LeftButton);
                if (QWindow *w = header(QStringLiteral("Audio"))->window(); w != window)
                    receiver = w;
            }
        };
        sendMouse(window, QEvent::MouseButtonPress, start, Qt::LeftButton);
        moveTo(start, left);
        QTRY_VERIFY(dock("Audio")->property("isFloating").toBool());
        QQuickItem *highlight = nullptr;
        QTRY_VERIFY(!(itemsNamed(window, QStringLiteral("dockDropHighlight"))).isEmpty());
        highlight = itemsNamed(window, QStringLiteral("dockDropHighlight")).first();
        const QRectF h = highlight->mapRectToScene(QRectF(0, 0, highlight->width(), highlight->height()));
        QVERIFY2(qAbs(h.left() - g.left()) <= 1 && qAbs(h.width() - g.width() / 2) <= 2 && qAbs(h.height() - g.height()) <= 2,
                 qPrintable(QStringLiteral("highlight %1,%2 %3x%4 group %5,%6 %7x%8").arg(h.x()).arg(h.y()).arg(h.width())
                                .arg(h.height()).arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height())));
        QCOMPARE(highlight->property("border").value<QObject *>()->property("width").toInt(), 1);
        const QColor fill = highlight->property("color").value<QColor>();
        QCOMPARE(qRound(fill.alphaF() * 10), 3);
        QCOMPARE(fill.rgb(), highlight->property("border").value<QObject *>()->property("color").value<QColor>().rgb());
        // Over the group's header: a tab, the whole group.
        moveTo(left, tab);
        QTRY_VERIFY(highlight->isVisible());
        QTRY_VERIFY(qAbs(highlight->mapRectToScene(QRectF(0, 0, highlight->width(), highlight->height())).width() - g.width()) <= 2);
        {
            const QRectF all = highlight->mapRectToScene(QRectF(0, 0, highlight->width(), highlight->height()));
            QVERIFY2(qAbs(all.left() - g.left()) <= 1 && qAbs(all.top() - g.top()) <= 1 && qAbs(all.height() - g.height()) <= 2,
                     qPrintable(QStringLiteral("%1,%2 %3x%4").arg(all.x()).arg(all.y()).arg(all.width()).arg(all.height())));
        }
        // Back to the left half, and drop there.
        moveTo(tab, left);
        sendMouse(receiver, QEvent::MouseButtonRelease, left, Qt::NoButton);
        QTRY_VERIFY(!dock("Audio")->property("isFloating").toBool());
        QTRY_VERIFY(!highlight->isVisible());
        bool beside = false;
        for (const QString &row : arrangement(layout()))
            beside = beside || row.startsWith(QStringLiteral("Audio "));
        QVERIFY(beside);
        QQuickItem *audioGroup = groupOf(header(QStringLiteral("Audio")));
        QTRY_VERIFY(audioGroup->mapToScene(QPointF()).x() < gridGroup->mapToScene(QPointF()).x() + 2);
    }

    // D3, Wayland (forced here: Docking.setSystemMove): a floating lone
    // panel's header keeps both gestures. Its title is the engine's drag,
    // which shows the drop highlight and docks; the header right of it
    // moves the window through the compositor, only once the pointer has
    // moved, so a click there does not hand the pointer over and a
    // double-click there docks the panel.
    void waylandHeaderDocksByItsTitleAndMovesByTheRest()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("a synthetic drag between windows needs the offscreen platform (the native gate drags for real)");
        restoreInitial();
        hikari::ui::Docking *d = docking();
        d->setSystemMove(true);
        const auto restore = qScopeGuard([d] { d->setSystemMove(hikari::ui::Docking::platformNeedsSystemMove()); });
        QSignalSpy moves(d, &hikari::ui::Docking::systemMoveRequested);
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        QWindow *floating = bar->window();
        QTRY_VERIFY(floating->isExposed());
        QVERIFY(bar->property("titleMode").toBool());
        QQuickItem *title = childNamed(bar, QStringLiteral("dockTitleText"));
        QQuickItem *moveArea = childNamed(bar, QStringLiteral("dockSystemMoveArea"));
        QQuickItem *button = childNamed(bar, QStringLiteral("dockMenuButton"));
        QVERIFY(title && moveArea && button);
        QVERIFY(moveArea->isVisible());
        // the move area starts after the title and ends before the button
        QVERIFY2(moveArea->x() >= title->x() + title->property("contentWidth").toReal(),
                 qPrintable(QStringLiteral("%1 %2").arg(moveArea->x()).arg(title->property("contentWidth").toReal())));
        QVERIFY(moveArea->x() + moveArea->width() <= button->x());
        QVERIFY(moveArea->width() >= 48);
        const QPoint free = moveArea->mapToScene(QPointF(moveArea->width() / 2, moveArea->height() / 2)).toPoint();
        // A click on the free part: no move.
        QTest::mouseClick(floating, Qt::LeftButton, {}, free);
        QCOMPARE(moves.count(), 0);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50); // not a double-click
        // A press and a move there: the compositor's move, the panel still floating.
        QTest::mousePress(floating, Qt::LeftButton, {}, free);
        QTest::mouseMove(floating, free + QPoint(30, 4));
        QTRY_COMPARE(moves.count(), 1);
        QCOMPARE(moves.first().first().value<QWindow *>(), floating);
        QTest::mouseRelease(floating, Qt::LeftButton, {}, free + QPoint(30, 4));
        QVERIFY(audio->property("isFloating").toBool());
        // A double-click there docks it.
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseDClick(floating, Qt::LeftButton, {}, free);
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        QCOMPARE(moves.count(), 1);

        // Floating again, the title drags with the drop highlight and docks
        // left of the Grid.
        QVERIFY(audio->setProperty("isFloating", true));
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        floating = bar->window();
        QTRY_VERIFY(floating->isExposed());
        title = childNamed(bar, QStringLiteral("dockTitleText"));
        QQuickItem *gridGroup = groupOf(header(QStringLiteral("Grid")));
        const QRectF g = gridGroup->mapRectToScene(QRectF(0, 0, gridGroup->width(), gridGroup->height()));
        const QPoint start = floating->mapToGlobal(title->mapToScene(QPointF(6, title->height() / 2)).toPoint());
        const QPoint left = window->mapToGlobal(QPointF(g.left() + g.width() / 8, g.center().y()).toPoint());
        sendMouse(floating, QEvent::MouseButtonPress, start, Qt::LeftButton);
        // a few pixels along the title first, as a hand does: the drag starts there
        for (int i = 1; i <= 4; ++i)
            sendMouse(floating, QEvent::MouseMove, start + QPoint(4 * i, 0), Qt::LeftButton);
        const QPoint from = start + QPoint(16, 0);
        for (int i = 1; i <= 12; ++i)
            sendMouse(floating, QEvent::MouseMove, from + (left - from) * i / 12, Qt::LeftButton);
        QTRY_VERIFY(!itemsNamed(window, QStringLiteral("dockDropHighlight")).isEmpty());
        sendMouse(floating, QEvent::MouseButtonRelease, left, Qt::NoButton);
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        QCOMPARE(moves.count(), 1); // the title never moved the window itself
        QQuickItem *audioGroup = groupOf(header(QStringLiteral("Audio")));
        QTRY_VERIFY(audioGroup->mapToScene(QPointF()).x() < gridGroup->mapToScene(QPointF()).x() + 2);
    }

    // D3: a floating window holding several groups shows the engine's title
    // bar (DockTitleBar.qml): the window's title in bold, the move cursor,
    // and a "⋯" menu of the window's own: Dock, which docks its panels, and
    // Close, which closes them. No other header of the window repeats it.
    void floatingWindowOfGroupsHasItsOwnMenu()
    {
        restoreInitial();
        auto *video = dock("Video");
        auto *audio = dock("Audio");
        QVERIFY(video->setProperty("isFloating", true));
        QQuickItem *videoBar = nullptr;
        QTRY_VERIFY((videoBar = header(QStringLiteral("Video"))) && videoBar->window() != window);
        auto *floating = qobject_cast<QQuickWindow *>(videoBar->window());
        QVERIFY(QMetaObject::invokeMethod(window, "pairFloating"));
        QTRY_VERIFY(header(QStringLiteral("Audio")) && header(QStringLiteral("Audio"))->window() == floating);
        QQuickItem *titleBar = nullptr;
        QTRY_VERIFY(!itemsNamed(floating, QStringLiteral("dockTitleBar")).isEmpty());
        titleBar = itemsNamed(floating, QStringLiteral("dockTitleBar")).first();
        QCOMPARE(titleBar->height(), 35.0);
        QQuickItem *button = childNamed(titleBar, QStringLiteral("dockMenuButton"));
        QVERIFY(button && button->isVisible());
        QCOMPARE(button->property("contentItem").value<QObject *>()->property("iconRole").toString(), QStringLiteral("panel-menu"));
        QTRY_VERIFY(floating->isExposed());
        QObject *menu = nullptr;
        for (QObject *o : titleBar->findChildren<QObject *>())
            if (o->objectName() == QLatin1String("dockWindowMenu"))
                menu = o;
        QVERIFY(menu);
        QTest::mouseClick(floating, Qt::LeftButton, {}, centreOf(button));
        QTRY_VERIFY(menu->property("visible").toBool());
        QStringList items;
        for (int i = 0; i < menu->property("count").toInt(); ++i) {
            QQuickItem *entry = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, i));
            if (entry)
                items << entry->property("text").toString() + (entry->isEnabled() ? QString() : QStringLiteral(" (off)"));
        }
        QCOMPARE(items, (QStringList{QStringLiteral("Dock"), QStringLiteral("Close")}));
        QObject *dockItem = nullptr;
        for (QObject *o : titleBar->findChildren<QObject *>())
            if (o->objectName() == QLatin1String("dockWindowDock"))
                dockItem = o;
        QVERIFY(dockItem);
        QVERIFY(QMetaObject::invokeMethod(dockItem, "triggered"));
        QTRY_VERIFY(!video->property("isFloating").toBool() && !audio->property("isFloating").toBool());
        QTRY_COMPARE(header(QStringLiteral("Video"))->window(), window);
        QTRY_COMPARE(header(QStringLiteral("Audio"))->window(), window);

        // Again, and Close closes both.
        QVERIFY(video->setProperty("isFloating", true));
        QTRY_VERIFY((videoBar = header(QStringLiteral("Video"))) && videoBar->window() != window);
        floating = qobject_cast<QQuickWindow *>(videoBar->window());
        QVERIFY(QMetaObject::invokeMethod(window, "pairFloating"));
        QTRY_VERIFY(header(QStringLiteral("Audio")) && header(QStringLiteral("Audio"))->window() == floating);
        QTRY_VERIFY(!itemsNamed(floating, QStringLiteral("dockTitleBar")).isEmpty());
        titleBar = itemsNamed(floating, QStringLiteral("dockTitleBar")).first();
        QObject *closeItem = nullptr;
        for (QObject *o : titleBar->findChildren<QObject *>())
            if (o->objectName() == QLatin1String("dockWindowClose"))
                closeItem = o;
        QVERIFY(closeItem && closeItem->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(closeItem, "triggered"));
        QTRY_VERIFY(!video->property("isOpen").toBool() && !audio->property("isOpen").toBool());
    }

    // D3: tabs that do not fit (MuseScore's DockTabBar): the selected tab
    // keeps its width, the others share what is left in proportion, cut
    // off under a 20-pixel fade, and the tabs never run past the header.
    void crowdedTabsKeepTheSelectedOneWhole()
    {
        restoreInitial();
        QVERIFY(QMetaObject::invokeMethod(window, "tabIntoVideo"));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Editor"))) && bar->property("count").toInt() == 3);
        // Wide: every tab at its natural width, none cut off.
        for (int i = 0; i < 3; ++i) {
            QQuickItem *tab = childNamed(bar, QStringLiteral("dockTab%1").arg(i));
            QCOMPARE(tab->width(), tab->property("naturalWidth").toReal());
            QVERIFY(!tab->property("cutOff").toBool());
        }
        // Narrow: below the tabs' natural widths.
        qreal natural = 0;
        for (int i = 0; i < 3; ++i)
            natural += childNamed(bar, QStringLiteral("dockTab%1").arg(i))->property("naturalWidth").toReal();
        QQuickItem *group = groupOf(bar);
        QTRY_VERIFY(groupOf(header(QStringLiteral("Grid")))->mapToScene(QPointF()).x() > group->mapToScene(QPointF()).x());
        QVERIFY(docking()->resizeInLayout(QStringLiteral("Grid"), int(group->width() - natural * 0.75), 0, 0, 0));
        QTRY_VERIFY2(bar->width() < natural, qPrintable(QStringLiteral("%1 %2").arg(bar->width()).arg(natural)));
        const int selected = bar->property("currentTabIndex").toInt();
        qreal used = 0;
        for (int i = 0; i < 3; ++i) {
            QQuickItem *tab = childNamed(bar, QStringLiteral("dockTab%1").arg(i));
            used += tab->width();
            QQuickItem *fade = childNamed(tab, QStringLiteral("dockTabFade"));
            if (i == selected) {
                QCOMPARE(tab->width(), tab->property("naturalWidth").toReal());
                QVERIFY(!fade->isVisible());
            } else {
                QVERIFY(tab->property("cutOff").toBool());
                QVERIFY(tab->width() < tab->property("naturalWidth").toReal());
                QVERIFY(tab->clip());
                QVERIFY(fade->isVisible());
                QCOMPARE(fade->width(), qMin(20.0, tab->width() - 1));
            }
        }
        QVERIFY2(used <= bar->width() + 0.5, qPrintable(QStringLiteral("%1 > %2").arg(used).arg(bar->width())));
    }

};

QTEST_MAIN(DockingQualification)
#include "docking_qualification_tests.moc"
