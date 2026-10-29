#include "automation_manager_controller.h"
#include "hikari/backends/automation_manager.h"

#include <QDir>
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

}

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
        QCOMPARE(item(QStringLiteral("state_0"))->property("scriptState").toString(), QStringLiteral("ready"));
        QVERIFY(!item(QStringLiteral("state_0"))->property("visible").toBool());
    }

    void aFailedLoadSaysWhy()
    {
        controller->load(QStringLiteral(HIKARI_LUA_FIXTURES "/syntax-error.lua"));
        QTRY_COMPARE(scriptState(0), QStringLiteral("failed"));
        QTRY_VERIFY(item(QStringLiteral("error_0")));
        const QString error = item(QStringLiteral("error_0"))->property("text").toString();
        QVERIFY2(error.startsWith(QStringLiteral("syntax-error.lua:")), qPrintable(error));
        QVERIFY(error.contains(QStringLiteral("expected")));
        QVERIFY(item(QStringLiteral("error_0"))->property("visible").toBool());
    }

    void autoloadScriptsHaveAnEnabledBox()
    {
        const QString on = QStringLiteral(HIKARI_LUA_FIXTURES "/host-fixture.lua");
        const QString off = QStringLiteral("/scripts/off.lua");
        QStringList disabled{off};
        QList<std::pair<QString, bool>> toggled;
        controller->setAutoload([&] { return disabled; },
                                [&](const QString &path, bool enabled) { toggled.append({path, enabled}); });
        controller->load(on);
        QTRY_COMPARE(scriptState(0), QStringLiteral("ready"));
        QCOMPARE(scriptState(1), QStringLiteral("disabled"));
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        QTRY_VERIFY(item(QStringLiteral("enabled_1")));
        auto *offBox = qobject_cast<QQuickItem *>(item(QStringLiteral("enabled_1")));
        QVERIFY(offBox->isVisible());
        QVERIFY(!offBox->property("checked").toBool());
        QVERIFY(!item(QStringLiteral("reload_1"))->property("visible").toBool());
        QVERIFY(!item(QStringLiteral("state_1"))->property("visible").toBool());
        QVERIFY(item(QStringLiteral("enabled_0"))->property("checked").toBool());
        if (const QString out = qEnvironmentVariable("HIKARI_AUTOMATION_SHOT_DIR"); !out.isEmpty()) {
            QVERIFY(QDir().mkpath(out));
            QTest::qWait(300);
            QVERIFY(view->grabWindow().save(out + QStringLiteral("/automation-autoload-enabled.png")));
        }
        QVERIFY(QMetaObject::invokeMethod(offBox, "click"));
        QCOMPARE(toggled, (QList<std::pair<QString, bool>>{{off, true}}));
        auto *onBox = qobject_cast<QQuickItem *>(item(QStringLiteral("enabled_0")));
        QTest::mouseClick(view.get(), Qt::LeftButton, {},
                          onBox->mapToScene(QPointF(onBox->width() / 2, onBox->height() / 2)).toPoint());
        QCOMPARE(toggled.last(), std::pair(on, false));
        QVERIFY(controller->reload(on));
        QCOMPARE(scriptState(0), QStringLiteral("loading"));
        QTRY_VERIFY(!item(QStringLiteral("enabled_0"))->property("enabled").toBool());
        QTRY_COMPARE(scriptState(0), QStringLiteral("ready"));
        QTRY_VERIFY(item(QStringLiteral("enabled_0"))->property("enabled").toBool());
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
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("run_0_4")), "clicked"));
        QTRY_VERIFY(controller->busy());
        QTRY_VERIFY(!item(QStringLiteral("run_0_2"))->property("enabled").toBool());
        QVERIFY(!controller->run(path, 2));
        controller->cancel();
        QTRY_VERIFY(!controller->busy());
        QTRY_VERIFY(item(QStringLiteral("run_0_2"))->property("enabled").toBool());
    }
};

QTEST_MAIN(AutomationManagerUiTests)
#include "automation_manager_ui_tests.moc"
