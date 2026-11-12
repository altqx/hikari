#include "hikari/app/application.h"
#include "docking.h"
#include "settings_store.h"

#include <QDir>
#include <QJSValue>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

class SideBarsShellTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> home;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    void start(const QString &sideBars = {})
    {
        app::Application::Options o;
        o.settingsFile = home->filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        o.legacyAutosaveDir = home->filePath(QStringLiteral("Subs"));
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
        if (T *found = root()->findChild<T *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (auto *found = qobject_cast<T *>(findItem(quick->contentItem(), QLatin1String(name))))
                    return found;
        return nullptr;
    }
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (!from)
            return nullptr;
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *panel(const char *name) const
    {
        QQuickItem *found = item(name);
        if (!found)
            qWarning("no item %s", name);
        return found;
    }
    QQuickItem *parentOf(const char *name) const
    {
        QQuickItem *found = panel(name);
        return found ? found->parentItem() : nullptr;
    }
    bool shown(const char *name) const
    {
        QQuickItem *found = panel(name);
        return found && found->isVisible();
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
    void init()
    {
        home = std::make_unique<QTemporaryDir>();
        QVERIFY(home->isValid());
    }
    void cleanup()
    {
        delete engine;
        delete application;
        engine = nullptr;
        application = nullptr;
        home.reset();
    }

    void theToolsSideBarStartsOnTheRightWithItsPanels()
    {
        start();
        auto *right = item("rightSideBar");
        QVERIFY(right);
        QVERIFY(right->isVisible());
        for (const char *panel : {"sideBarIcon_Search", "sideBarIcon_Timing", "sideBarIcon_Postprocessor",
                                  "sideBarIcon_Styles", "sideBarIcon_QcNotes"})
            QVERIFY2(in("rightSideBar", QLatin1String(panel)), panel);
        QCOMPARE(parentOf("searchPanel"), slotOf("rightSideBar"));
        QVERIFY(!item<QObject>("searchDock")->property("isOpen").toBool());
        QVERIFY(!shown("timingPanel"));
        QCOMPARE(item("dockingArea")->x() + item("dockingArea")->width(), right->x());
        const QJsonObject bars = saved();
        QCOMPARE(panelsOf(bars.value(QStringLiteral("second")).toObject()).first(), QStringLiteral("Explorer"));
        QCOMPARE(application->settingsStore()->text("workspace.sideBars").isEmpty(), false);
    }

    void anIconShowsItsPanelAndTheShownOneHidesTheSideBar()
    {
        start();
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_COMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
        QVERIFY(shown("timingPanel"));
        QVERIFY(!shown("searchPanel"));
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("shown")).toString(),
                 QStringLiteral("Timing"));
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_VERIFY(!shown("rightSideBar"));
        QVERIFY(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("hidden")).toBool());
        QVERIFY(!shown("timingPanel"));
    }

    void theToggleCommandsHideAndShowEachSideBar()
    {
        start();
        auto *toggle = item<QObject>("rightSideBarToggle");
        QVERIFY(toggle);
        QVERIFY(toggle->property("checked").toBool());
        call("toggleSideBar", false);
        QTRY_VERIFY(!shown("rightSideBar"));
        QVERIFY(!toggle->property("checked").toBool());
        QCOMPARE(item("dockingArea")->x() + item("dockingArea")->width(), window->contentItem()->width());
        call("toggleSideBar", false);
        QTRY_VERIFY(shown("rightSideBar"));
        QVERIFY(toggle->property("checked").toBool());
    }

    void showingASideBarPanelShowsItInItsSideBar()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"],"shown":"Search","hidden":true}})"));
        QVERIFY(!shown("rightSideBar"));
        call("showPanel", dock("timingDock"));
        QTRY_VERIFY(shown("rightSideBar"));
        QCOMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
        QTRY_VERIFY(panel("timingPanel") && panel("timingPanel")->hasActiveFocus());
        QVERIFY(!item<QObject>("timingDock")->property("isOpen").toBool());
    }

    void aPanelMovesOutOfTheSideBarAndBack()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"],"shown":"Timing"}})"));
        QCOMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
        call("moveOutOfSideBar", dock("timingDock"));
        QTRY_VERIFY(item<QObject>("timingDock")->property("isOpen").toBool());
        QVERIFY(parentOf("timingPanel") != slotOf("rightSideBar"));
        QVERIFY(shown("timingPanel"));
        QCOMPARE(panelsOf(saved().value(QStringLiteral("second")).toObject()), QStringList{QStringLiteral("Search")});
        QCOMPARE(parentOf("searchPanel"), slotOf("rightSideBar"));
        call("moveToSideBar", dock("timingDock"), true, QVariant());
        QTRY_VERIFY(!item<QObject>("timingDock")->property("isOpen").toBool());
        QCOMPARE(parentOf("timingPanel"), slotOf("leftSideBar"));
        QVERIFY(shown("leftSideBar"));
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
        QCOMPARE(parentOf("logPanel"), slotOf("leftSideBar"));
        call("swapSideBars");
        QCOMPARE(application->settingsStore()->text("appearance.aiSideBar"), QStringLiteral("end"));
        QTRY_COMPARE(parentOf("logPanel"), slotOf("rightSideBar"));
        QCOMPARE(parentOf("searchPanel"), slotOf("leftSideBar"));
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
        QTRY_COMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
    }

    void f6GoesFromTheLeftSideBarThroughTheMiddleToTheRight()
    {
        start(QStringLiteral(R"({"first":{"panels":["Log"]},"second":{"panels":["Search"]}})"));
        QVariant order;
        QVERIFY(QMetaObject::invokeMethod(root(), "sideBarPanelCycle", Q_RETURN_ARG(QVariant, order)));
        const QVariantList panels = order.metaType() == QMetaType::fromType<QJSValue>() ? order.value<QJSValue>().toVariant().toList()
                                                                                         : order.toList();
        QVERIFY(panels.size() > 2);
        QCOMPARE(panels.first().value<QQuickItem *>(), panel("logPanel"));
        QCOMPARE(panels.last().value<QQuickItem *>(), panel("searchPanel"));
    }

    void aDraggedEdgeBelowTheMinimumHidesTheSideBar()
    {
        start();
        call("resizeSideBar", false, 400, true);
        QTRY_COMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("width")).toInt(), 400);
        call("resizeSideBar", false, 100, true);
        QTRY_VERIFY(!shown("rightSideBar"));
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("width")).toInt(), 400);
    }

    void theSideBarsComeBackAfterARestart()
    {
        start();
        call("sideBarIconClicked", QStringLiteral("Styles"));
        QTRY_COMPARE(parentOf("stylesPanel"), slotOf("rightSideBar"));
        cleanupEngineOnly();
        start();
        QTRY_COMPARE(parentOf("stylesPanel"), slotOf("rightSideBar"));
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
