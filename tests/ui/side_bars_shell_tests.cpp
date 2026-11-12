#include "hikari/app/application.h"
#include "docking.h"
#include "settings_store.h"

#include <QDir>
#include <QJSValue>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

class SideBarsShellTests : public QObject {
    Q_OBJECT

    QTemporaryDir home;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    void start(const QString &sideBars = {})
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        o.legacyAutosaveDir = home.filePath(QStringLiteral("Subs"));
        application = new app::Application(o);
        if (!sideBars.isEmpty())
            application->settingsStore()->setValue(QStringLiteral("workspace.sideBars"), sideBars);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(root()->property("sideBarsLoaded").toBool());
    }
    QObject *root() const { return engine->rootObjects().first(); }
    template <typename T = QQuickItem> T *item(const char *name) const
    {
        return root()->findChild<T *>(QLatin1String(name));
    }
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *in(const char *bar, const QString &name) const { return findItem(item(bar), name); }
    QQuickItem *slotOf(const char *bar) const { return in(bar, QStringLiteral("sideBarSlot")); }
    QJsonObject saved() const
    {
        return QJsonDocument::fromJson(application->sideBars(shellWindow()).toUtf8()).object();
    }
    int shellWindow() const { return root()->property("shellWindow").toInt(); }
    QStringList panelsOf(const QJsonObject &bar) const
    {
        QStringList names;
        for (const QJsonValue &v : bar.value(QStringLiteral("panels")).toArray())
            names << v.toString();
        return names;
    }
    template <typename... Args> void call(const char *function, const Args &...args)
    {
        QVERIFY2(QMetaObject::invokeMethod(root(), function, Q_ARG(QVariant, QVariant::fromValue(args))...), function);
    }
    QVariant dock(const char *name) const { return QVariant::fromValue(item<QObject>(name)); }

