// D1: qualification of the pinned docking engine (KDDockWidgets 2.4.1,
// QtQuick frontend, docs/qt/docking.md) with the provisioned Qt: the
// default arrangement, float/redock, tabs, close/reopen, layout save and
// restore through the public LayoutSaver, and a corrupt layout refused
// without breaking the live one. Platform checks (screen readers, Wayland
// compositors, mixed DPI) are native observations, not these tests.

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/qtquick/Platform.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

namespace {

constexpr auto kShell = R"(
import QtQuick
import QtQuick.Controls
import com.kdab.dockwidgets 2.0 as KDDW
ApplicationWindow {
    width: 1000; height: 700; visible: true
    KDDW.DockingArea {
        id: area
        objectName: "area"
        anchors.fill: parent
        uniqueName: "Classic"
        KDDW.DockWidget { id: video; objectName: "Video"; uniqueName: "Video"; title: "Video"; Rectangle { anchors.fill: parent; color: "black" } }
        KDDW.DockWidget { id: audio; objectName: "Audio"; uniqueName: "Audio"; title: "Audio"; Rectangle { anchors.fill: parent } }
        KDDW.DockWidget { id: editor; objectName: "Editor"; uniqueName: "Editor"; title: "Editor"; TextArea { objectName: "editorText"; anchors.fill: parent } }
        KDDW.DockWidget { id: grid; objectName: "Grid"; uniqueName: "Grid"; title: "Grid"; Rectangle { anchors.fill: parent } }
        Component.onCompleted: {
            addDockWidget(video, KDDW.KDDockWidgets.Location_OnLeft)
            addDockWidget(audio, KDDW.KDDockWidgets.Location_OnRight)
            addDockWidget(editor, KDDW.KDDockWidgets.Location_OnBottom)
            addDockWidget(grid, KDDW.KDDockWidgets.Location_OnBottom)
        }
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

    QObject *dock(const char *name) const { return engine->rootObjects().first()->findChild<QObject *>(QLatin1String(name)); }
    QByteArray layout() const { return KDDockWidgets::LayoutSaver().serializeLayout(); }

private slots:
    void initTestCase()
    {
        KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
        engine = new QQmlApplicationEngine(this);
        KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(engine);
        engine->loadData(QByteArray(kShell));
        QVERIFY(!engine->rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
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
};

QTEST_MAIN(DockingQualification)
#include "docking_qualification_tests.moc"
