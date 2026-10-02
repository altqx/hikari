// N8: a script dialog rendered by fixed QML controls from plain data. The
// round trip runs the real Lua helper: the script asks, the QML window shows
// the controls, the test edits them and presses a button, and the script
// sees typed values (false kept, the label's nil absent).
#include "automation_dialog_controller.h"
#include "hikari/backends/lua_script_host.h"

#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

#include <optional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using hikari::application::DialogControl;
using hikari::application::DialogRequest;
using hikari::application::DialogResult;
using hikari::backends::LuaScriptHost;
using hikari::ui::AutomationDialogController;

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

class AutomationDialogTests : public QObject {
    Q_OBJECT

    AutomationDialogController *controller = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    QObject *control(int index)
    {
        auto *loader = findNamed(window->contentItem(), QStringLiteral("control_%1").arg(index));
        return loader ? loader->property("item").value<QObject *>() : nullptr;
    }
    void press(int index)
    {
        QObject *button = findNamed(window->contentItem(), QStringLiteral("dialogButton%1").arg(index));
        QVERIFY(button);
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    }

    static DialogRequest sampleRequest()
    {
        DialogRequest r;
        DialogControl label{.kind = "label", .name = "lbl", .label = "Hello"};
        DialogControl edit{.kind = "edit", .name = "text", .x = 1, .width = 2, .text = "t"};
        DialogControl spin{.kind = "intedit", .name = "n", .y = 1, .intValue = 12, .intMin = 0, .intMax = 50};
        DialogControl number{.kind = "floatedit", .name = "f", .x = 1, .y = 1, .number = 1.5};
        DialogControl check{.kind = "checkbox", .name = "flag", .y = 2, .label = "Flag", .checked = true};
        DialogControl drop{.kind = "dropdown", .name = "pick", .x = 1, .y = 2, .text = "b", .items = {"a", "b"}};
        r.controls = {label, edit, spin, number, check, drop};
        r.buttons = {"Go", "Stop"};
        return r;
    }

private slots:
    void init()
    {
        controller = new AutomationDialogController;
        engine = new QQmlApplicationEngine;
        engine->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(controller)}});
        engine->loadFromModule("Hikari.Ui", "AutomationDialog");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
    }
    void cleanup()
    {
        delete engine;
        delete controller;
        engine = nullptr;
        controller = nullptr;
    }

    void fixedControlsReturnTypedValues()
    {
        std::optional<DialogResult> result;
        controller->present(QStringLiteral("Fixture"), sampleRequest(), [&](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        QCOMPARE(window->title(), QStringLiteral("Fixture"));
        QCOMPARE(control(0)->property("text").toString(), QStringLiteral("Hello"));
        QCOMPARE(control(1)->property("text").toString(), QStringLiteral("t"));
        QCOMPARE(control(2)->property("value").toInt(), 12);
        QVERIFY(control(4)->property("checked").toBool());
        QCOMPARE(control(5)->property("currentText").toString(), QStringLiteral("b"));

        control(1)->setProperty("text", QStringLiteral("typed"));
        control(2)->setProperty("value", 99); // the SpinBox clamps to its range
        control(3)->setProperty("text", QStringLiteral("2.5"));
        control(4)->setProperty("checked", false);
        control(5)->setProperty("currentIndex", 0);
        press(1);

        QVERIFY(result);
        QCOMPARE(result->pressed, 1);
        QCOMPARE(result->values.size(), 6u);
        QVERIFY(std::holds_alternative<std::monostate>(result->values[0]));
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("typed"));
        QCOMPARE(std::get<int>(result->values[2]), 50);
        QCOMPARE(std::get<double>(result->values[3]), 2.5);
        QCOMPARE(std::get<bool>(result->values[4]), false);
        QCOMPARE(std::get<std::string>(result->values[5]), std::string("a"));
        QTRY_VERIFY(!window->isVisible());
    }

    void closingAnswersWithoutAButton()
    {
        std::optional<DialogResult> result;
        controller->present(QStringLiteral("Fixture"), sampleRequest(), [&](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        window->close();
        QVERIFY(result);
        QCOMPARE(result->pressed, -1);
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("t")); // values are still read back
    }

    void defaultButtonsShowOkAndCancel()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "edit", .name = "e", .text = "x"}};
        controller->present(QStringLiteral("Default"), request, [](DialogResult) {});
        QCOMPARE(controller->buttons(), (QStringList{QStringLiteral("OK"), QStringLiteral("Cancel")}));
    }

    void scriptDialogRoundTripThroughQml()
    {
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_FIXTURES "/host-fixture.lua"),
                           QStringLiteral(HIKARI_LUA_INCLUDE));
        host.setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
            controller->present(QString::fromStdString(host.info().name), request, std::move(reply));
        });
        QStringList log;
        std::optional<LuaScriptHost::RunOutcome> outcome;
        connect(&host, &LuaScriptHost::logged, this, [&](const QString &t) { log << t; });
        connect(&host, &LuaScriptHost::finished, this,
                [&](LuaScriptHost::RunOutcome o, const QString &) { outcome = o; });
        host.load();
        QTRY_COMPARE(host.state(), LuaScriptHost::State::Ready);
        QVERIFY(host.run(0)); // "Dialog"
        QTRY_VERIFY(window->isVisible());
        QCOMPARE(window->title(), QStringLiteral("Host fixture"));

        control(1)->setProperty("text", QStringLiteral("typed"));
        control(2)->setProperty("value", 99);
        control(3)->setProperty("text", QStringLiteral("2.5"));
        control(4)->setProperty("checked", false);
        control(5)->setProperty("currentIndex", 0);
        press(1); // "Stop"

        QTRY_VERIFY(outcome.has_value());
        QCOMPARE(*outcome, LuaScriptHost::RunOutcome::Ok);
        QCOMPARE(log, QStringList{QStringLiteral(
                          "button=string:Stop 7=number:99 f=number:2.5 flag=boolean:false pick=string:a "
                          "text=string:typed")});
    }

    void cancelledRunWithdrawsTheDialog()
    {
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_FIXTURES "/host-fixture.lua"),
                           QStringLiteral(HIKARI_LUA_INCLUDE));
        host.setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
            controller->present(QStringLiteral("Cancel me"), request, std::move(reply));
        });
        connect(&host, &LuaScriptHost::dialogWithdrawn, controller, &AutomationDialogController::withdraw);
        std::optional<LuaScriptHost::RunOutcome> outcome;
        connect(&host, &LuaScriptHost::finished, this,
                [&](LuaScriptHost::RunOutcome o, const QString &) { outcome = o; });
        host.load();
        QTRY_COMPARE(host.state(), LuaScriptHost::State::Ready);
        QVERIFY(host.run(1)); // "Default buttons"
        QTRY_VERIFY(window->isVisible());
        host.cancel();
        QTRY_VERIFY(outcome.has_value());
        QCOMPARE(*outcome, LuaScriptHost::RunOutcome::Cancelled);
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(!controller->isOpen());
    }
};

QTEST_MAIN(AutomationDialogTests)
#include "automation_dialog_tests.moc"