private slots:
    void init() { QVERIFY(home.isValid()); }
    void cleanup()
    {
        delete engine;
        delete application;
        engine = nullptr;
        application = nullptr;
        QFile::remove(home.filePath(QStringLiteral("settings.toml")));
    }

    void theToolsSideBarStartsOnTheRightWithItsPanels()
    {
        start();
        auto *right = item("rightSideBar");
        QVERIFY(right->isVisible());
        for (const char *panel : {"sideBarIcon_Search", "sideBarIcon_Timing", "sideBarIcon_Postprocessor",
                                  "sideBarIcon_Styles", "sideBarIcon_QcNotes"})
            QVERIFY2(in("rightSideBar", QLatin1String(panel)), panel);
        QCOMPARE(item("searchPanel")->parentItem(), slotOf("rightSideBar"));
        QVERIFY(!item<QObject>("searchDock")->property("isOpen").toBool());
        QVERIFY(!item("timingPanel")->isVisible());
        QCOMPARE(item("dockingArea")->x() + item("dockingArea")->width(), right->x());
        const QJsonObject bars = saved();
        QCOMPARE(panelsOf(bars.value(QStringLiteral("second")).toObject()).first(), QStringLiteral("Explorer"));
        QCOMPARE(application->settingsStore()->text("workspace.sideBars").isEmpty(), false);
    }

    void anIconShowsItsPanelAndTheShownOneHidesTheSideBar()
    {
        start();
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_COMPARE(item("timingPanel")->parentItem(), slotOf("rightSideBar"));
        QVERIFY(item("timingPanel")->isVisible());
        QVERIFY(!item("searchPanel")->isVisible());
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("shown")).toString(),
                 QStringLiteral("Timing"));
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_VERIFY(!item("rightSideBar")->isVisible());
        QVERIFY(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("hidden")).toBool());
        QVERIFY(!item("timingPanel")->isVisible());
    }

    void theToggleCommandsHideAndShowEachSideBar()
    {
        start();
        auto *toggle = item<QObject>("rightSideBarToggle");
        QVERIFY(toggle);
        QVERIFY(toggle->property("checked").toBool());
        call("toggleSideBar", false);
        QTRY_VERIFY(!item("rightSideBar")->isVisible());
        QVERIFY(!toggle->property("checked").toBool());
        QCOMPARE(item("dockingArea")->x() + item("dockingArea")->width(), window->contentItem()->width());
        call("toggleSideBar", false);
        QTRY_VERIFY(item("rightSideBar")->isVisible());
        QVERIFY(toggle->property("checked").toBool());
    }

    void showingASideBarPanelShowsItInItsSideBar()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"],"shown":"Search","hidden":true}})"));
        QVERIFY(!item("rightSideBar")->isVisible());
        call("showPanel", dock("timingDock"));
        QTRY_VERIFY(item("rightSideBar")->isVisible());
        QCOMPARE(item("timingPanel")->parentItem(), slotOf("rightSideBar"));
        QTRY_VERIFY(item("timingPanel")->hasActiveFocus());
        QVERIFY(!item<QObject>("timingDock")->property("isOpen").toBool());
    }

    void aPanelMovesOutOfTheSideBarAndBack()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"],"shown":"Timing"}})"));
        QCOMPARE(item("timingPanel")->parentItem(), slotOf("rightSideBar"));
        call("moveOutOfSideBar", dock("timingDock"));
        QTRY_VERIFY(item<QObject>("timingDock")->property("isOpen").toBool());
        QVERIFY(item("timingPanel")->parentItem() != slotOf("rightSideBar"));
        QVERIFY(item("timingPanel")->isVisible());
        QCOMPARE(panelsOf(saved().value(QStringLiteral("second")).toObject()), QStringList{QStringLiteral("Search")});
        QCOMPARE(item("searchPanel")->parentItem(), slotOf("rightSideBar"));
        call("moveToSideBar", dock("timingDock"), true, QVariant());
        QTRY_VERIFY(!item<QObject>("timingDock")->property("isOpen").toBool());
        QCOMPARE(item("timingPanel")->parentItem(), slotOf("leftSideBar"));
        QVERIFY(item("leftSideBar")->isVisible());
        QCOMPARE(panelsOf(saved().value(QStringLiteral("first")).toObject()), QStringList{QStringLiteral("Timing")});
    }

    void theMiddlePanelsNeverGoInASideBar()
    {
        start();
        const QJsonObject before = saved();
        call("moveToSideBar", dock("gridDock"), true, QVariant());
        QCOMPARE(saved(), before);
        QVERIFY(item<QObject>("gridDock")->property("isOpen").toBool());
    }

    void closeTakesTheIconOffTheStrip()
    {
        start();
        call("closePanel", dock("qcNotesDock"));
        QTRY_VERIFY(!in("rightSideBar", QStringLiteral("sideBarIcon_QcNotes")));
        QVERIFY(!panelsOf(saved().value(QStringLiteral("second")).toObject()).contains(QStringLiteral("QcNotes")));
        QVERIFY(!item<QObject>("qcNotesDock")->property("isOpen").toBool());
    }

    void swapPutsTheAiSideBarOnTheOtherEdge()
    {
        start(QStringLiteral(R"({"first":{"panels":["Log"]},"second":{"panels":["Search"]}})"));
        QCOMPARE(item("logPanel")->parentItem(), slotOf("leftSideBar"));
        call("swapSideBars");
        QCOMPARE(application->settingsStore()->text("appearance.aiSideBar"), QStringLiteral("end"));
        QTRY_COMPARE(item("logPanel")->parentItem(), slotOf("rightSideBar"));
        QCOMPARE(item("searchPanel")->parentItem(), slotOf("leftSideBar"));
    }

    void lockLayoutKeepsPanelsInTheirSideBar()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"]}})"));
        QVERIFY(QMetaObject::invokeMethod(root()->property("workspaceLayout").value<QObject *>(), "setLocked", Q_ARG(bool, true)));
        const QJsonObject before = saved();
        call("moveOutOfSideBar", dock("timingDock"));
        call("moveToSideBar", dock("searchDock"), true, QVariant());
        QCOMPARE(saved(), before);
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_COMPARE(item("timingPanel")->parentItem(), slotOf("rightSideBar"));
    }

    void f6GoesFromTheLeftSideBarThroughTheMiddleToTheRight()
    {
        start(QStringLiteral(R"({"first":{"panels":["Log"]},"second":{"panels":["Search"]}})"));
        QVariant order;
        QVERIFY(QMetaObject::invokeMethod(root(), "sideBarPanelCycle", Q_RETURN_ARG(QVariant, order)));
        const QVariantList panels = order.metaType() == QMetaType::fromType<QJSValue>() ? order.value<QJSValue>().toVariant().toList()
                                                                                         : order.toList();
        QVERIFY(panels.size() > 2);
        QCOMPARE(panels.first().value<QQuickItem *>(), item("logPanel"));
        QCOMPARE(panels.last().value<QQuickItem *>(), item("searchPanel"));
    }

    void aDraggedEdgeBelowTheMinimumHidesTheSideBar()
    {
        start();
        call("resizeSideBar", false, 400, true);
        QTRY_COMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("width")).toInt(), 400);
        call("resizeSideBar", false, 100, true);
        QTRY_VERIFY(!item("rightSideBar")->isVisible());
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("width")).toInt(), 400);
    }

    void theSideBarsComeBackAfterARestart()
    {
        start();
        call("sideBarIconClicked", QStringLiteral("Styles"));
        QTRY_COMPARE(item("stylesPanel")->parentItem(), slotOf("rightSideBar"));
        cleanupEngineOnly();
        start();
        QTRY_COMPARE(item("stylesPanel")->parentItem(), slotOf("rightSideBar"));
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("shown")).toString(),
                 QStringLiteral("Styles"));
    }

private:
    void cleanupEngineOnly()
    {
        delete engine;
        delete application;
        engine = nullptr;
        application = nullptr;
    }
};

QTEST_MAIN(SideBarsShellTests)
#include "side_bars_shell_tests.moc"
