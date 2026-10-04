// N8: a script dialog rendered by fixed QML controls from plain data. The
// round trip runs the real Lua helper: the script asks, the QML window shows
// the controls, the test edits them and presses a button, and the script
// sees typed values (false kept, the label's nil absent).
#include "automation_dialog_controller.h"
#include "hikari/backends/lua_script_host.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWheelEvent>
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

    QQuickItem *named(const QString &name)
    {
        return qobject_cast<QQuickItem *>(findNamed(window->contentItem(), name));
    }
    QQuickItem *controlItem(int index) { return qobject_cast<QQuickItem *>(control(index)); }
    // The control or button that has the focus, by its name.
    QString focusName()
    {
        for (QQuickItem *item = window->activeFocusItem(); item; item = item->parentItem())
            if (!item->objectName().isEmpty())
                return item->objectName();
        return QString();
    }
    void showDialog(const DialogRequest &request, std::optional<DialogResult> &result)
    {
        result.reset();
        controller->present(QStringLiteral("Keys"), request, [&result](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        window->requestActivate();
        QTRY_VERIFY(QGuiApplication::focusWindow() == window);
        QTRY_COMPARE(focusName(), QStringLiteral("dialogButton0"));
    }
    static QPoint centre(QQuickItem *item)
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
    void wheel(QQuickItem *item, int notches)
    {
        const QPointF at = centre(item);
        QWheelEvent event(at, window->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(window, &event);
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
        control(2)->setProperty("text", QStringLiteral("99")); // clamped to the range
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

    // L2: colour pickers show the legacy colour (AA is ASS transparency) and
    // return legacy hex; number fields take a comma as the decimal point.
    void coloursAndNumbersFollowTheLegacyControls()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "coloralpha", .name = "c", .text = "#800000FF"},
                            DialogControl{.kind = "color", .name = "d", .text = "#00FF00"},
                            DialogControl{.kind = "floatedit", .name = "f", .number = 1, .numberMin = 0, .numberMax = 10}};
        request.buttons = {"OK"};
        std::optional<DialogResult> result;
        controller->present(QStringLiteral("Controls"), request, [&](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        const QColor shown = control(0)->property("value").value<QColor>();
        QCOMPARE(shown, QColor(0, 0, 255, 255 - 0x80)); // transparency 0x80
        control(0)->setProperty("value", QColor(0, 0, 255, 128));
        control(1)->setProperty("value", QColor(255, 0, 0)); // a picked opaque colour
        control(2)->setProperty("text", QStringLiteral("2,5"));
        press(0);
        QVERIFY(result);
        QCOMPARE(std::get<std::string>(result->values[0]), std::string("#7F0000FF"));
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("#FF0000"));
        QCOMPARE(std::get<double>(result->values[2]), 2.5);
    }

    // Legacy HikariDialog::Show focuses the enter button (LuaDialog::
    // CreateWindow: the first OK/Yes/Save button, else the first; the
    // rewrite drops the IDs, so the first), so typing reaches no field and
    // Return presses that button (arrows: arrowsOnAButtonMoveTheFocusPerPlatform).
    void enterButtonHasTheFocusOnShow()
    {
        std::optional<DialogResult> result;
        controller->present(QStringLiteral("Fixture"), sampleRequest(), [&](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        window->requestActivate();
        QTRY_VERIFY(QGuiApplication::focusWindow() == window);
        QTRY_VERIFY(window->activeFocusItem());
        QCOMPARE(window->activeFocusItem()->objectName(), QStringLiteral("dialogButton0"));
        for (const QChar c : QStringLiteral("xyz"))
            QTest::sendKeyEvent(QTest::Click, window, Qt::Key_unknown, QString(c), Qt::NoModifier);
        QCOMPARE(control(1)->property("text").toString(), QStringLiteral("t"));
        QCOMPARE(control(2)->property("value").toInt(), 12);
        QCOMPARE(control(3)->property("text").toString(), QStringLiteral("1.5"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 0);
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("t"));
        QTRY_VERIFY(!window->isVisible());

        // Shown again, the enter button has the focus again.
        result.reset();
        controller->present(QStringLiteral("Again"), sampleRequest(), [&](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        window->requestActivate();
        QTRY_VERIFY(window->activeFocusItem() &&
                    window->activeFocusItem()->objectName() == QStringLiteral("dialogButton0"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, -1); // OnEscape: no button pushed
    }

    // L2, legacy HikariDialog::OnCharHook and the controls' keys (see
    // AutomationDialog.qml): Return presses the focused button, Escape ends
    // the dialog with no button, Space does nothing on a button.
    void returnPressesTheFocusedButtonAndEscapeNone()
    {
        std::optional<DialogResult> result;
        showDialog(sampleRequest(), result);
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_Enter, Qt::KeypadModifier); // Linux: not the Return accelerator
        QVERIFY(!result.has_value());
        QVERIFY(controlItem(4)->property("checked").toBool());
        named(QStringLiteral("dialogButton1"))->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 1);

        showDialog(sampleRequest(), result);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, -1);
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("t")); // values are read back

        // On Windows keypad Enter matches the Return accelerator.
        window->setProperty("navigationPlatform", QStringLiteral("windows"));
        showDialog(sampleRequest(), result);
        QTest::keyClick(window, Qt::Key_Enter, Qt::KeypadModifier);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 0);
        window->setProperty("navigationPlatform", QStringLiteral("linux"));
    }

    // A text field takes Return and Escape (OnCharHook skips them for a
    // HikariTextCtrl): a single-line one ignores them, the textbox makes a
    // new line; the dialog stays open.
    void textFieldsKeepReturnAndEscape()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "edit", .name = "e", .text = ""},
                            DialogControl{.kind = "textbox", .name = "t", .y = 1, .text = ""},
                            DialogControl{.kind = "intedit", .name = "i", .y = 2, .intValue = 1, .intMin = 0, .intMax = 9},
                            DialogControl{.kind = "floatedit", .name = "f", .y = 3, .number = 1, .numberMin = 0,
                                          .numberMax = 9}};
        std::optional<DialogResult> result;
        showDialog(request, result);
        for (int i = 0; i < 4; ++i) {
            controlItem(i)->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_A);
            QTest::keyClick(window, Qt::Key_Return);
            QTest::keyClick(window, Qt::Key_Escape);
            QTest::keyClick(window, Qt::Key_Enter, Qt::KeypadModifier);
            QTest::qWait(20);
            QVERIFY2(!result.has_value(), qPrintable(QString::number(i)));
            QVERIFY(window->isVisible());
        }
        QCOMPARE(controlItem(0)->property("text").toString(), QStringLiteral("a"));
        QCOMPARE(controlItem(1)->property("text").toString(), QStringLiteral("a\n"));
        QCOMPARE(controlItem(2)->property("text").toString(), QStringLiteral("1")); // digits only
        QCOMPARE(controlItem(3)->property("text").toString(), QStringLiteral("1"));
        named(QStringLiteral("dialogButton0"))->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 0);
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("a\n"));
    }

    // A checkbox and a dropdown handle no Return or Escape: OnCharHook clicks
    // the enter or escape ID on the dialog, which ends with no button pushed
    // (the script's button is false). Space toggles or opens nothing.
    void checkboxAndDropdownEndTheDialogWithoutAButton()
    {
        for (const auto &[index, key] : {std::pair{4, Qt::Key_Return}, std::pair{4, Qt::Key_Escape},
                                         std::pair{5, Qt::Key_Return}, std::pair{5, Qt::Key_Escape}}) {
            std::optional<DialogResult> result;
            showDialog(sampleRequest(), result);
            controlItem(index)->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            QTest::qWait(20);
            QVERIFY(controlItem(4)->property("checked").toBool());
            QVERIFY(!result.has_value());
            QTest::keyClick(window, key);
            QTRY_VERIFY(result.has_value());
            QCOMPARE(result->pressed, -1);
            QCOMPARE(std::get<bool>(result->values[4]), true);
            QCOMPARE(std::get<std::string>(result->values[5]), std::string("b"));
        }
    }

    // Legacy NumCtrl (int): no key steps it; the wheel adds a notch at a
    // time and a right-button drag changes it (1 per 8 px up or down, 10 per
    // 10 px sideways), within the range.
    void intEditStepsByWheelAndRightDragOnly()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "intedit", .name = "i", .intValue = 5, .intMin = 0, .intMax = 30}};
        std::optional<DialogResult> result;
        showDialog(request, result);
        QQuickItem *field = controlItem(0);
        field->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Up);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(field->property("text").toString(), QStringLiteral("5"));
        wheel(field, 1);
        QCOMPARE(field->property("text").toString(), QStringLiteral("6"));
        wheel(field, -2);
        QCOMPARE(field->property("text").toString(), QStringLiteral("4"));
        const QPoint at = centre(field);
        QTest::mousePress(window, Qt::RightButton, Qt::NoModifier, at);
        QTest::mouseMove(window, at - QPoint(0, 9));
        QCOMPARE(field->property("text").toString(), QStringLiteral("5"));
        QTest::mouseMove(window, at - QPoint(-11, 9));
        QCOMPARE(field->property("text").toString(), QStringLiteral("15"));
        QTest::mouseRelease(window, Qt::RightButton, Qt::NoModifier, at - QPoint(-11, 9));
        // Past the range the wheel shows nothing, but the value keeps the step.
        wheel(field, 20);
        QCOMPARE(field->property("text").toString(), QStringLiteral("15"));
        wheel(field, -10);
        QCOMPARE(field->property("text").toString(), QStringLiteral("25"));
        named(QStringLiteral("dialogButton0"))->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(std::get<int>(result->values[0]), 25);
    }

    // The arrows on a button move the focus: the Linux build's GtkWindow
    // moves it to the nearest control in that direction overlapping the
    // button (else nowhere); the Windows build to the previous or next
    // control in creation order, wrapping (HikariDialog::SetNextControl).
    void arrowsOnAButtonMoveTheFocusPerPlatform()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "edit", .name = "e", .text = ""}};
        std::optional<DialogResult> result;
        window->setProperty("navigationPlatform", QStringLiteral("linux"));
        showDialog(request, result);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(focusName(), QStringLiteral("dialogButton0"));
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(focusName(), QStringLiteral("dialogButton0"));
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(focusName(), QStringLiteral("dialogButton1"));
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(focusName(), QStringLiteral("dialogButton1"));
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(focusName(), QStringLiteral("dialogButton0"));
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(focusName(), QStringLiteral("control_0"));
        QTest::keyClick(window, Qt::Key_Down); // the edit takes its arrows
        QCOMPARE(focusName(), QStringLiteral("control_0"));
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!result.has_value());
        window->close();
        QTRY_VERIFY(result.has_value());

        window->setProperty("navigationPlatform", QStringLiteral("windows"));
        showDialog(request, result);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(focusName(), QStringLiteral("dialogButton1"));
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(focusName(), QStringLiteral("control_0"));
        named(QStringLiteral("dialogButton0"))->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(focusName(), QStringLiteral("control_0"));
        named(QStringLiteral("dialogButton0"))->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Up, Qt::ControlModifier); // Ctrl+arrow is not navigation
        QCOMPARE(focusName(), QStringLiteral("dialogButton0"));
        window->close();
        QTRY_VERIFY(result.has_value());
        window->setProperty("navigationPlatform", QStringLiteral("linux"));
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
        control(2)->setProperty("text", QStringLiteral("99"));
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

    // S3/L2: the legacy capture probe's dialog cases (tools/legacy-capture
    // plan.json "automation") run through this dialog with the plan's keys,
    // as drive.py/drive_windows.py send them to the legacy app. Each case's
    // JSON line is the rewrite side of the legacy comparison
    // (artifacts/automation-capture-dialogs.json). The legacy baseline hangs
    // on all of them, so what is asserted comes from the legacy sources: the
    // enter button has the focus when the dialog shows (HikariDialog::Show);
    // Return presses it, Escape ends the dialog with no button (false); Up on
    // it moves the focus to the edit above (GTK directional focus on Linux,
    // the previous control on Windows), where Return does nothing, so
    // float-step-up and int-step-up stay open, unchanged, until the OK
    // button is clicked (f=1, i=1); typed text reaches no field (e="").
    void captureProbeDialogCasesAnswer()
    {
        QFile planFile(QStringLiteral(HIKARI_LEGACY_CAPTURE_PLAN));
        QVERIFY(planFile.open(QIODevice::ReadOnly));
        const QJsonArray steps =
            QJsonDocument::fromJson(planFile.readAll()).object().value(QStringLiteral("automation")).toObject().value(
                QStringLiteral("steps")).toArray();
        QVERIFY(!steps.isEmpty());
        QTemporaryDir work;
        QVERIFY(work.isValid());
        const QString output = work.filePath(QStringLiteral("capture.jsonl"));
        // The helper process inherits the environment, as the legacy app's Lua does.
        qputenv("HIKARI_CAPTURE_OUT", output.toLocal8Bit());
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_CAPTURE_PROBE),
                           QStringLiteral(HIKARI_LUA_INCLUDE));
        host.setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
            controller->present(QString::fromStdString(host.info().name), request, std::move(reply));
        });
        connect(&host, &LuaScriptHost::dialogWithdrawn, controller, &AutomationDialogController::withdraw);
        std::optional<LuaScriptHost::RunOutcome> outcome;
        QString message;
        connect(&host, &LuaScriptHost::finished, this, [&](LuaScriptHost::RunOutcome o, const QString &m) {
            outcome = o;
            message = m;
        });
        host.load();
        QTRY_COMPARE(host.state(), LuaScriptHost::State::Ready);
        qunsetenv("HIKARI_CAPTURE_OUT");

        auto lines = [&] {
            QFile f(output);
            QList<QByteArray> got;
            if (f.open(QIODevice::ReadOnly))
                for (const QByteArray &line : f.readAll().split('\n'))
                    if (!line.trimmed().isEmpty())
                        got << line;
            return got;
        };
        QJsonArray cases;
        for (const auto &value : steps) {
            const QJsonObject step = value.toObject();
            if (step.value(QStringLiteral("case")).toString() == QStringLiteral("corpus"))
                continue; // L6: the corpus has its own capture (LuaHelper.CaptureProbeCorpusRunsInThisHost)
            const QString name = step.value(QStringLiteral("case")).toString();
            QJsonObject record{{QStringLiteral("case"), name},
                               {QStringLiteral("macro"), step.value(QStringLiteral("macro"))},
                               {QStringLiteral("keys"), step.value(QStringLiteral("keys"))},
                               {QStringLiteral("status"), QStringLiteral("timeout")}};
            const qsizetype before = lines().size();
            outcome.reset();
            QVERIFY(host.run(step.value(QStringLiteral("macro")).toInt()));
            QTest::qWait(300);
            record.insert(QStringLiteral("dialog_shown"), window->isVisible());
            if (window->isVisible()) {
                window->requestActivate();
                QTRY_VERIFY(QGuiApplication::focusWindow() == window);
                record.insert(QStringLiteral("dialog_title"), window->title());
                record.insert(QStringLiteral("dialog_buttons"), QJsonArray::fromStringList(controller->buttons()));
                QTRY_VERIFY(window->activeFocusItem());
                record.insert(QStringLiteral("focus"), window->activeFocusItem()->objectName());
                for (const auto &key : step.value(QStringLiteral("keys")).toArray()) {
                    const QString chord = key.toString();
                    if (chord.startsWith(QStringLiteral("type:"))) {
                        for (const QChar c : chord.mid(5))
                            QTest::sendKeyEvent(QTest::Click, window, Qt::Key_unknown, QString(c), Qt::NoModifier);
                    } else {
                        const Qt::Key code = chord == QStringLiteral("Return")   ? Qt::Key_Return
                                             : chord == QStringLiteral("Escape") ? Qt::Key_Escape
                                             : chord == QStringLiteral("Up")     ? Qt::Key_Up
                                                                                 : Qt::Key_unknown;
                        QVERIFY2(code != Qt::Key_unknown, qPrintable(chord));
                        QTest::keyClick(window, code);
                    }
                    QTest::qWait(50);
                }
            }
            if (!QTest::qWaitFor([&] { return outcome.has_value(); }, 1000)) {
                record.insert(QStringLiteral("dialog_open_after_keys"), window->isVisible());
                if (window->isVisible() && !step.value(QStringLiteral("keys")).toArray().isEmpty()) {
                    // The keys left it open: only a click closes it (legacy
                    // takes no other key there), on its first button.
                    record.insert(QStringLiteral("focus_after_keys"), focusName());
                    record.insert(QStringLiteral("closed_by"), QStringLiteral("click dialogButton0"));
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                                      centre(named(QStringLiteral("dialogButton0"))));
                } else {
                    // No keys answer it (the plan sends none): withdraw it,
                    // as a user cancelling the run would.
                    host.cancel();
                }
                QTRY_VERIFY(outcome.has_value());
            }
            record.insert(QStringLiteral("outcome"), static_cast<int>(*outcome));
            // An Ok run's message is its binary result payload, not text.
            if (*outcome != LuaScriptHost::RunOutcome::Ok && !message.trimmed().remove(QChar(0)).isEmpty())
                record.insert(QStringLiteral("message"), message.section(QLatin1Char('\n'), 0, 2));
            const QList<QByteArray> got = lines();
            if (got.size() > before) {
                record.insert(QStringLiteral("status"), QStringLiteral("captured"));
                record.insert(QStringLiteral("result"), QJsonDocument::fromJson(got.at(before)).object());
            }
            cases.append(record);
            QTRY_VERIFY(!window->isVisible());
        }
        QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
        QFile artifact(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/automation-capture-dialogs.json"));
        QVERIFY(artifact.open(QIODevice::WriteOnly));
        artifact.write(QJsonDocument(QJsonObject{{QStringLiteral("cases"), cases}}).toJson());
        QMap<QString, QJsonObject> byName;
        for (const auto &c : cases) {
            const QJsonObject record = c.toObject();
            const QString name = record.value(QStringLiteral("case")).toString();
            byName.insert(name, record);
            QVERIFY2(record.value(QStringLiteral("status")).toString() == QStringLiteral("captured") ||
                         record.contains(QStringLiteral("dialog_open_after_keys")),
                     qPrintable(name));
            if (record.value(QStringLiteral("dialog_shown")).toBool())
                QVERIFY2(record.value(QStringLiteral("focus")).toString() == QStringLiteral("dialogButton0"),
                         qPrintable(name));
        }
        const auto value = [&](const char *name, const char *field) {
            return byName.value(QLatin1String(name))
                .value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("values")).toObject()
                .value(QLatin1String(field)).toObject()
                .value(QStringLiteral("value"));
        };
        for (const char *name : {"float-step-up", "int-step-up"}) {
            const QJsonObject record = byName.value(QLatin1String(name));
            QVERIFY2(record.value(QStringLiteral("dialog_open_after_keys")).toBool(), name);
            QCOMPARE(record.value(QStringLiteral("focus_after_keys")).toString(), QStringLiteral("control_0"));
        }
        for (const char *name : {"defaults-ok", "buttons-return", "edit-typed", "defaults-escape", "buttons-escape"})
            QVERIFY2(!byName.value(QLatin1String(name)).contains(QStringLiteral("dialog_open_after_keys")), name);
        const auto button = [&](const char *name) {
            return byName.value(QLatin1String(name)).value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("button")).toObject();
        };
        QCOMPARE(button("defaults-ok").value(QStringLiteral("value")).toString(), QString());
        QCOMPARE(button("defaults-escape").value(QStringLiteral("type")).toString(), QStringLiteral("boolean"));
        QCOMPARE(button("buttons-return").value(QStringLiteral("value")).toString(), QStringLiteral("Apply"));
        QCOMPARE(button("buttons-escape").value(QStringLiteral("type")).toString(), QStringLiteral("boolean"));
        QCOMPARE(button("buttons-escape").value(QStringLiteral("value")).toBool(), false);
        QCOMPARE(value("float-step-up", "f").toDouble(), 1.0);
        QCOMPARE(value("int-step-up", "i").toDouble(), 1.0);
        QCOMPARE(value("edit-typed", "e").toString(), QString());
        QVERIFY(value("edit-typed", "e").isString());
    }
};

QTEST_MAIN(AutomationDialogTests)
#include "automation_dialog_tests.moc"
