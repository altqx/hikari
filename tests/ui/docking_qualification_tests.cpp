#include "docking.h"
#include "left_button_held.h"
#include "window_decorations.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/views/FloatingWindow.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <utility>
#include <QAccessible>
#include <QFont>
#include <QFontMetricsF>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStyleHints>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qwindowsysteminterface.h>

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

QList<QObject *> expandedChanges;

void captureExpandedChanges(QAccessibleEvent *event)
{
    if (event->type() == QAccessible::StateChanged &&
        static_cast<QAccessibleStateChangeEvent *>(event)->changedStates().expanded)
        expandedChanges.append(event->object());
}

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

}

class DockingQualification : public QObject {
    Q_OBJECT
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QByteArray initial;

    QObject *dock(const char *name) const { return engine->rootObjects().first()->findChild<QObject *>(QLatin1String(name)); }
    QByteArray layout() const { return KDDockWidgets::LayoutSaver().serializeLayout(); }
    hikari::ui::Docking *docking() const { return engine->singletonInstance<hikari::ui::Docking *>("Hikari.Ui", "Docking"); }

    // The engine's views are not QObject children of the window: walk the items.
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
    // At a fractional scale a window's position is rounded to device pixels.
    static bool near(QPoint position, QPoint expected)
    {
        return qAbs(position.x() - expected.x()) <= 1 && qAbs(position.y() - expected.y()) <= 1;
    }
    static QString moved(const QWindow *w, QPoint before)
    {
        return QStringLiteral("moved by %1,%2").arg(w->x() - before.x()).arg(w->y() - before.y());
    }
    // A window manager can map a new window where it places it, then move it where asked.
    static bool holdsItsPosition(QWindow *w, int quiet = 300)
    {
        const QPoint at = w->position();
        QTest::qWait(quiet);
        return w->position() == at;
    }
    // KDDockWidgets reads the cursor while dragging; Qt Quick drops a move event that names a button.
    static void sendMouse(QWindow *w, QEvent::Type type, QPoint global, Qt::MouseButtons buttons)
    {
        QCursor::setPos(global);
        const QPointF local = QPointF(global - w->position());
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, local, local, QPointF(global), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &event);
        QCoreApplication::processEvents();
    }

    // Qt changes QWindow::width at a scale change without a signal: catch it in the event.
    struct WidthAtScaleChange : QObject {
        explicit WidthAtScaleChange(QWindow *window)
        {
            window->installEventFilter(this);
        }
        bool eventFilter(QObject *object, QEvent *event) override
        {
            if (event->type() == QEvent::DevicePixelRatioChange && width < 0)
                width = static_cast<QWindow *>(object)->width();
            return false;
        }
        int width = -1;
    };

    // Offscreen QPA only: its native interface gets and sets the screens' configuration.
    static bool configureScreens(const std::function<void(QJsonArray &)> &edit)
    {
        QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
        using Get = QJsonObject (*)(QPlatformNativeInterface *);
        using Set = void (*)(const QJsonObject &, QPlatformNativeInterface *);
        auto get = reinterpret_cast<Get>(native ? native->nativeResourceForIntegration("configuration") : nullptr);
        auto set = reinterpret_cast<Set>(native ? native->nativeResourceForIntegration("setConfiguration") : nullptr);
        if (!get || !set)
            return false;
        QJsonObject config = get(native);
        QJsonArray screens = config.value(QStringLiteral("screens")).toArray();
        if (screens.isEmpty())
            return false;
        edit(screens);
        config.insert(QStringLiteral("screens"), screens);
        set(config, native);
        QWindowSystemInterface::flushWindowSystemEvents();
        return true;
    }
    static bool setScreenDpi(int dpi)
    {
        return configureScreens([dpi](QJsonArray &screens) {
            QJsonObject first = screens.first().toObject();
            first.insert(QStringLiteral("logicalDpi"), dpi);
            screens.replace(0, first);
        });
    }
    static bool setSideScreen(const QString &name, int dpi)
    {
        return configureScreens([&](QJsonArray &screens) {
            for (qsizetype i = screens.size() - 1; i > 0; --i)
                if (screens.at(i).toObject().value(QStringLiteral("name")).toString() == name)
                    screens.removeAt(i);
            if (dpi <= 0)
                return;
            const QJsonObject first = screens.first().toObject();
            screens.append(QJsonObject{{QStringLiteral("name"), name},
                                       {QStringLiteral("x"), first.value(QStringLiteral("x")).toInt()
                                                                 + first.value(QStringLiteral("width")).toInt()},
                                       {QStringLiteral("y"), 0},
                                       {QStringLiteral("width"), 1920},
                                       {QStringLiteral("height"), 1080},
                                       {QStringLiteral("logicalDpi"), dpi},
                                       {QStringLiteral("logicalBaseDpi"), 96},
                                       {QStringLiteral("dpr"), 1.0}});
        });
    }
    static QScreen *screenNamed(const QString &name)
    {
        for (QScreen *s : QGuiApplication::screens())
            if (s->name() == name)
                return s;
        return nullptr;
    }

    QObject *area() const { return engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("area")); }
    static QRectF sceneRect(QQuickItem *item)
    {
        return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    }
    QQuickItem *separatorThrough(Qt::Orientation line, QPointF point) const
    {
        for (QQuickItem *separator : itemsNamed(window, QStringLiteral("dockSeparator"))) {
            const QRectF r = sceneRect(separator);
            if ((line == Qt::Vertical) == (r.height() > r.width()) && r.adjusted(-3, -3, 3, 3).contains(point))
                return separator;
        }
        return nullptr;
    }
    void dragFrom(QPoint from, QPoint by)
    {
        const LeftButtonHeld held;
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(window, from + by * i / 10);
        QTest::mouseRelease(window, Qt::LeftButton, {}, from + by);
    }

    static void dragWindow(QWindow *w, QPoint from, QPoint by)
    {
        QTest::mousePress(w, Qt::LeftButton, {}, from);
        QTest::mouseMove(w, from + by);
        QTest::mouseRelease(w, Qt::LeftButton, {}, from + by);
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
    void cleanupTestCase()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("TypeError")));
        delete std::exchange(engine, nullptr);
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
        QTRY_VERIFY(QGuiApplication::topLevelWindows().size() > windowsBefore);
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
        const QStringList layoutRows = arrangement(layout());
        int audioRow = -1, gridRow = -1;
        for (int i = 0; i < layoutRows.size(); ++i) {
            if (layoutRows[i].startsWith(QStringLiteral("Audio ")))
                audioRow = i;
            if (layoutRows[i].contains(QStringLiteral("Grid")))
                gridRow = i;
        }
        QVERIFY2(audioRow >= 0 && gridRow == audioRow + 1, qPrintable(layoutRows.join(QLatin1Char('|'))));
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
        QCOMPARE(menu->mapToItem(video, QPointF(menu->width(), 0)).x(), video->width() - 12);
        for (QQuickItem *bar : itemsNamed(window, QStringLiteral("dockTitleBar")))
            QVERIFY2(!bar->isVisible(), "a group shows the engine's title bar");

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
        QQuickItem *gridGroup = groupOf(grid);
        auto *gridBody = window->findChild<QQuickItem *>(QStringLiteral("gridBody"));
        QVERIFY(gridGroup && gridBody);
        QCOMPARE(gridBody->mapToItem(gridGroup, QPointF()).y(), 35.0 + 8.0);
        auto *videoBody = window->findChild<QQuickItem *>(QStringLiteral("videoBody"));
        QCOMPARE(videoBody->mapToItem(groupOf(video), QPointF()).y(), 35.0);

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
        QCOMPARE(onSelected.right() - button.right(), 7.0);
        group->setProperty("currentTabIndex", other == tab0 ? 0 : 1);
        QTRY_VERIFY(other->property("selected").toBool());
        const QRectF onOther = other->mapRectToItem(group, QRectF(0, 0, other->width(), other->height()));
        QTRY_VERIFY(onOther.contains(groupMenu->mapRectToItem(group, QRectF(0, 0, 20, 20))));
    }

    void headerFollowsThePanelsDescription()
    {
        restoreInitial();
        auto *gridToolbar = window->findChild<QQuickItem *>(QStringLiteral("gridToolbar"));
        QVERIFY(gridToolbar);
        QQuickItem videoToolbar;
        videoToolbar.setImplicitWidth(40);
        QQuickItem otherGridToolbar;
        otherGridToolbar.setImplicitWidth(40);
        const auto restore = qScopeGuard([this, gridToolbar] {
            docking()->setPanelHeader(QStringLiteral("Video"), false, nullptr);
            docking()->setPanelHeader(QStringLiteral("Grid"), true, gridToolbar);
        });

        QQuickItem *video = header(QStringLiteral("Video"));
        QVERIFY(video);
        QQuickItem *videoSlot = childNamed(video, QStringLiteral("dockToolbarSlot"));
        QVERIFY(videoSlot);
        QVERIFY(video->property("titleMode").toBool());
        QVERIFY(!videoSlot->isVisible());
        docking()->setPanelHeader(QStringLiteral("Video"), true, &videoToolbar);
        QTRY_VERIFY(!video->property("titleMode").toBool());
        QCOMPARE(video->property("currentName").toString(), QStringLiteral("Video"));
        QVERIFY(childNamed(video, QStringLiteral("dockTab0"))->isVisible());
        QTRY_COMPARE(videoSlot->property("toolbar").value<QQuickItem *>(), &videoToolbar);
        QCOMPARE(videoToolbar.parentItem(), videoSlot);
        QVERIFY(videoSlot->isVisible());
        docking()->setPanelHeader(QStringLiteral("Video"), false, nullptr);
        QTRY_VERIFY(video->property("titleMode").toBool());
        QVERIFY(!videoSlot->isVisible());
        QVERIFY(!videoToolbar.parentItem());

        QQuickItem *grid = header(QStringLiteral("Grid"));
        QVERIFY(grid);
        QQuickItem *gridSlot = childNamed(grid, QStringLiteral("dockToolbarSlot"));
        QTRY_COMPARE(gridToolbar->parentItem(), gridSlot);
        docking()->setPanelHeader(QStringLiteral("Grid"), true, &otherGridToolbar);
        QTRY_COMPARE(gridSlot->property("toolbar").value<QQuickItem *>(), &otherGridToolbar);
        QCOMPARE(otherGridToolbar.parentItem(), gridSlot);
        QVERIFY(!gridToolbar->parentItem());
        QVERIFY(!grid->property("titleMode").toBool());
        docking()->setPanelHeader(QStringLiteral("Grid"), true, gridToolbar);
        QTRY_COMPARE(gridToolbar->parentItem(), gridSlot);
        QVERIFY(!otherGridToolbar.parentItem());
    }

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
        QVERIFY(menu->state().expandable);
        QVERIFY(!menu->state().expanded);
        expandedChanges.clear();
        QAccessible::installUpdateHandler(captureExpandedChanges);
        docking()->setOpenMenu(QStringLiteral("Video"));
        const QAccessible::State opened = menu->state();
        docking()->setOpenMenu({});
        QAccessible::installUpdateHandler(nullptr);
        QVERIFY(opened.expanded);
        QVERIFY(!opened.checked);
        QVERIFY(!menu->state().expanded);
        QCOMPARE(expandedChanges, (QList<QObject *>{menu->object(), menu->object()}));
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

    void theMenuIsReachedByPointerAndKeyboard()
    {
        restoreInitial();
        QSignalSpy asked(docking(), &hikari::ui::Docking::menuRequested);
        QQuickItem *video = header(QStringLiteral("Video"));
        QQuickItem *videoMenu = childNamed(video, QStringLiteral("dockMenuButton"));
        QTest::mouseClick(window, Qt::LeftButton, {}, centreOf(videoMenu));
        QTRY_COMPARE(asked.size(), 1);
        QCOMPARE(asked.at(0).at(0).toString(), QStringLiteral("Video"));
        QCOMPARE(asked.at(0).at(1).value<QQuickItem *>(), videoMenu);
        QTest::mouseClick(window, Qt::RightButton, {}, video->mapToScene(QPointF(60, video->height() / 2)).toPoint());
        QTRY_COMPARE(asked.size(), 2);
        QCOMPARE(asked.at(1).at(0).toString(), QStringLiteral("Video"));

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
        QTest::qWait(600);
        // Windows delivers Shift+F10 as a keyboard context menu request (WM_CONTEXTMENU), not a key press.
        QContextMenuEvent request(QContextMenuEvent::Keyboard, QPoint(), QPoint());
        QGuiApplication::sendEvent(window, &request);
        QTRY_COMPARE(asked.size(), 7);
        QCOMPARE(asked.at(6).at(0).toString(), QStringLiteral("Editor"));
        window->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), static_cast<QWindow *>(window));
        docking()->requestKeyboardContextMenu();
        QTRY_COMPARE(asked.size(), 8);
        QCOMPARE(asked.at(7).at(0).toString(), QStringLiteral("Editor"));
        const int before = group->property("currentTabIndex").toInt();
        QTest::keyClick(window, before == 0 ? Qt::Key_Right : Qt::Key_Left);
        if (before == 0) {
            QTRY_VERIFY(groupMenu->hasActiveFocus());
            QTest::keyClick(window, Qt::Key_Right);
        }
        QTRY_VERIFY(group->property("currentTabIndex").toInt() != before);
    }

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
        QQuickItem *grid = header(QStringLiteral("Grid"));
        QTest::mouseDClick(window, Qt::LeftButton, {}, centreOf(childNamed(grid, QStringLiteral("dockTab0"))));
        QTRY_VERIFY(dock("Grid")->property("isFloating").toBool());
        QVERIFY(docking()->toggleFloating(QStringLiteral("Grid")));
        QTRY_VERIFY(!dock("Grid")->property("isFloating").toBool());
    }

    void floatingPanelsAreBorderlessToolWindows()
    {
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
        QCOMPARE(qobject_cast<QQuickWindow *>(floating)->color().alpha(), 0);
        QVERIFY(bar->property("titleMode").toBool());
        QCOMPARE(bar->mapToScene(QPointF()).toPoint(), QPoint(shadow + 1, shadow + 1));
        QVERIFY(bar->property("floating").toBool());
        QVERIFY(audio->setProperty("isFloating", false));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
    }

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

    void aWindowSystemTitleBarTakesTheWindowsName()
    {
        using hikari::ui::windowdecorations::overrideSystemFrame;
        using hikari::ui::windowdecorations::SystemFrame;
        restoreInitial();
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        QWindow *floating = bar->window();
        QCOMPARE(floating->title(), QStringLiteral("Audio"));
        QQuickItem *title = childNamed(bar, QStringLiteral("dockTitleText"));
        QQuickItem *button = childNamed(bar, QStringLiteral("dockMenuButton"));
        QVERIFY(title && button);
        QVERIFY(title->isVisible() && button->isVisible());
        QVERIFY(!docking()->windowSystemTitle(floating));
        const auto unset = qScopeGuard([floating = QPointer<QWindow>(floating)] {
            if (floating)
                overrideSystemFrame(floating, std::nullopt);
        });
        QSignalSpy changes(docking(), &hikari::ui::Docking::windowSystemTitlesChanged);
        overrideSystemFrame(floating, SystemFrame::TitleBar);
        QCOMPARE(changes.count(), 1);
        QVERIFY(docking()->windowSystemTitle(floating));
        QTRY_VERIFY(!title->isVisible());
        QVERIFY(button->isVisible());
        QVERIFY(bar->property("titleMode").toBool());
        QAccessibleInterface *named = QAccessible::queryAccessibleInterface(bar);
        QVERIFY(named);
        QCOMPARE(named->text(QAccessible::Name), QStringLiteral("Audio"));
        QVERIFY(!docking()->windowSystemTitle(window));
        QVERIFY(childNamed(header(QStringLiteral("Video")), QStringLiteral("dockTitleText"))->isVisible());
        overrideSystemFrame(floating, SystemFrame::None);
        QTRY_VERIFY(title->isVisible());
        QVERIFY(audio->setProperty("isFloating", false));
        QTRY_VERIFY(!audio->property("isFloating").toBool());

        auto *video = dock("Video");
        QVERIFY(video->setProperty("isFloating", true));
        QQuickItem *videoBar = nullptr;
        QTRY_VERIFY((videoBar = header(QStringLiteral("Video"))) && videoBar->window() != window);
        auto *pair = qobject_cast<QQuickWindow *>(videoBar->window());
        QVERIFY(QMetaObject::invokeMethod(window, "pairFloating"));
        QTRY_VERIFY(header(QStringLiteral("Audio")) && header(QStringLiteral("Audio"))->window() == pair);
        QTRY_VERIFY(!itemsNamed(pair, QStringLiteral("dockTitleBar")).isEmpty());
        QQuickItem *titleBar = itemsNamed(pair, QStringLiteral("dockTitleBar")).first();
        QQuickItem *barTitle = childNamed(titleBar, QStringLiteral("dockTitleText"));
        QVERIFY(barTitle && barTitle->isVisible());
        overrideSystemFrame(pair, SystemFrame::TitleBar);
        QTRY_VERIFY(!barTitle->isVisible());
        QVERIFY(childNamed(titleBar, QStringLiteral("dockMenuButton"))->isVisible());
        for (const QString &panel : {QStringLiteral("Video"), QStringLiteral("Audio")})
            QVERIFY2(childNamed(header(panel), QStringLiteral("dockTitleText"))->isVisible(), qPrintable(panel));
        overrideSystemFrame(pair, std::nullopt);
        QTRY_VERIFY(barTitle->isVisible());
        restoreInitial();
    }

    void serverSideFramesOfABorderOnlyCompositorHaveNoTitleBar()
    {
        using hikari::ui::windowdecorations::serverSideFrame;
        using hikari::ui::windowdecorations::SystemFrame;
        const QStringList common{QStringLiteral("wl_compositor"), QStringLiteral("xdg_wm_base"),
                                 QStringLiteral("zxdg_decoration_manager_v1")};
        QCOMPARE(serverSideFrame(common + QStringList{QStringLiteral("hyprland_focus_grab_manager_v1"),
                                                      QStringLiteral("hyprland_surface_manager_v1")}),
                 SystemFrame::Border);
        QCOMPARE(serverSideFrame(common + QStringList{QStringLiteral("zwlr_layer_shell_v1")}), SystemFrame::TitleBar);
        QCOMPARE(serverSideFrame(common + QStringList{QStringLiteral("org_kde_kwin_server_decoration_manager")}),
                 SystemFrame::TitleBar);
        QCOMPARE(serverSideFrame({}), SystemFrame::TitleBar);
    }

    void aWindowSystemBorderKeepsTheNameAndDropsTheShadow()
    {
        using hikari::ui::windowdecorations::overrideSystemFrame;
        using hikari::ui::windowdecorations::SystemFrame;
        hikari::ui::dockchrome::overrideCompositing(true);
        const auto unsetCompositing = qScopeGuard([] { hikari::ui::dockchrome::overrideCompositing(std::nullopt); });
        restoreInitial();
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        auto *floating = qobject_cast<QQuickWindow *>(bar->window());
        QVERIFY(floating);
        const auto unset = qScopeGuard([floating = QPointer<QWindow>(floating)] {
            if (floating)
                overrideSystemFrame(floating, std::nullopt);
        });
        QQuickItem *title = childNamed(bar, QStringLiteral("dockTitleText"));
        QQuickItem *button = childNamed(bar, QStringLiteral("dockMenuButton"));
        QQuickItem *frame = itemsNamed(floating, QStringLiteral("dockFloatingFrame")).value(0);
        QQuickItem *chrome = itemsNamed(floating, QStringLiteral("dockFloatingWindow")).value(0);
        QVERIFY(title && button && frame && chrome);
        const int shadow = hikari::ui::dockchrome::floatingShadow();
        QCOMPARE(shadow, 8);
        QCOMPARE(docking()->windowShadow(floating), shadow);
        QTRY_COMPARE(bar->mapToScene(QPointF()).toPoint(), QPoint(shadow + 1, shadow + 1));

        QSignalSpy changes(docking(), &hikari::ui::Docking::windowSystemTitlesChanged);
        overrideSystemFrame(floating, SystemFrame::Border);
        QCOMPARE(changes.count(), 1);
        QVERIFY(docking()->windowSystemFrame(floating));
        QVERIFY(!docking()->windowSystemTitle(floating));
        QCOMPARE(docking()->windowShadow(floating), 0);
        QVERIFY(bar->property("titleMode").toBool());
        QVERIFY(!bar->property("systemTitle").toBool());
        QVERIFY(title->isVisible());
        QVERIFY(button->isVisible());
        QCOMPARE(title->property("text").toString(), QStringLiteral("Audio"));
        QTRY_COMPARE(chrome->property("shadow").toInt(), 0);
        QTRY_COMPARE(frame->size(), QSizeF(floating->size()));
        QCOMPARE(frame->mapToScene(QPointF()), QPointF());
        QCOMPARE(frame->property("radius").toReal(), 0.0);
        QTRY_COMPARE(bar->mapToScene(QPointF()).toPoint(), QPoint(1, 1));
        QQuickItem *right = itemsNamed(floating, QStringLiteral("resizeRight")).value(0);
        QVERIFY(right && right->isVisible());
        QCOMPARE(right->width(), qreal(hikari::ui::dockchrome::kResizeGrip));
        QCOMPARE(right->mapToScene(QPointF(right->width(), 0)).x(), qreal(floating->width()));
        QVERIFY(!docking()->windowSystemFrame(window));
        QCOMPARE(docking()->windowShadow(window), shadow);

        overrideSystemFrame(floating, SystemFrame::TitleBar);
        QTRY_VERIFY(!title->isVisible());
        QCOMPARE(chrome->property("shadow").toInt(), 0);

        overrideSystemFrame(floating, SystemFrame::None);
        QTRY_VERIFY(title->isVisible());
        QTRY_COMPARE(chrome->property("shadow").toInt(), shadow);
        QTRY_COMPARE(bar->mapToScene(QPointF()).toPoint(), QPoint(shadow + 1, shadow + 1));
        QTRY_COMPARE(frame->mapToScene(QPointF()), QPointF(shadow, shadow));
        QVERIFY(audio->setProperty("isFloating", false));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
    }

    void separatorsAreOnePixel()
    {
        restoreInitial();
        const QList<QQuickItem *> separators = itemsNamed(window, QStringLiteral("dockSeparator"));
        QVERIFY(!separators.isEmpty());
        for (QQuickItem *separator : separators)
            QVERIFY2(qMin(separator->width(), separator->height()) == 1.0,
                     qPrintable(QStringLiteral("%1x%2").arg(separator->width()).arg(separator->height())));
        QQuickItem *handle = childNamed(separators.first(), QStringLiteral("dockSeparatorHandle"));
        QVERIFY(handle);
        QCOMPARE(qMin(handle->width(), handle->height()), 11.0);
    }

    void aSeparatorJunctionShowsTheMoveCursor()
    {
        restoreInitial();
        const QRectF video = sceneRect(groupOf(header(QStringLiteral("Video"))));
        QQuickItem *upright = separatorThrough(Qt::Vertical, QPointF(video.right(), video.center().y()));
        QQuickItem *across = separatorThrough(Qt::Horizontal, QPointF(video.center().x(), video.bottom()));
        QVERIFY(upright && across);
        const QPoint junction(qRound(sceneRect(upright).center().x()), qRound(sceneRect(across).center().y()));
        QTest::mouseMove(window, junction);
        QTRY_COMPARE(window->cursor().shape(), Qt::SizeAllCursor);
        QTest::mouseMove(window, sceneRect(upright).center().toPoint());
        QTRY_COMPARE(window->cursor().shape(), Qt::SizeHorCursor);
        QTest::mouseMove(window, QPoint(qRound(video.center().x()), junction.y()));
        QTRY_COMPARE(window->cursor().shape(), Qt::SizeVerCursor);
        QTest::mouseMove(window, centreOf(header(QStringLiteral("Grid"))));
    }

    void aTJunctionMovesBothSeparators()
    {
        restoreInitial();
        QQuickItem *videoGroup = groupOf(header(QStringLiteral("Video")));
        QQuickItem *audioGroup = groupOf(header(QStringLiteral("Audio")));
        QQuickItem *editorGroup = groupOf(header(QStringLiteral("Editor")));
        const QRectF video = sceneRect(videoGroup);
        const QRectF editor = sceneRect(editorGroup);
        QQuickItem *upright = separatorThrough(Qt::Vertical, QPointF(video.right(), video.center().y()));
        QQuickItem *across = separatorThrough(Qt::Horizontal, QPointF(video.center().x(), video.bottom()));
        QVERIFY(upright && across);
        const QPoint junction(qRound(sceneRect(upright).center().x()), qRound(sceneRect(across).center().y()));
        dragFrom(junction, QPoint(40, 30));
        QTRY_VERIFY2(qAbs(videoGroup->width() - (video.width() + 40)) <= 2 && qAbs(videoGroup->height() - (video.height() + 30)) <= 2,
                     qPrintable(QStringLiteral("Video %1x%2 from %3x%4").arg(videoGroup->width()).arg(videoGroup->height())
                                    .arg(video.width()).arg(video.height())));
        QVERIFY2(qAbs(editorGroup->height() - (editor.height() - 30)) <= 2,
                 qPrintable(QStringLiteral("Editor %1 from %2").arg(editorGroup->height()).arg(editor.height())));

        const qreal least = 300;
        QVERIFY(docking()->setMinimumSize(QStringLiteral("Audio"), QSize(int(least), 0)));
        const auto unset = qScopeGuard([this] {
            docking()->setMinimumSize(QStringLiteral("Audio"), QSize());
            hikari::ui::refreshDockConstraints();
        });
        QTRY_VERIFY(audioGroup->width() >= least);
        const qreal videoHeight = videoGroup->height();
        const QPoint moved(qRound(sceneRect(upright).center().x()), qRound(sceneRect(across).center().y()));
        dragFrom(moved, QPoint(window->width() - moved.x(), -20));
        QTRY_VERIFY2(qAbs(videoGroup->height() - (videoHeight - 20)) <= 2,
                     qPrintable(QStringLiteral("Video height %1 from %2").arg(videoGroup->height()).arg(videoHeight)));
        QVERIFY2(audioGroup->width() >= least, qPrintable(QString::number(audioGroup->width())));
        QVERIFY(videoGroup->width() > video.width() + 40);
    }

    void aPlusJunctionMovesAllThreeSeparators()
    {
        restoreInitial();
        QVERIFY(QMetaObject::invokeMethod(area(), "addDockWidget", Q_ARG(QQuickItem *, qobject_cast<QQuickItem *>(dock("Grid"))),
                                          Q_ARG(KDDockWidgets::Location, KDDockWidgets::Location_OnRight),
                                          Q_ARG(QQuickItem *, qobject_cast<QQuickItem *>(dock("Editor"))), Q_ARG(QSize, QSize()),
                                          Q_ARG(KDDockWidgets::InitialVisibilityOption, {})));
        QQuickItem *videoGroup = groupOf(header(QStringLiteral("Video")));
        QQuickItem *editorGroup = nullptr;
        QTRY_VERIFY((editorGroup = groupOf(header(QStringLiteral("Editor"))))
                    && qAbs(sceneRect(editorGroup).bottom() - sceneRect(videoGroup).bottom()) > 50);
        QQuickItem *gridGroup = groupOf(header(QStringLiteral("Grid")));
        QTRY_VERIFY(qAbs(sceneRect(gridGroup).top() - sceneRect(editorGroup).top()) <= 1);
        QVERIFY(docking()->resizeInLayout(QStringLiteral("Editor"), 0, 0,
                                          qRound(sceneRect(videoGroup).right() - sceneRect(editorGroup).right()), 0));
        QTRY_VERIFY2(qAbs(sceneRect(videoGroup).right() - sceneRect(editorGroup).right()) <= 1,
                     qPrintable(QStringLiteral("Video right %1, Editor right %2").arg(sceneRect(videoGroup).right())
                                    .arg(sceneRect(editorGroup).right())));
        const QRectF video = sceneRect(videoGroup);
        const QRectF editor = sceneRect(editorGroup);
        QQuickItem *upper = separatorThrough(Qt::Vertical, QPointF(video.right(), video.center().y()));
        QQuickItem *lower = separatorThrough(Qt::Vertical, QPointF(editor.right(), editor.center().y()));
        QQuickItem *across = separatorThrough(Qt::Horizontal, QPointF(video.center().x(), video.bottom()));
        QVERIFY(upper && lower && across && upper != lower);
        const QPoint junction(qRound(sceneRect(upper).center().x()), qRound(sceneRect(across).center().y()));
        QTest::mouseMove(window, junction + QPoint(0, 3));
        QTRY_COMPARE(window->cursor().shape(), Qt::SizeAllCursor);
        dragFrom(junction, QPoint(-50, 25));
        QTRY_VERIFY2(qAbs(videoGroup->width() - (video.width() - 50)) <= 2 && qAbs(editorGroup->width() - (editor.width() - 50)) <= 2
                         && qAbs(videoGroup->height() - (video.height() + 25)) <= 2,
                     qPrintable(QStringLiteral("Video %1x%2 from %3x%4, Editor width %5 from %6").arg(videoGroup->width())
                                    .arg(videoGroup->height()).arg(video.width()).arg(video.height()).arg(editorGroup->width())
                                    .arg(editor.width())));
        restoreInitial();
    }

    void floatingPanelsResizeFromEveryEdgeAndCorner()
    {
        hikari::ui::dockchrome::overrideSystemResize(false);
        const auto unset = qScopeGuard([] { hikari::ui::dockchrome::overrideSystemResize(std::nullopt); });
        restoreInitial();
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        auto *floating = qobject_cast<QQuickWindow *>(bar->window());
        QVERIFY(floating);
        QTRY_VERIFY(floating->isExposed());
        const QSize roomy = floating->minimumSize().expandedTo(floating->size()) + QSize(100, 100);
        floating->setGeometry(QRect(QPoint(200, 150), roomy));
        QTRY_COMPARE(floating->size(), roomy);
        const struct {
            const char *name;
            QPoint by;
            Qt::CursorShape cursor;
            QMargins grows;
        } grips[] = {
            {"resizeLeft", {-30, 0}, Qt::SizeHorCursor, {30, 0, 0, 0}},
            {"resizeRight", {30, 0}, Qt::SizeHorCursor, {0, 0, 30, 0}},
            {"resizeTop", {0, -20}, Qt::SizeVerCursor, {0, 20, 0, 0}},
            {"resizeBottom", {0, 20}, Qt::SizeVerCursor, {0, 0, 0, 20}},
            {"resizeTopLeft", {-30, -20}, Qt::SizeFDiagCursor, {30, 20, 0, 0}},
            {"resizeTopRight", {30, -20}, Qt::SizeBDiagCursor, {0, 20, 30, 0}},
            {"resizeBottomLeft", {-30, 20}, Qt::SizeBDiagCursor, {30, 0, 0, 20}},
            {"resizeBottomRight", {30, 20}, Qt::SizeFDiagCursor, {0, 0, 30, 20}},
        };
        for (const auto &grip : grips) {
            QQuickItem *edge = itemsNamed(floating, QLatin1String(grip.name)).value(0);
            QVERIFY2(edge && edge->isVisible(), grip.name);
            QCOMPARE(edge->cursor().shape(), grip.cursor);
            const QRect before = floating->geometry();
            const QPoint at = centreOf(edge);
            QTest::mousePress(floating, Qt::LeftButton, {}, at);
            QTest::mouseMove(floating, at + grip.by);
            QTRY_VERIFY(floating->geometry() != before);
            QTest::mouseRelease(floating, Qt::LeftButton, {}, at + grip.by - (floating->position() - before.topLeft()));
            const QRect want = before.marginsAdded(grip.grows);
            QTRY_VERIFY2(floating->geometry() == want,
                         qPrintable(QStringLiteral("%1: %2,%3 %4x%5, want %6,%7 %8x%9").arg(QLatin1String(grip.name))
                                        .arg(floating->x()).arg(floating->y()).arg(floating->width()).arg(floating->height())
                                        .arg(want.x()).arg(want.y()).arg(want.width()).arg(want.height())));
        }
        const QRect before = floating->geometry();
        QQuickItem *corner = itemsNamed(floating, QStringLiteral("resizeBottomRight")).value(0);
        dragWindow(floating, centreOf(corner), QPoint(-5000, -5000));
        QTRY_VERIFY(floating->width() < before.width());
        QVERIFY2(floating->width() >= floating->minimumWidth() && floating->height() >= floating->minimumHeight(),
                 qPrintable(QStringLiteral("%1x%2, minimum %3x%4").arg(floating->width()).arg(floating->height())
                                .arg(floating->minimumWidth()).arg(floating->minimumHeight())));
        QVERIFY(audio->setProperty("isFloating", false));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
    }

    void aResizeKeepsTheMinimumSize()
    {
        const QSize least(200, 120);
        const QRect from(100, 100, 400, 300);
        using hikari::ui::dockchrome::resizedFrom;
        QCOMPARE(resizedFrom(from, Qt::RightEdge | Qt::BottomEdge, {-1000, -1000}, least), QRect(100, 100, 200, 120));
        QCOMPARE(resizedFrom(from, Qt::LeftEdge | Qt::TopEdge, {1000, 1000}, least), QRect(300, 280, 200, 120));
        QCOMPARE(resizedFrom(from, Qt::LeftEdge, {-50, 999}, least), QRect(50, 100, 450, 300));
        QCOMPARE(resizedFrom(from, Qt::TopEdge, {0, 40}, QSize()), QRect(100, 140, 400, 260));
    }

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
        QVERIFY(KDDockWidgets::LayoutSaver().restoreLayout(initial));
        hikari::ui::refreshDockConstraints();
        QTRY_VERIFY(groupOf(header(QStringLiteral("Editor"))) && groupOf(header(QStringLiteral("Editor")))->height() >= 260 + 35);
        QVERIFY(docking()->setMinimumSize(QStringLiteral("Editor"), QSize()));
        hikari::ui::refreshDockConstraints();
    }

    void floatingPanelKeepsItsSizeAcrossAScaleChange()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("the screen's scale changes through the offscreen platform's configuration");
        QCOMPARE(window->devicePixelRatio(), 1.0);
        restoreInitial();
        qreal native = 1.0;
        hikari::ui::dockchrome::overrideNativeScale([&native](QWindow *) { return std::optional<qreal>(native); });
        QVERIFY(docking()->setMinimumSize(QStringLiteral("Editor"), QSize(360, 140)));
        auto *editor = dock("Editor");
        const auto restore = qScopeGuard([&] {
            setScreenDpi(96);
            hikari::ui::dockchrome::overrideNativeScale(nullptr);
            docking()->setMinimumSize(QStringLiteral("Editor"), QSize());
            editor->setProperty("isFloating", false);
        });
        QVERIFY(editor->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Editor"))) && bar->window() != window);
        auto *floating = qobject_cast<QQuickWindow *>(bar->window());
        QVERIFY(QTest::qWaitForWindowExposed(floating));
        KDDockWidgets::QtQuick::FloatingWindow *view = nullptr;
        for (QQuickItem *child : floating->contentItem()->childItems())
            if (auto *v = qobject_cast<KDDockWidgets::QtQuick::FloatingWindow *>(child))
                view = v;
        QVERIFY(view);
        const QSize minimum = view->minSize();
        floating->resize(minimum);
        QTRY_COMPARE(floating->size(), minimum);
        QTest::qWait(50);

        WidthAtScaleChange atChange(floating);
        native = 1.25;
        QVERIFY(setScreenDpi(120));
        QTRY_COMPARE(floating->devicePixelRatio(), 1.25);
        QTRY_COMPARE(floating->size(), minimum);
        QVERIFY2(qAbs(atChange.width - qRound(minimum.width() / 1.25)) <= 1,
                 qPrintable(QStringLiteral("%1 at 125 %").arg(atChange.width)));
        QTRY_COMPARE(static_cast<QQuickItem *>(view)->size(), QSizeF(floating->size()));
        QCOMPARE(bar->window(), floating);

        const QSize large(qRound(minimum.width() * 1.6), qRound(minimum.height() * 1.6));
        floating->resize(large);
        QTRY_COMPARE(floating->size(), large);
        QTest::qWait(50);
        // Windows resizes a window for the new scale (WM_DPICHANGED) before Qt takes it.
        native = 1.0;
        const QSize early(qRound(large.width() / 1.25), qRound(large.height() / 1.25));
        floating->resize(early);
        QTRY_COMPARE(floating->size(), early);
        QTest::qWait(50);
        QVERIFY(setScreenDpi(96));
        QTRY_COMPARE(floating->devicePixelRatio(), 1.0);
        QTest::qWait(hikari::ui::dockchrome::kScaleSettleMs * 2);
        QCOMPARE(floating->size(), large);
    }

    void floatingPanelComesBackWholeWhenItsScreenGoesAway()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("screens come and go through the offscreen platform's configuration");
        QCOMPARE(window->devicePixelRatio(), 1.0);
        restoreInitial();
        hikari::ui::dockchrome::overrideNativeScale([](QWindow *) { return std::optional<qreal>(); });
        const QString side = QStringLiteral("Side");
        auto *audio = dock("Audio");
        const auto restore = qScopeGuard([&] {
            setSideScreen(side, 0);
            hikari::ui::dockchrome::overrideNativeScale(nullptr);
            audio->setProperty("isFloating", false);
        });
        QScreen *main = window->screen();
        const QRect mainArea = main->availableGeometry();
        QVERIFY(setSideScreen(side, 144));
        QScreen *sideScreen = nullptr;
        QTRY_VERIFY((sideScreen = screenNamed(side)));
        QCOMPARE(sideScreen->devicePixelRatio(), 1.5);

        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        auto *floating = qobject_cast<QQuickWindow *>(bar->window());
        QVERIFY(QTest::qWaitForWindowExposed(floating));
        QCOMPARE(floating->transientParent(), window);
        KDDockWidgets::QtQuick::FloatingWindow *view = nullptr;
        for (QQuickItem *child : floating->contentItem()->childItems())
            if (auto *v = qobject_cast<KDDockWidgets::QtQuick::FloatingWindow *>(child))
                view = v;
        QVERIFY(view);
        const QRect sideArea = sideScreen->availableGeometry();
        floating->setFramePosition(sideArea.topLeft() + QPoint(200, 150));
        QTRY_COMPARE(floating->screen(), sideScreen);
        QTRY_COMPARE(floating->devicePixelRatio(), 1.5);
        QTest::qWait(hikari::ui::dockchrome::kScaleSettleMs * 2);
        const QSize size = view->minSize().expandedTo(QSize(mainArea.width() * 3 / 4, 220));
        QVERIFY(size.width() < mainArea.width() && size.height() < mainArea.height());
        floating->resize(size);
        QTRY_COMPARE(floating->size(), size);
        QTest::qWait(50);

        auto wholeOnMain = [&] {
            return floating->screen() == main && mainArea.contains(floating->frameGeometry());
        };
        auto report = [&] {
            return QStringLiteral("%1,%2 %3x%4 on %5, panel %6x%7")
                .arg(floating->frameGeometry().x()).arg(floating->frameGeometry().y())
                .arg(floating->width()).arg(floating->height())
                .arg(floating->screen() ? floating->screen()->name() : QString())
                .arg(static_cast<QQuickItem *>(view)->width()).arg(static_cast<QQuickItem *>(view)->height());
        };
        // Windows ("PC screen only") moves the window onto the remaining screen at its pixel size, 173 px showing.
        auto windowSystemMoves = [&](QSize pixels) {
            floating->setGeometry(QRect(QPoint(mainArea.right() + 1 - 173, mainArea.top() + 107), pixels));
        };
        auto settled = [&] {
            // MUST KILL: qWait(250) copies WorkspaceLayoutController's screen-check delay by hand
            QTest::qWait(250);
            hikari::ui::keepFloatingPanelsOnScreen();
            QTest::qWait(hikari::ui::dockchrome::kScaleSettleMs * 2);
        };

        QVERIFY(setSideScreen(side, 0));
        QTRY_COMPARE(floating->screen(), main);
        QTRY_COMPARE(floating->devicePixelRatio(), 1.0);
        windowSystemMoves(QSize(qRound(size.width() / 1.5), qRound(size.height() / 1.5)));
        settled();
        QTRY_VERIFY2(wholeOnMain(), qPrintable(report()));
        QCOMPARE(floating->size(), size);
        QTRY_COMPARE(static_cast<QQuickItem *>(view)->size(), QSizeF(floating->size()));
        QCOMPARE(bar->window(), floating);

        windowSystemMoves(size);
        settled();
        QTRY_VERIFY2(wholeOnMain(), qPrintable(report()));
        QCOMPARE(floating->size(), size);

        QTest::qWait(hikari::ui::dockchrome::kScreenLossWatchMs);
        const QPoint partlyOff(mainArea.right() + 1 - 173, mainArea.top() + 50);
        floating->setFramePosition(partlyOff);
        QTRY_COMPARE(floating->framePosition(), partlyOff);
        QTest::qWait(hikari::ui::dockchrome::kScaleSettleMs * 2);
        QCOMPARE(floating->framePosition(), partlyOff);

        QVERIFY(setSideScreen(side, 144));
        QTRY_VERIFY((sideScreen = screenNamed(side)));
        floating->setFramePosition(sideScreen->availableGeometry().topLeft() + QPoint(200, 150));
        QTRY_COMPARE(floating->devicePixelRatio(), 1.5);
        QTest::qWait(hikari::ui::dockchrome::kScaleSettleMs * 2);
        QCOMPARE(floating->size(), size);
        QVERIFY(setSideScreen(side, 0));
        QTRY_COMPARE(floating->screen(), main);
        settled();
        QTRY_VERIFY2(wholeOnMain(), qPrintable(report()));
        QCOMPARE(floating->size(), size);
        QTRY_COMPARE(static_cast<QQuickItem *>(view)->size(), QSizeF(floating->size()));
    }

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
        moveTo(left, tab);
        QTRY_VERIFY(highlight->isVisible());
        QTRY_VERIFY(qAbs(highlight->mapRectToScene(QRectF(0, 0, highlight->width(), highlight->height())).width() - g.width()) <= 2);
        {
            const QRectF all = highlight->mapRectToScene(QRectF(0, 0, highlight->width(), highlight->height()));
            QVERIFY2(qAbs(all.left() - g.left()) <= 1 && qAbs(all.top() - g.top()) <= 1 && qAbs(all.height() - g.height()) <= 2,
                     qPrintable(QStringLiteral("%1,%2 %3x%4").arg(all.x()).arg(all.y()).arg(all.width()).arg(all.height())));
        }
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

    void aDropBesideOrIntoTheGroupItEmptiesIsRefused()
    {
        using namespace KDDockWidgets;
        const auto allowed = Config::self().dropIndicatorAllowedFunc();
        QVERIFY(allowed);
        Core::DockWidget *video = DockRegistry::self()->dockByName(QStringLiteral("Video"));
        Core::DockWidget *audio = DockRegistry::self()->dockByName(QStringLiteral("Audio"));
        QVERIFY(video && audio);
        const Vector<Core::DockWidget *> alone{video}, pair{video, audio}, other{audio};
        for (DropLocation inner : {DropLocation_Left, DropLocation_Top, DropLocation_Right, DropLocation_Bottom,
                                   DropLocation_Center}) {
            QVERIFY(!allowed(inner, alone, alone, nullptr));
            QVERIFY(!allowed(inner, pair, pair, nullptr));
            QVERIFY(allowed(inner, alone, pair, nullptr));
            QVERIFY(allowed(inner, other, alone, nullptr));
        }
        for (DropLocation outer : {DropLocation_OutterLeft, DropLocation_OutterTop, DropLocation_OutterRight,
                                   DropLocation_OutterBottom})
            QVERIFY(allowed(outer, alone, alone, nullptr));
    }

    void everyDropLandsWhereItsHighlightShows_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("side");
        QTest::addColumn<bool>("floating");
        for (bool floating : {false, true})
            for (const char *target : {"Video", "Editor", "Grid"})
                for (const char *side : {"left", "right", "top", "bottom", "tab"})
                    QTest::addRow("%s %s %s", floating ? "floating" : "docked", target, side)
                        << QString::fromLatin1(target) << QString::fromLatin1(side) << floating;
    }

    void everyDropLandsWhereItsHighlightShows()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("a synthetic drag between windows needs the offscreen platform (the native gate drags for real)");
        QFETCH(QString, target);
        QFETCH(QString, side);
        QFETCH(bool, floating);
        restoreInitial();
        if (floating) {
            QVERIFY(dock("Audio")->setProperty("isFloating", true));
            QTRY_VERIFY(header(QStringLiteral("Audio")) && header(QStringLiteral("Audio"))->window() != window);
            QTRY_VERIFY(header(QStringLiteral("Audio"))->window()->isExposed());
        }
        QQuickItem *bar = header(QStringLiteral("Audio"));
        QWindow *receiver = bar->window();
        const QPoint start = receiver->mapToGlobal(bar->mapToScene(QPointF(30, bar->height() / 2)).toPoint());
        sendMouse(receiver, QEvent::MouseButtonPress, start, Qt::LeftButton);
        auto release = qScopeGuard([&] { sendMouse(receiver, QEvent::MouseButtonRelease, QCursor::pos(), Qt::NoButton); });
        for (int i = 1; i <= 4; ++i)
            sendMouse(receiver, QEvent::MouseMove, start + QPoint(0, 8 * i), Qt::LeftButton);
        QTRY_VERIFY(dock("Audio")->property("isFloating").toBool());
        receiver = header(QStringLiteral("Audio"))->window();

        const auto sceneRect = [](QQuickItem *item) {
            return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        };
        const QRectF g = sceneRect(groupOf(header(target)));
        const qreal headerHeight = hikari::ui::dockchrome::kHeaderHeight;
        QPointF at = g.center();
        if (side == QLatin1String("left"))
            at.setX(g.left() + g.width() / 6);
        else if (side == QLatin1String("right"))
            at.setX(g.right() - g.width() / 6);
        else if (side == QLatin1String("top"))
            at.setY(g.top() + std::max(headerHeight + 4, g.height() / 6));
        else if (side == QLatin1String("bottom"))
            at.setY(g.bottom() - g.height() / 6);
        const QPoint to = window->mapToGlobal(at.toPoint());
        const QPoint from = QCursor::pos();
        for (int i = 1; i <= 12; ++i)
            sendMouse(receiver, QEvent::MouseMove, from + (to - from) * i / 12, Qt::LeftButton);
        const auto report = [&](const QRectF &r) {
            return QStringLiteral("%1,%2 %3x%4 over %5 %6,%7 %8x%9 at %10,%11")
                .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).arg(target)
                .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()).arg(at.x()).arg(at.y());
        };
        QTRY_VERIFY2(!itemsNamed(window, QStringLiteral("dockDropHighlight")).isEmpty(), qPrintable(report({})));
        const QRectF h = sceneRect(itemsNamed(window, QStringLiteral("dockDropHighlight")).first());
        // KDDW's highlight for a side straddles the edge, taking room from the neighbour beyond it too.
        if (side == QLatin1String("left"))
            QVERIFY2(h.left() <= g.left() + 1 && h.right() > g.left() && h.right() <= g.center().x() + g.width() / 4 + 2,
                     qPrintable(report(h)));
        else if (side == QLatin1String("right"))
            QVERIFY2(h.right() >= g.right() - 1 && h.left() < g.right() && h.left() >= g.center().x() - g.width() / 4 - 2,
                     qPrintable(report(h)));
        else if (side == QLatin1String("top"))
            QVERIFY2(h.top() <= g.top() + 1 && h.bottom() > g.top() && h.bottom() <= g.center().y() + g.height() / 4 + 2,
                     qPrintable(report(h)));
        else if (side == QLatin1String("bottom"))
            QVERIFY2(h.bottom() >= g.bottom() - 1 && h.top() < g.bottom() && h.top() >= g.center().y() - g.height() / 4 - 2,
                     qPrintable(report(h)));
        else
            QVERIFY2(qAbs(h.width() - g.width()) <= 2 && qAbs(h.height() - g.height()) <= 2, qPrintable(report(h)));

        release.dismiss();
        sendMouse(receiver, QEvent::MouseButtonRelease, to, Qt::NoButton);
        QTRY_VERIFY(!dock("Audio")->property("isFloating").toBool());
        if (side == QLatin1String("tab")) {
            QCOMPARE(docking()->tabCount(QStringLiteral("Audio")), 2);
            QCOMPARE(docking()->tabCount(target), 2);
            restoreInitial();
            return;
        }
        const QRectF a = sceneRect(groupOf(header(QStringLiteral("Audio"))));
        const QRectF t = sceneRect(groupOf(header(target)));
        const QString landed = QStringLiteral("Audio %1,%2 %3x%4 %5 %6,%7 %8x%9")
                                   .arg(a.x()).arg(a.y()).arg(a.width()).arg(a.height()).arg(target)
                                   .arg(t.x()).arg(t.y()).arg(t.width()).arg(t.height());
        if (side == QLatin1String("left"))
            QVERIFY2(a.right() <= t.left() + 2 && a.top() < t.bottom() && a.bottom() > t.top(), qPrintable(landed));
        else if (side == QLatin1String("right"))
            QVERIFY2(a.left() >= t.right() - 2 && a.top() < t.bottom() && a.bottom() > t.top(), qPrintable(landed));
        else if (side == QLatin1String("top"))
            QVERIFY2(a.bottom() <= t.top() + 2 && a.left() < t.right() && a.right() > t.left(), qPrintable(landed));
        else
            QVERIFY2(a.top() >= t.bottom() - 2 && a.left() < t.right() && a.right() > t.left(), qPrintable(landed));
        restoreInitial();
    }

    void wholeFloatingHeaderDocks()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("a synthetic drag between windows needs the offscreen platform (the native gate drags for real)");
        restoreInitial();
        using hikari::ui::Docking;
        Docking *d = docking();
        QSignalSpy moves(d, &Docking::systemMoveRequested);
        QSignalSpy menus(d, &Docking::menuRequested);
        auto dragOverGrid = [&](QWindow *from, QPoint start) {
            QQuickItem *gridGroup = groupOf(header(QStringLiteral("Grid")));
            const QRectF g = gridGroup->mapRectToScene(QRectF(0, 0, gridGroup->width(), gridGroup->height()));
            const QPoint left = window->mapToGlobal(QPointF(g.left() + g.width() / 8, g.center().y()).toPoint());
            sendMouse(from, QEvent::MouseButtonPress, start, Qt::LeftButton);
            for (int i = 1; i <= 4; ++i)
                sendMouse(from, QEvent::MouseMove, start + QPoint(4 * i, 0), Qt::LeftButton);
            const QPoint along = start + QPoint(16, 0);
            for (int i = 1; i <= 12; ++i)
                sendMouse(from, QEvent::MouseMove, along + (left - along) * i / 12, Qt::LeftButton);
            const bool highlighted = QTest::qWaitFor([&] { return !itemsNamed(window, QStringLiteral("dockDropHighlight")).isEmpty(); });
            sendMouse(from, QEvent::MouseButtonRelease, left, Qt::NoButton);
            return highlighted;
        };

        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        QWindow *floating = bar->window();
        QTRY_VERIFY(floating->isExposed());
        QVERIFY(bar->property("titleMode").toBool());
        QQuickItem *title = childNamed(bar, QStringLiteral("dockTitleText"));
        QQuickItem *button = childNamed(bar, QStringLiteral("dockMenuButton"));
        QVERIFY(title && button);
        QVERIFY2(title->x() + title->width() >= button->x() - 4 - 0.5,
                 qPrintable(QStringLiteral("title %1+%2, button %3").arg(title->x()).arg(title->width()).arg(button->x())));
        const QPoint free = bar->mapToScene(QPointF(button->x() - 16, bar->height() / 2)).toPoint();
        QVERIFY(free.x() > title->mapToScene(QPointF(title->property("contentWidth").toReal() + 8, 0)).x());
        QTest::mouseClick(floating, Qt::RightButton, {}, free);
        QTRY_COMPARE(menus.count(), 1);
        QCOMPARE(menus.first().first().toString(), QStringLiteral("Audio"));
        QVERIFY(audio->property("isFloating").toBool());
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseDClick(floating, Qt::LeftButton, {}, free);
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        QVERIFY(audio->setProperty("isFloating", true));
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        floating = bar->window();
        QTRY_VERIFY(floating->isExposed());
        button = childNamed(bar, QStringLiteral("dockMenuButton"));
        QVERIFY(dragOverGrid(floating, floating->mapToGlobal(bar->mapToScene(QPointF(button->x() - 16, bar->height() / 2)).toPoint())));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        QQuickItem *gridGroup = groupOf(header(QStringLiteral("Grid")));
        QTRY_VERIFY(groupOf(header(QStringLiteral("Audio")))->mapToScene(QPointF()).x() < gridGroup->mapToScene(QPointF()).x() + 2);
        QCOMPARE(moves.count(), 0);

        restoreInitial();
        auto *grid = dock("Grid");
        QVERIFY(grid->setProperty("isFloating", true));
        QTRY_VERIFY((bar = header(QStringLiteral("Grid"))) && bar->window() != window);
        floating = bar->window();
        QTRY_VERIFY(floating->isExposed());
        QVERIFY(!bar->property("titleMode").toBool());
        QQuickItem *tab = childNamed(bar, QStringLiteral("dockTab0"));
        QQuickItem *slot = childNamed(bar, QStringLiteral("dockToolbarSlot"));
        QVERIFY(tab && slot);
        const QRectF t = tab->mapRectToItem(bar, QRectF(0, 0, tab->width(), tab->height()));
        const QRectF sl = slot->mapRectToItem(bar, QRectF(0, 0, slot->width(), slot->height()));
        QVERIFY2(sl.left() - t.right() > 20, qPrintable(QStringLiteral("tab ..%1, toolbar %2..").arg(t.right()).arg(sl.left())));
        const QPoint rightOfTabs = floating->mapToGlobal(bar->mapToScene(QPointF((t.right() + sl.left()) / 2, bar->height() / 2)).toPoint());
        QQuickItem *videoGroup = groupOf(header(QStringLiteral("Video")));
        const QRectF v = videoGroup->mapRectToScene(QRectF(0, 0, videoGroup->width(), videoGroup->height()));
        const QPoint overVideo = window->mapToGlobal(QPointF(v.left() + v.width() / 8, v.center().y()).toPoint());
        sendMouse(floating, QEvent::MouseButtonPress, rightOfTabs, Qt::LeftButton);
        for (int i = 1; i <= 4; ++i)
            sendMouse(floating, QEvent::MouseMove, rightOfTabs + QPoint(4 * i, 0), Qt::LeftButton);
        for (int i = 1; i <= 12; ++i)
            sendMouse(floating, QEvent::MouseMove, rightOfTabs + QPoint(16, 0) + (overVideo - rightOfTabs - QPoint(16, 0)) * i / 12,
                      Qt::LeftButton);
        QTRY_VERIFY(!itemsNamed(window, QStringLiteral("dockDropHighlight")).isEmpty());
        sendMouse(floating, QEvent::MouseButtonRelease, overVideo, Qt::NoButton);
        QTRY_VERIFY(!grid->property("isFloating").toBool());
        QCOMPARE(moves.count(), 0);

        restoreInitial();
        auto *video = dock("Video");
        QVERIFY(video->setProperty("isFloating", true));
        QQuickItem *videoBar = nullptr;
        QTRY_VERIFY((videoBar = header(QStringLiteral("Video"))) && videoBar->window() != window);
        auto *pair = qobject_cast<QQuickWindow *>(videoBar->window());
        QVERIFY(QMetaObject::invokeMethod(window, "pairFloating"));
        QTRY_VERIFY(header(QStringLiteral("Audio")) && header(QStringLiteral("Audio"))->window() == pair);
        QTRY_VERIFY(!itemsNamed(pair, QStringLiteral("dockTitleBar")).isEmpty());
        bar = itemsNamed(pair, QStringLiteral("dockTitleBar")).first();
        title = childNamed(bar, QStringLiteral("dockTitleText"));
        button = childNamed(bar, QStringLiteral("dockMenuButton"));
        QVERIFY(title && button);
        QVERIFY(title->x() + title->width() >= button->x() - 4 - 0.5);
        QVERIFY(dragOverGrid(pair, pair->mapToGlobal(bar->mapToScene(QPointF(button->x() - 16, bar->height() / 2)).toPoint())));
        QTRY_VERIFY(!video->property("isFloating").toBool() && !audio->property("isFloating").toBool());
        QCOMPARE(moves.count(), 0);
        restoreInitial();
    }

    void floatingHeaderHasNoSystemMoveArea()
    {
        restoreInitial();
        const auto noMoveArea = [](QQuickItem *bar) {
            const QList<QQuickItem *> areas = itemsNamed(bar->window(), QStringLiteral("dockSystemMoveArea"));
            return areas.isEmpty();
        };
        const auto titleReachesTheButton = [](QQuickItem *bar) {
            QQuickItem *title = childNamed(bar, QStringLiteral("dockTitleText"));
            QQuickItem *button = childNamed(bar, QStringLiteral("dockMenuButton"));
            return title && button && qAbs(title->x() + title->width() - (button->x() - 4)) < 0.5;
        };
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        QTRY_VERIFY(bar->window()->isExposed());
        QVERIFY(bar->property("titleMode").toBool());
        QVERIFY(noMoveArea(bar));
        QTRY_VERIFY(titleReachesTheButton(bar));

        auto *video = dock("Video");
        QVERIFY(video->setProperty("isFloating", true));
        QQuickItem *videoBar = nullptr;
        QTRY_VERIFY((videoBar = header(QStringLiteral("Video"))) && videoBar->window() != window);
        auto *pair = qobject_cast<QQuickWindow *>(videoBar->window());
        QVERIFY(QMetaObject::invokeMethod(window, "pairFloating"));
        QTRY_VERIFY(header(QStringLiteral("Audio")) && header(QStringLiteral("Audio"))->window() == pair);
        QTRY_VERIFY(!itemsNamed(pair, QStringLiteral("dockTitleBar")).isEmpty());
        QQuickItem *titleBar = itemsNamed(pair, QStringLiteral("dockTitleBar")).first();
        QVERIFY(noMoveArea(titleBar));
        QTRY_VERIFY(titleReachesTheButton(titleBar));

        restoreInitial();
        auto *grid = dock("Grid");
        QVERIFY(grid->setProperty("isFloating", true));
        QTRY_VERIFY((bar = header(QStringLiteral("Grid"))) && bar->window() != window);
        QVERIFY(!bar->property("titleMode").toBool());
        QVERIFY(noMoveArea(bar));
        restoreInitial();
    }

    void floatingHeaderDocksByItsTitle()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("a synthetic drag between windows needs the offscreen platform (the native gate drags for real)");
        restoreInitial();
        QSignalSpy moves(docking(), &hikari::ui::Docking::systemMoveRequested);
        auto *audio = dock("Audio");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio"))) && bar->window() != window);
        QWindow *floating = bar->window();
        QTRY_VERIFY(floating->isExposed());
        QQuickItem *title = childNamed(bar, QStringLiteral("dockTitleText"));
        QQuickItem *gridGroup = groupOf(header(QStringLiteral("Grid")));
        const QRectF g = gridGroup->mapRectToScene(QRectF(0, 0, gridGroup->width(), gridGroup->height()));
        const QPoint start = floating->mapToGlobal(title->mapToScene(QPointF(6, title->height() / 2)).toPoint());
        const QPoint left = window->mapToGlobal(QPointF(g.left() + g.width() / 8, g.center().y()).toPoint());
        sendMouse(floating, QEvent::MouseButtonPress, start, Qt::LeftButton);
        for (int i = 1; i <= 4; ++i)
            sendMouse(floating, QEvent::MouseMove, start + QPoint(4 * i, 0), Qt::LeftButton);
        const QPoint from = start + QPoint(16, 0);
        for (int i = 1; i <= 12; ++i)
            sendMouse(floating, QEvent::MouseMove, from + (left - from) * i / 12, Qt::LeftButton);
        QTRY_VERIFY(!itemsNamed(window, QStringLiteral("dockDropHighlight")).isEmpty());
        sendMouse(floating, QEvent::MouseButtonRelease, left, Qt::NoButton);
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        QCOMPARE(moves.count(), 0);
        QQuickItem *audioGroup = groupOf(header(QStringLiteral("Audio")));
        QTRY_VERIFY(audioGroup->mapToScene(QPointF()).x() < gridGroup->mapToScene(QPointF()).x() + 2);
    }

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

    void crowdedTabsKeepTheSelectedOneWhole()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop")));
        restoreInitial();
        QVERIFY(QMetaObject::invokeMethod(window, "tabIntoVideo"));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Editor"))) && bar->property("count").toInt() == 3);
        for (int i = 0; i < 3; ++i) {
            QQuickItem *tab = childNamed(bar, QStringLiteral("dockTab%1").arg(i));
            QCOMPARE(tab->width(), tab->property("naturalWidth").toReal());
            QVERIFY(!tab->property("cutOff").toBool());
        }
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
