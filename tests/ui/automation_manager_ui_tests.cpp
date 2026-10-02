// L1: the automation manager tool in QML over the real manager and helpers:
// scripts in load order, macros with their legacy aliases, a visible reload,
// a failed load explained, and one macro at a time.
#include "automation_manager_controller.h"
#include "hikari/backends/automation_manager.h"

#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using hikari::backends::AutomationManager;
using hikari::ui::AutomationManagerController;

namespace {

QObject *findNamed(QQuickItem *root, const QString &name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems())
        if (QObject *found = findNamed(child, name))
            return found;
    return nullptr;
}

} // namespace

class AutomationManagerUiTests : public QObject {
    Q_OBJECT

    std::unique_ptr<AutomationManager> manager;
    std::unique_ptr<AutomationManagerController> controller;
    std::unique_ptr<QQuickView> view;

    QObject *item(const QString &name) { return findNamed(view->rootObject(), name); }
    QString scriptState(int index) { return controller->scripts().value(index).toMap().value("state").toString(); }

private slots:
    void init()
    {
        manager = std::make_unique<AutomationManager>(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_INCLUDE));
        controller = std::make_unique<AutomationManagerController>(*manager);
        view = std::make_unique<QQuickView>();
        view->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(controller.get())}});
        view->loadFromModule("Hikari.Ui", "AutomationManager");
        QVERIFY(view->rootObject());
        view->resize(480, 640);
        view->show();
    }
    void cleanup()
    {
        view.reset();
        controller.reset();
        manager.reset();
    }

    void scriptsAndMacrosAppearWithTheirAliases()
    {
        controller->load(QStringLiteral(HIKARI_LUA_FIXTURES "/host-fixture.lua"));
        QTRY_COMPARE(scriptState(0), QStringLiteral("ready"));
        const auto script = controller->scripts().value(0).toMap();
        QCOMPARE(script.value("name").toString(), QStringLiteral("Host fixture"));
        const auto macros = script.value("macros").toList();
        QCOMPARE(macros.value(2).toMap().value("name").toString(), QStringLiteral("Count"));
        QCOMPARE(macros.value(2).toMap().value("alias").toString(), QStringLiteral("host-fixture.lua:2"));
        QTRY_VERIFY(item(QStringLiteral("macro_0_2")));
        QCOMPARE(item(QStringLiteral("macro_0_2"))->property("text").toString(), QStringLiteral("Count"));
        QCOMPARE(item(QStringLiteral("state_0"))->property("text").toString(), QStringLiteral("ready"));
    }

    void aFailedLoadSaysWhy()
    {
        controller->load(QStringLiteral(HIKARI_LUA_FIXTURES "/syntax-error.lua"));
        QTRY_COMPARE(scriptState(0), QStringLiteral("failed"));
        QTRY_VERIFY(item(QStringLiteral("error_0")));
        QVERIFY(item(QStringLiteral("error_0"))->property("text").toString().contains(QStringLiteral("expected")));
        QVERIFY(item(QStringLiteral("error_0"))->property("visible").toBool());
    }

    void reloadIsVisible()
    {
        const QString path = QStringLiteral(HIKARI_LUA_FIXTURES "/host-fixture.lua");
        controller->load(path);
        QTRY_COMPARE(scriptState(0), QStringLiteral("ready"));
        QVERIFY(controller->reload(path));
        QCOMPARE(scriptState(0), QStringLiteral("loading"));
        QCOMPARE(controller->scripts().value(0).toMap().value("generation").toULongLong(), 2ull);
        QTRY_COMPARE(scriptState(0), QStringLiteral("ready"));
    }

    void runningOneMacroDisablesTheOthers()
    {
        const QString path = QStringLiteral(HIKARI_LUA_FIXTURES "/host-fixture.lua");
        controller->load(path);
        QTRY_COMPARE(scriptState(0), QStringLiteral("ready"));
        QTRY_VERIFY(item(QStringLiteral("macro_0_4")));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("macro_0_4")), "clicked")); // Wait for cancel
        QTRY_VERIFY(controller->busy());
        QTRY_VERIFY(!item(QStringLiteral("macro_0_2"))->property("enabled").toBool());
        QVERIFY(item(QStringLiteral("cancelRun"))->property("enabled").toBool());
        QVERIFY(!controller->run(path, 2));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("cancelRun")), "clicked"));
        QTRY_VERIFY(!controller->busy());
        QTRY_VERIFY(item(QStringLiteral("macro_0_2"))->property("enabled").toBool());
    }
};

QTEST_MAIN(AutomationManagerUiTests)
#include "automation_manager_ui_tests.moc"
