// N8: a script dialog rendered by fixed QML controls from plain data. The
// round trip runs the real Lua helper: the script asks, the QML window shows
// the controls, the test edits them and presses a button, and the script
// sees typed values (false kept, the label's nil absent).
#include "automation_dialog_controller.h"
#include "colour_picker_controller.h"
#include "settings_store.h"
#include "theme.h"
#include "hikari/backends/lua_script_host.h"

#include <QAccessible>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QScopeGuard>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>

#include <functional>
#include <optional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using hikari::application::DialogControl;
using hikari::application::DialogRequest;
using hikari::application::DialogResult;
using hikari::backends::LuaScriptHost;
using hikari::ui::AutomationDialogController;
using hikari::ui::ColourPickerController;

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
    ColourPickerController *colourPicker = nullptr;
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
        showDialogOn(request, result, QStringLiteral("dialogButton0"));
    }
    // Shows the dialog and waits for the Enter button, named, to have the keyboard.
    void showDialogOn(const DialogRequest &request, std::optional<DialogResult> &result, const QString &enter)
    {
        result.reset();
        QTest::mouseMove(window, QPoint(1, 1)); // off the controls: no hovered hint
        controller->present(QStringLiteral("Keys"), request, [&result](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        window->requestActivate();
        QTRY_VERIFY(QGuiApplication::focusWindow() == window);
        QTRY_COMPARE(focusName(), enter);
    }
    // Types ASCII text with real key events (QTest::keyClicks takes widgets only).
    static void type(QWindow *into, const QString &text)
    {
        for (const QChar c : text)
            QTest::keyClick(into, c.toLatin1());
    }
    QString hint()
    {
        QQuickItem *text = named(QStringLiteral("automationHintText"));
        return text ? text->property("text").toString() : QString();
    }
    static QString accessible(QQuickItem *item, QAccessible::Text which)
    {
        QAccessibleInterface *face = QAccessible::queryAccessibleInterface(item);
        return face ? face->text(which) : QString();
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
        colourPicker = new ColourPickerController;
        engine = new QQmlApplicationEngine;
        engine->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(controller)},
                                      {QStringLiteral("colourPicker"), QVariant::fromValue(colourPicker)}});
        engine->loadFromModule("Hikari.Ui", "AutomationDialog");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
    }
    void cleanup()
    {
        delete engine;
        delete controller;
        delete colourPicker;
        engine = nullptr;
        controller = nullptr;
        colourPicker = nullptr;
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

    // SY2: a colour control's button opens the colour popover at the colour,
    // with Alpha (the ASS transparency) only for coloralpha; each change
    // sets the value at once, Esc puts it back, Enter keeps it, and the
    // dialog reads the kept colour as legacy hex.
    void colourControlsOpenThePopover()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "coloralpha", .name = "c", .text = "#800000FF"},
                            DialogControl{.kind = "color", .name = "d", .x = 1, .text = "#00FF00"}};
        request.buttons = {"OK"};
        std::optional<DialogResult> result;
        showDialog(request, result);
        auto *popover = window->findChild<QObject *>(QStringLiteral("automationColourPopover"));
        QVERIFY(popover);
        auto *content = popover->property("contentItem").value<QQuickItem *>();
        auto *alphaField = qobject_cast<QQuickItem *>(findNamed(content, QStringLiteral("alpha")));
        QVERIFY(alphaField);

        auto *pick = findNamed(controlItem(0), QStringLiteral("colorPick"));
        QVERIFY(QMetaObject::invokeMethod(pick, "clicked"));
        QTRY_VERIFY(popover->property("visible").toBool());
        // A dialog this small cannot hold the popover with More… open: it
        // opens in a window of its own (Popup.Window), the keys go there.
        QCOMPARE(popover->property("popupType").toInt(), 1);
        QQuickWindow *own = content->window();
        QVERIFY(own && own != window);
        QTRY_VERIFY(own->isVisible());
        QCOMPARE(popover->property("blue").toInt(), 255);
        QCOMPARE(popover->property("red").toInt(), 0);
        QCOMPARE(popover->property("alpha").toInt(), 0x80);
        QVERIFY(alphaField->isVisible());
        QVERIFY(QMetaObject::invokeMethod(popover, "setRgb", Q_ARG(QVariant, 255), Q_ARG(QVariant, 0), Q_ARG(QVariant, 0)));
        QTRY_COMPARE(control(0)->property("value").value<QColor>(), QColor(255, 0, 0, 255 - 0x80));
        QTest::keyClick(content->window(), Qt::Key_Escape);
        QTRY_VERIFY(!popover->property("visible").toBool());
        QVERIFY(window->isVisible()); // Esc was the popover's, not the dialog's
        QCOMPARE(control(0)->property("value").value<QColor>(), QColor(0, 0, 255, 255 - 0x80));

        pick = findNamed(controlItem(1), QStringLiteral("colorPick"));
        QVERIFY(QMetaObject::invokeMethod(pick, "clicked"));
        QTRY_VERIFY(popover->property("visible").toBool());
        QVERIFY(!alphaField->isVisible());
        QCOMPARE(popover->property("green").toInt(), 255);
        QVERIFY(QMetaObject::invokeMethod(popover, "setRgb", Q_ARG(QVariant, 0), Q_ARG(QVariant, 0), Q_ARG(QVariant, 255)));
        QTest::keyClick(content->window(), Qt::Key_Return);
        QTRY_VERIFY(!popover->property("visible").toBool());
        QVERIFY(window->isVisible());
        QTRY_COMPARE(control(1)->property("value").value<QColor>(), QColor(0, 0, 255));
        // A press on the anchor back in the dialog keeps the colour and
        // closes the popover, without the button opening it again.
        auto *anchor = qobject_cast<QQuickItem *>(pick);
        QVERIFY(QMetaObject::invokeMethod(pick, "clicked"));
        QTRY_VERIFY(popover->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(popover, "setRgb", Q_ARG(QVariant, 0), Q_ARG(QVariant, 128), Q_ARG(QVariant, 0)));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          anchor->mapToScene(QPointF(anchor->width() / 2, anchor->height() / 2)).toPoint());
        QTRY_VERIFY(!popover->property("visible").toBool());
        QTest::qWait(100);
        QVERIFY(!popover->property("visible").toBool());
        QTRY_COMPARE(control(1)->property("value").value<QColor>(), QColor(0, 128, 0));
        // A press elsewhere closes it too; a click on the anchor straight
        // after opens it again.
        QVERIFY(QMetaObject::invokeMethod(pick, "clicked"));
        QTRY_VERIFY(popover->property("visible").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
        QTRY_VERIFY(!popover->property("visible").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          anchor->mapToScene(QPointF(anchor->width() / 2, anchor->height() / 2)).toPoint());
        QTRY_VERIFY(popover->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(popover, "setRgb", Q_ARG(QVariant, 0), Q_ARG(QVariant, 0), Q_ARG(QVariant, 255)));
        QTest::keyClick(content->window(), Qt::Key_Return);
        QTRY_VERIFY(!popover->property("visible").toBool());
        press(0);
        QVERIFY(result);
        QCOMPARE(std::get<std::string>(result->values[0]), std::string("#800000FF"));
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("#0000FF"));
    }

    // The enter button has the keyboard when the dialog shows (legacy
    // HikariDialog::Show, DG1), so typing reaches no field and Enter presses
    // it; shown again, it has the keyboard again.
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

        result.reset();
        controller->present(QStringLiteral("Again"), sampleRequest(), [&](DialogResult r) { result = r; });
        QTRY_VERIFY(window->isVisible());
        window->requestActivate();
        QTRY_VERIFY(window->activeFocusItem() &&
                    window->activeFocusItem()->objectName() == QStringLiteral("dialogButton0"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, -1);
    }

    // AD1-button-ids: the Enter button is Aegisub's affirmative one (an ok,
    // yes or save id, the last such), else the first; it is filled in the
    // accent and has the keyboard. The buttons keep the script's order at
    // the footer's trailing edge.
    void enterButtonFollowsTheButtonIds()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "label", .name = "l", .label = "x"}};
        request.buttons = {"Trust", "Fetch once", "Block", "Skip"};
        request.buttonIds = {"", "ok", "", "cancel"};
        std::optional<DialogResult> result;
        controller->present(QStringLiteral("Feed"), request, [&](DialogResult r) { result = r; });
        QCOMPARE(controller->enterButton(), 1);
        QTRY_VERIFY(window->isVisible());
        window->requestActivate();
        QTRY_COMPARE(focusName(), QStringLiteral("dialogButton1"));
        qreal x = -1;
        for (int i = 0; i < 4; ++i) {
            QQuickItem *b = named(QStringLiteral("dialogButton%1").arg(i));
            QVERIFY(b);
            const qreal at = b->mapToScene(QPointF()).x();
            QVERIFY2(at > x, qPrintable(QString::number(i)));
            x = at;
            QCOMPARE(QString::fromLatin1(b->metaObject()->className()).contains(QLatin1String("PrimaryButton")), i == 1);
        }
        QQuickItem *last = named(QStringLiteral("dialogButton3"));
        QVERIFY(last->mapToScene(QPointF(last->width(), 0)).x() > window->width() - 40); // at the trailing edge
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 1);

        request.buttonIds = {"", "yes", "save", "close"};
        controller->present(QStringLiteral("Affirmative"), request, [](DialogResult) {});
        QCOMPARE(controller->enterButton(), 2);
        request.buttonIds = {"", "", "", "cancel"};
        controller->present(QStringLiteral("None"), request, [](DialogResult) {});
        QCOMPARE(controller->enterButton(), 0);
        request.buttonIds.clear();
        controller->present(QStringLiteral("No ids"), request, [](DialogResult) {});
        QCOMPARE(controller->enterButton(), 0);
        controller->withdraw();
        QTRY_VERIFY(!window->isVisible());
    }

    // AD1-keyboard, one rule set on every platform: Enter (and keypad Enter)
    // presses the focused button, else the Enter button, from a single-line
    // field too; a textbox takes Enter as a new line and presses the Enter
    // button on Ctrl+Enter; Esc ends with no button from anywhere, text
    // fields included; Space ticks a check box and presses a button.
    void keysFollowOneRuleSet()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "edit", .name = "e", .text = ""},
                            DialogControl{.kind = "textbox", .name = "t", .y = 1, .text = ""},
                            DialogControl{.kind = "intedit", .name = "i", .y = 2, .intValue = 1, .intMin = 0, .intMax = 9},
                            DialogControl{.kind = "checkbox", .name = "c", .y = 3, .label = "Check"},
                            DialogControl{.kind = "dropdown", .name = "d", .y = 4, .text = "b", .items = {"a", "b"}}};
        request.buttons = {"Back", "Go", "Stop"};
        request.buttonIds = {"", "ok", "cancel"};
        std::optional<DialogResult> result;

        showDialogOn(request, result, QStringLiteral("dialogButton1"));
        controlItem(0)->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 1);
        QCOMPARE(std::get<std::string>(result->values[0]), std::string("a"));

        showDialogOn(request, result, QStringLiteral("dialogButton1"));
        controlItem(1)->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A);
        QTest::keyClick(window, Qt::Key_Return);
        QTest::keyClick(window, Qt::Key_B);
        QTest::qWait(20);
        QVERIFY(!result.has_value());
        QCOMPARE(controlItem(1)->property("text").toString(), QStringLiteral("a\nb"));
        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 1);
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("a\nb"));

        for (int i : {0, 1, 2, 3, 4}) {
            showDialogOn(request, result, QStringLiteral("dialogButton1"));
            controlItem(i)->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY2(result.has_value(), qPrintable(QString::number(i)));
            QCOMPARE(result->pressed, -1);
        }

        showDialogOn(request, result, QStringLiteral("dialogButton1"));
        controlItem(3)->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(controlItem(3)->property("checked").toBool());
        QTest::keyClick(window, Qt::Key_Enter, Qt::KeypadModifier);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 1);
        QCOMPARE(std::get<bool>(result->values[3]), true);

        showDialogOn(request, result, QStringLiteral("dialogButton1"));
        named(QStringLiteral("dialogButton0"))->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 0);

        showDialogOn(request, result, QStringLiteral("dialogButton1"));
        named(QStringLiteral("dialogButton2"))->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 2);
    }

    // AD1-keyboard: Tab goes through the grid row by row, left to right
    // (a colour's swatch, then its field), then the buttons in order;
    // Shift+Tab goes back. The script lists its controls in another order.
    void tabFollowsTheGrid()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "edit", .name = "b", .x = 1, .y = 1, .text = ""},
                            DialogControl{.kind = "checkbox", .name = "c", .y = 2, .label = "c"},
                            DialogControl{.kind = "color", .name = "a2", .x = 1, .text = "#000000"},
                            DialogControl{.kind = "textbox", .name = "b0", .y = 1, .text = ""},
                            DialogControl{.kind = "label", .name = "l", .label = "L"},
                            DialogControl{.kind = "intedit", .name = "a3", .x = 2, .intValue = 0}};
        request.buttons = {"One", "Two"};
        std::optional<DialogResult> result;
        showDialog(request, result);
        auto *swatch = qobject_cast<QQuickItem *>(findNamed(controlItem(2), QStringLiteral("colorPick")));
        QVERIFY(swatch);
        swatch->forceActiveFocus(Qt::TabFocusReason);
        QStringList order;
        for (int i = 0; i < 8; ++i) {
            QQuickItem *f = window->activeFocusItem();
            order << (f->objectName().isEmpty() ? focusName() : f->objectName());
            QTest::keyClick(window, Qt::Key_Tab);
        }
        QCOMPARE(order, (QStringList{QStringLiteral("colorPick"), QStringLiteral("colorText"), QStringLiteral("numberField"),
                                     QStringLiteral("control_3"), QStringLiteral("control_0"), QStringLiteral("control_1"),
                                     QStringLiteral("dialogButton0"), QStringLiteral("dialogButton1")}));
        QTest::keyClick(window, Qt::Key_Backtab);
        QCOMPARE(focusName(), QStringLiteral("dialogButton1"));
        controlItem(3)->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Tab); // the textbox passes Tab on
        QCOMPARE(focusName(), QStringLiteral("control_0"));
        QTest::keyClick(window, Qt::Key_Backtab);
        QCOMPARE(focusName(), QStringLiteral("control_3"));
        QCOMPARE(controlItem(3)->property("text").toString(), QString());
        window->close();
        QTRY_VERIFY(result.has_value());
    }

    // AD1-number-commit and AD1-float-step: typing is never blocked; out of
    // range shows in the danger colour and the strip says what it becomes;
    // committing clamps. The arrows, Up / Down, Page Up / Page Down and the
    // wheel step by 1 / 10 steps (an intedit by 1, a floatedit by its step);
    // a floatedit without a step has no arrows and does not step. An
    // emptied field shows the range and keeps its value. An intedit keeps
    // legacy's right-button drag.
    void numbersClampOnCommit()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "intedit", .name = "i", .intValue = 40, .intMin = 10, .intMax = 500},
                            DialogControl{.kind = "floatedit", .name = "f", .y = 1, .number = 1.25, .numberMin = 0.1,
                                          .numberMax = 10, .step = 0.05},
                            DialogControl{.kind = "floatedit", .name = "g", .y = 2, .number = 0.6, .numberMin = 0,
                                          .numberMax = 20},
                            DialogControl{.kind = "intedit", .name = "j", .y = 3, .intValue = 5, .intMin = 0, .intMax = 30}};
        std::optional<DialogResult> result;
        showDialog(request, result);
        QQuickItem *i = controlItem(0);
        i->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_5);
        QCOMPARE(i->property("text").toString(), QStringLiteral("5"));
        QVERIFY(i->property("outOfRange").toBool());
        QTRY_VERIFY(hint().contains(QStringLiteral("10 to 500")));
        QVERIFY2(hint().contains(QStringLiteral("5 becomes 10")), qPrintable(hint()));
        QTest::keyClick(window, Qt::Key_0);
        QVERIFY(!i->property("outOfRange").toBool()); // "50" on the way
        QTest::keyClick(window, Qt::Key_0);
        QTest::keyClick(window, Qt::Key_0);
        QCOMPARE(i->property("text").toString(), QStringLiteral("5000"));
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(i->property("text").toString(), QStringLiteral("500"));
        i->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(i->property("text").toString(), QStringLiteral("499"));
        QTest::keyClick(window, Qt::Key_PageDown);
        QCOMPARE(i->property("text").toString(), QStringLiteral("489"));
        QTest::keyClick(window, Qt::Key_PageUp);
        QTest::keyClick(window, Qt::Key_Up);
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(i->property("text").toString(), QStringLiteral("500"));
        wheel(i, -3);
        QCOMPARE(i->property("text").toString(), QStringLiteral("497"));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Backspace);
        QCOMPARE(i->property("placeholderText").toString(), QStringLiteral("10 – 500"));
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(i->property("text").toString(), QStringLiteral("497")); // emptied: the value stays

        QQuickItem *f = controlItem(1);
        auto *up = qobject_cast<QQuickItem *>(findNamed(f, QStringLiteral("numberUp")));
        QVERIFY(up && up->isVisible());
        f->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(f->property("text").toString(), QStringLiteral("1.3"));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(up));
        QCOMPARE(f->property("text").toString(), QStringLiteral("1.35"));
        QTRY_VERIFY2(hint().contains(QStringLiteral("0.1 to 10, in steps of 0.05")), qPrintable(hint()));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        type(window, QStringLiteral("0,01"));
        QVERIFY(f->property("outOfRange").toBool());
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(f->property("text").toString(), QStringLiteral("0.1"));

        QQuickItem *g = controlItem(2);
        QVERIFY(!qobject_cast<QQuickItem *>(findNamed(g, QStringLiteral("numberUp")))->isVisible());
        g->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Up);
        wheel(g, 2);
        QCOMPARE(g->property("text").toString(), QStringLiteral("0.6"));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        type(window, QStringLiteral("25"));
        QTest::keyClick(window, Qt::Key_Return); // commits, then presses the Enter button
        QTRY_VERIFY(result.has_value());
        QCOMPARE(std::get<int>(result->values[0]), 497);
        QCOMPARE(std::get<double>(result->values[1]), 0.1);
        QCOMPARE(std::get<double>(result->values[2]), 20.0);

        showDialog(request, result);
        QQuickItem *j = controlItem(3);
        const QPoint at = centre(j);
        QTest::mousePress(window, Qt::RightButton, Qt::NoModifier, at);
        QTest::mouseMove(window, at - QPoint(0, 9));
        QCOMPARE(j->property("text").toString(), QStringLiteral("6"));
        QTest::mouseMove(window, at - QPoint(-11, 9));
        QCOMPARE(j->property("text").toString(), QStringLiteral("16"));
        QTest::mouseRelease(window, Qt::RightButton, Qt::NoModifier, at - QPoint(-11, 9));
        controlItem(0)->setProperty("text", QStringLiteral("7")); // typed, never committed
        press(0);
        QVERIFY(result.has_value());
        QCOMPARE(std::get<int>(result->values[0]), 10); // a button commits it, clamped
        QCOMPARE(std::get<int>(result->values[3]), 16);
    }

    // The hint strip shows the hovered control's hint, else the focused
    // one's; the hint is the tooltip and the accessible description too. It
    // keeps one height while focus moves. A dialog with no hint and no
    // range has no strip.
    void hintStripShowsTheHoveredElseTheFocusedHint()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "label", .name = "l", .label = "&Tags"},
                            DialogControl{.kind = "edit", .name = "t", .hint = "The tags to gradient.", .x = 1, .text = ""},
                            DialogControl{.kind = "checkbox", .name = "k", .hint = "Leave the border as it is.", .x = 1,
                                          .y = 1, .label = "&Ignore border"},
                            DialogControl{.kind = "edit", .name = "n", .hint = QString(80, QLatin1Char('w')).toStdString()
                                          + " a long hint that wraps", .x = 1, .y = 2, .text = ""}};
        std::optional<DialogResult> result;
        showDialog(request, result);
        QQuickItem *strip = named(QStringLiteral("automationHints"));
        QVERIFY(strip && strip->isVisible());
        const qreal height = strip->height();
        QCOMPARE(hint(), QString());
        controlItem(1)->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(hint(), QStringLiteral("The tags to gradient."));
        QCOMPARE(accessible(controlItem(1), QAccessible::Description), QStringLiteral("The tags to gradient."));
        QCOMPARE(accessible(controlItem(1), QAccessible::Name), QStringLiteral("Tags"));
        QTest::mouseMove(window, centre(controlItem(2)));
        QTRY_COMPARE(hint(), QStringLiteral("Leave the border as it is."));
        QTest::mouseMove(window, QPoint(2, window->height() - 2));
        QTRY_COMPARE(hint(), QStringLiteral("The tags to gradient."));
        controlItem(3)->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(hint().endsWith(QStringLiteral("wraps")));
        QCOMPARE(strip->height(), height);
        window->close();
        QTRY_VERIFY(result.has_value());

        request.controls = {DialogControl{.kind = "edit", .name = "e", .text = ""}};
        showDialog(request, result);
        QVERIFY(!named(QStringLiteral("automationHints"))->isVisible());
        window->close();
        QTRY_VERIFY(result.has_value());
    }

    // A colour's field takes #RRGGBB, RRGGBB or ASS &HBBGGRR& (and the ASS
    // transparency for coloralpha: #RRGGBBAA, &HAABBGGRR&); the swatch
    // follows as you type; committed, it shows #RRGGBB[AA]; text that is
    // no colour shows in the danger colour and goes back when left. The
    // script gets the legacy hex as before.
    void colourFieldsTakeHtmlAndAss()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "color", .name = "c", .text = "#E83A5F"},
                            DialogControl{.kind = "coloralpha", .name = "a", .y = 1, .text = "#803A7BE8"}};
        std::optional<DialogResult> result;
        showDialog(request, result);
        auto *c = qobject_cast<QQuickItem *>(findNamed(controlItem(0), QStringLiteral("colorText")));
        auto *a = qobject_cast<QQuickItem *>(findNamed(controlItem(1), QStringLiteral("colorText")));
        QCOMPARE(c->property("text").toString(), QStringLiteral("#E83A5F"));
        QCOMPARE(a->property("text").toString(), QStringLiteral("#3A7BE880"));
        c->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        type(window, QStringLiteral("&H5F3AE8&"));
        QCOMPARE(control(0)->property("value").value<QColor>(), QColor(0xE8, 0x3A, 0x5F));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        type(window, QStringLiteral("00ff00"));
        QCOMPARE(control(0)->property("value").value<QColor>(), QColor(0, 255, 0));
        QTest::keyClick(window, Qt::Key_Backspace);
        QVERIFY(c->property("bad").toBool());
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(c->property("text").toString(), QStringLiteral("#00FF00"));
        QVERIFY(!c->property("bad").toBool());
        a->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        type(window, QStringLiteral("&H40FF0000&"));
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(a->property("text").toString(), QStringLiteral("#0000FF40"));
        press(0);
        QVERIFY(result.has_value());
        QCOMPARE(std::get<std::string>(result->values[0]), std::string("#00FF00"));
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("#400000FF"));
    }

    // Above 10 items, typing on a dropdown opens its list with a filter
    // field: the matches keep the script's order, the first is current,
    // Enter picks it and Esc closes the list only. Ten or fewer: no filter.
    // A value that is none of the items shows blank and comes back as given.
    void dropdownsFilterAboveTenItems()
    {
        const std::vector<std::string> masks = {"square", "rounded square", "circle", "triangle 1", "triangle 2",
                                                "fade left", "fade right", "fade up", "fade down", "alignment grid",
                                                "rect. gradient", "ring", "diamond", "star"};
        DialogRequest request;
        request.controls = {DialogControl{.kind = "dropdown", .name = "m", .text = "circle", .items = masks},
                            DialogControl{.kind = "dropdown", .name = "s", .y = 1, .text = "a", .items = {"a", "b"}},
                            DialogControl{.kind = "dropdown", .name = "x", .y = 2, .text = "zzz", .items = {"a", "b"}}};
        std::optional<DialogResult> result;
        showDialog(request, result);
        QQuickItem *m = controlItem(0);
        m->forceActiveFocus(Qt::TabFocusReason);
        auto *popup = m->property("popup").value<QObject *>();
        QTest::keyClick(window, 'f'); // opens the list with "f"
        QTRY_VERIFY(popup->property("opened").toBool());
        // In the dialog, as tall as the room it has (it scrolls).
        auto *listItem = popup->property("contentItem").value<QQuickItem *>();
        QCOMPARE(listItem->window(), static_cast<QQuickWindow *>(window));
        const QRectF at = listItem->mapRectToScene(QRectF(0, 0, listItem->width(), listItem->height()));
        QVERIFY2(at.top() >= 0 && at.bottom() <= window->height(), qPrintable(QString::number(at.bottom())));
        QVERIFY(m->hasActiveFocus()); // the keys stay on the dropdown
        type(window, QStringLiteral("ad"));
        QCOMPARE(m->property("filter").toString(), QStringLiteral("fad"));
        QCOMPARE(m->property("matches").toList().size(), 4);
        QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(m->property("currentText").toString(), QStringLiteral("fade right"));
        QVERIFY(!result.has_value());
        m->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, 's');
        QTRY_VERIFY(popup->property("opened").toBool());
        type(window, QStringLiteral("t"));
        QCOMPARE(m->property("matches").toList().size(), 1);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(window->isVisible()); // the list's Esc
        QVERIFY(!result.has_value());
        QCOMPARE(m->property("currentText").toString(), QStringLiteral("fade right"));

        QQuickItem *s = controlItem(1);
        QVERIFY(!s->property("filters").toBool());
        s->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_B);
        QCOMPARE(s->property("currentText").toString(), QStringLiteral("b"));
        QCOMPARE(controlItem(2)->property("currentIndex").toInt(), -1);
        press(0);
        QVERIFY(result.has_value());
        QCOMPARE(std::get<std::string>(result->values[0]), std::string("fade right"));
        QCOMPARE(std::get<std::string>(result->values[1]), std::string("b"));
        QCOMPARE(std::get<std::string>(result->values[2]), std::string("zzz"));
    }

    // `&` mnemonics: Alt with the letter presses a button, ticks a check box
    // or moves to the control after a label; the letters are underlined
    // while Alt is held; the `&` is never shown and the label returned
    // keeps it; `&&` is one `&`.
    void mnemonicsWork()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "label", .name = "l", .label = "&Tags"},
                            DialogControl{.kind = "edit", .name = "t", .x = 1, .text = ""},
                            DialogControl{.kind = "checkbox", .name = "k", .x = 1, .y = 1, .label = "&Ignore"},
                            DialogControl{.kind = "label", .name = "r", .y = 2, .label = "Fish && chips"}};
        request.buttons = {"&Apply", "&Load preset", "Cancel"};
        request.buttonIds = {"ok", "", "cancel"};
        std::optional<DialogResult> result;
        showDialog(request, result);
        QCOMPARE(controlItem(0)->property("text").toString(), QStringLiteral("Tags"));
        QCOMPARE(controlItem(3)->property("text").toString(), QStringLiteral("Fish &amp; chips"));
        QCOMPARE(named(QStringLiteral("dialogButton0"))->property("text").toString(), QStringLiteral("Apply"));
        QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
        QTRY_COMPARE(controlItem(0)->property("text").toString(), QStringLiteral("<u>T</u>ags"));
        QTest::keyRelease(window, Qt::Key_Alt);
        QTRY_COMPARE(controlItem(0)->property("text").toString(), QStringLiteral("Tags"));
        QTest::keyClick(window, Qt::Key_I, Qt::AltModifier);
        QTRY_VERIFY(controlItem(2)->property("checked").toBool());
        QTest::keyClick(window, Qt::Key_T, Qt::AltModifier);
        QTRY_COMPARE(focusName(), QStringLiteral("control_1"));
        QTest::keyClick(window, Qt::Key_L, Qt::AltModifier);
        QTRY_VERIFY(result.has_value());
        QCOMPARE(result->pressed, 1);
        QCOMPARE(std::get<bool>(result->values[2]), true);
    }

    // The window is resizable: the columns of an edit grow, a label's do
    // not; a textbox's row takes the extra height. Taller than the screen's
    // free area, it is capped: the grid scrolls (the focused control comes
    // into view) and the buttons stay on screen.
    void sizeGrowsTheFieldsAndIsCappedToTheScreen()
    {
        DialogRequest request;
        request.controls = {DialogControl{.kind = "label", .name = "l", .label = "Notes"},
                            DialogControl{.kind = "textbox", .name = "t", .x = 1, .text = "a"},
                            DialogControl{.kind = "label", .name = "m", .y = 1, .label = "Tags"},
                            DialogControl{.kind = "edit", .name = "e", .x = 1, .y = 1, .text = ""}};
        std::optional<DialogResult> result;
        showDialog(request, result);
        QVERIFY(window->maximumWidth() > window->width());
        const qreal label = controlItem(0)->width(), edit = controlItem(3)->width(), box = controlItem(1)->height();
        window->resize(window->width() + 200, window->height() + 150);
        QTRY_COMPARE(controlItem(3)->width(), edit + 200);
        QCOMPARE(controlItem(0)->width(), label);
        QTRY_VERIFY(controlItem(1)->height() > box + 100);
        window->close();
        QTRY_VERIFY(result.has_value());

        DialogRequest tall;
        for (int y = 0; y < 80; ++y)
            tall.controls.push_back(DialogControl{.kind = "edit", .name = "e" + std::to_string(y), .y = y, .text = ""});
        showDialog(tall, result);
        const QRect room = window->screen()->availableGeometry();
        QVERIFY2(window->height() <= room.height(), qPrintable(QString::number(window->height())));
        QQuickItem *scroll = named(QStringLiteral("automationDialogScroll"));
        QVERIFY(scroll->property("overflows").toBool());
        QQuickItem *button = named(QStringLiteral("dialogButton0"));
        QVERIFY(button->mapToScene(QPointF(0, button->height())).y() <= window->height());
        controlItem(79)->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(scroll->property("contentY").toReal() > 0);
        const QPointF at = controlItem(79)->mapToItem(scroll, QPointF(0, 0));
        QVERIFY(at.y() >= 0 && at.y() + controlItem(79)->height() <= scroll->height());
        window->close();
        QTRY_VERIFY(result.has_value());
    }

    // The size and place are kept per script file, macro and set of
    // controls (their classes and names).
    void sizeIsKeptPerScriptAndDialog()
    {
        DialogRequest a;
        a.controls = {DialogControl{.kind = "edit", .name = "git_path", .text = ""}};
        DialogRequest b = a;
        b.controls[0].name = "other";
        auto idOf = [&](const DialogRequest &r, const QString &source) {
            controller->present(QStringLiteral("t"), r, [](DialogResult) {}, source);
            const QString id = controller->dialogId();
            controller->withdraw();
            return id;
        };
        const QString one = idOf(a, QStringLiteral("/s/gitsigns.moon\nConfigure"));
        QVERIFY(one.startsWith(QStringLiteral("automation.")));
        QVERIFY(!one.contains(QLatin1Char('\t')) && !one.contains(QLatin1Char('\n')));
        QCOMPARE(idOf(a, QStringLiteral("/s/gitsigns.moon\nConfigure")), one);
        QVERIFY(idOf(b, QStringLiteral("/s/gitsigns.moon\nConfigure")) != one);
        QVERIFY(idOf(a, QStringLiteral("/s/gitsigns.moon\nShow diff")) != one);
        QVERIFY(idOf(a, QString()).isEmpty());
        controller->present(QStringLiteral("t"), a, [](DialogResult) {}, QStringLiteral("/s/x.lua\nm"));
        QTRY_COMPARE(window->property("dialogId").toString(), controller->dialogId());
        controller->withdraw();
        QTRY_VERIFY(!window->isVisible());
    }

    // The dialog in the four themes, for review: written to
    // HIKARI_SURFACE_SHOT_DIR when it is set (script-dialog-<part>-<theme>.png).
    void screenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        hikari::ui::SettingsStore store;
        hikari::ui::theme::useSettings(&store);
        window->setProperty("settings", QVariant::fromValue(&store));
        auto restore = qScopeGuard([this] {
            window->setProperty("settings", QVariant());
            hikari::ui::theme::endPreview();
            hikari::ui::theme::useSettings(nullptr);
        });

        DialogRequest gradient; // a typesetting script's dialog
        gradient.controls = {
            DialogControl{.kind = "label", .name = "l0", .label = "&Tags"},
            DialogControl{.kind = "edit", .name = "tags", .hint = "Override tags to gradient, e.g. \\1c\\3c.", .x = 1,
                          .width = 3, .text = "\\1c\\3c"},
            DialogControl{.kind = "label", .name = "l1", .y = 1, .label = "&Start"},
            DialogControl{.kind = "color", .name = "c1", .hint = "Colour at the start.", .x = 1, .y = 1, .text = "#E83A5F"},
            DialogControl{.kind = "label", .name = "l2", .x = 2, .y = 1, .label = "&End"},
            DialogControl{.kind = "coloralpha", .name = "c2", .hint = "Colour and transparency at the end.", .x = 3,
                          .y = 1, .text = "#803A7BE8"},
            DialogControl{.kind = "label", .name = "l3", .y = 2, .label = "Strip &width"},
            DialogControl{.kind = "intedit", .name = "w", .hint = "Pixels per strip.", .x = 1, .y = 2, .intValue = 2,
                          .intMin = 1, .intMax = 50},
            DialogControl{.kind = "label", .name = "l4", .x = 2, .y = 2, .label = "Acce&l"},
            DialogControl{.kind = "floatedit", .name = "a", .hint = "Acceleration of the gradient.", .x = 3, .y = 2,
                          .number = 1.25, .numberMin = 0.1, .numberMax = 10, .step = 0.05},
            DialogControl{.kind = "label", .name = "l5", .y = 3, .label = "&Mode"},
            DialogControl{.kind = "dropdown", .name = "m", .hint = "How the strips are cut.", .x = 1, .y = 3, .width = 3,
                          .text = "Horizontal", .items = {"Horizontal", "Vertical", "Diagonal"}},
            DialogControl{.kind = "checkbox", .name = "b", .hint = "Leave the border as it is.", .x = 1, .y = 4,
                          .width = 3, .label = "&Ignore border"},
        };
        gradient.buttons = {"&Apply", "&Load preset", "Cancel"};
        gradient.buttonIds = {"ok", "", "cancel"};

        DialogRequest numbers;
        numbers.controls = {DialogControl{.kind = "label", .name = "l", .label = "Interval (seconds)"},
                            DialogControl{.kind = "intedit", .name = "i", .x = 1, .intValue = 40, .intMin = 10,
                                          .intMax = 500},
                            DialogControl{.kind = "label", .name = "k", .y = 1, .label = "Opacity (%)"},
                            DialogControl{.kind = "floatedit", .name = "f", .x = 1, .y = 1, .number = 50,
                                          .numberMin = 0, .numberMax = 100, .step = 1}};
        numbers.buttons = {"Go", "Cancel"};
        numbers.buttonIds = {"ok", "cancel"};

        DialogRequest tall; // a HYDRA-sized dialog on a 1280 x 800 screen
        for (int y = 0; y < 40; ++y) {
            tall.controls.push_back(DialogControl{.kind = "checkbox", .name = "t" + std::to_string(y), .y = y,
                                                  .label = "\\tag" + std::to_string(y)});
            tall.controls.push_back(DialogControl{.kind = "edit", .name = "v" + std::to_string(y), .x = 1, .y = y,
                                                  .text = std::to_string(y * 3)});
        }
        tall.buttons = {"Apply", "Cancel"};
        tall.buttonIds = {"ok", "cancel"};

        DialogRequest masks;
        std::vector<std::string> items = {"square", "rounded square", "circle", "triangle 1", "triangle 2", "fade left",
                                          "fade right", "fade up", "fade down", "alignment grid", "ring", "star"};
        masks.controls = {DialogControl{.kind = "label", .name = "l", .label = "Mask"},
                          DialogControl{.kind = "dropdown", .name = "m", .x = 1, .text = "circle", .items = items}};
        const char *rows[] = {"Size", "Rotation", "Blur", "Border", "Layer", "Offset x", "Offset y"};
        for (int y = 0; y < 7; ++y) {
            masks.controls.push_back(DialogControl{.kind = "label", .name = "r" + std::to_string(y), .y = y + 1,
                                                   .label = rows[y]});
            masks.controls.push_back(DialogControl{.kind = "intedit", .name = "v" + std::to_string(y), .x = 1,
                                                   .y = y + 1, .intValue = 0, .intMin = 0, .intMax = 100});
        }
        masks.buttons = {"Draw", "Cancel"};
        masks.buttonIds = {"ok", "cancel"};

        for (const auto code : hikari::ui::theme::kCodes) {
            const QString theme = hikari::ui::theme::codeName(code);
            hikari::ui::theme::preview(
                {{QStringLiteral("appearance.theme"), theme}, {QStringLiteral("appearance.followSystem"), false}});
            const auto save = [&](const QString &part) {
                QTest::qWait(250);
                QVERIFY(window->grabWindow().save(out + QStringLiteral("/script-dialog-") + part + QLatin1Char('-') +
                                                  theme + QStringLiteral(".png")));
            };
            std::optional<DialogResult> result;
            showDialog(gradient, result);
            controlItem(1)->forceActiveFocus(Qt::TabFocusReason);
            save(QStringLiteral("layout"));
            QTest::keyPress(window, Qt::Key_Alt, Qt::AltModifier);
            save(QStringLiteral("mnemonics"));
            QTest::keyRelease(window, Qt::Key_Alt);
            window->close();
            QTRY_VERIFY(result.has_value());

            showDialog(numbers, result);
            controlItem(1)->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            type(window, QStringLiteral("5000"));
            save(QStringLiteral("number-out-of-range"));
            window->close();
            QTRY_VERIFY(result.has_value());

            showDialog(tall, result);
            controlItem(41)->forceActiveFocus(Qt::TabFocusReason);
            save(QStringLiteral("scrolling"));
            window->close();
            QTRY_VERIFY(result.has_value());

            showDialog(masks, result);
            controlItem(1)->forceActiveFocus(Qt::TabFocusReason);
            auto *list = controlItem(1)->property("popup").value<QObject *>();
            QTest::keyClick(window, 'f');
            QTRY_VERIFY(list->property("opened").toBool());
            type(window, QStringLiteral("a"));
            save(QStringLiteral("dropdown-filter"));
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!list->property("visible").toBool());
            window->close();
            QTRY_VERIFY(result.has_value());
        }
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
        press(1); // "Stop", the cancel id: false, as in Aegisub (AD1-button-ids)

        QTRY_VERIFY(outcome.has_value());
        QCOMPARE(*outcome, LuaScriptHost::RunOutcome::Ok);
        QCOMPARE(log, QStringList{QStringLiteral(
                          "button=boolean:false 7=number:99 f=number:2.5 flag=boolean:false pick=string:a "
                          "text=string:typed")});
    }

    // AD1-button-ids: dialogs from real scripts (lua-fixtures/
    // script-dialogs.lua) answered with the keyboard through this window
    // return what Aegisub returns: Return presses the ok id's button, a
    // cancel-id button and Esc give false.
    void realScriptDialogsAnswerAsInAegisub()
    {
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_FIXTURES "/script-dialogs.lua"),
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
        const auto run = [&](int macro, const QString &enter, const std::function<void()> &answer) {
            log.clear();
            outcome.reset();
            QVERIFY(host.run(macro));
            QTRY_VERIFY(window->isVisible());
            window->requestActivate();
            QTRY_VERIFY(QGuiApplication::focusWindow() == window);
            QTRY_COMPARE(focusName(), enter);
            answer();
            QTRY_VERIFY(outcome.has_value());
            QCOMPARE(*outcome, LuaScriptHost::RunOutcome::Ok);
            QTRY_VERIFY(!window->isVisible());
        };
        run(0, QStringLiteral("dialogButton0"), [&] { // GitSigns Configure: Cancel
            QTest::keyClick(window, Qt::Key_Tab);
            QCOMPARE(focusName(), QStringLiteral("dialogButton1"));
            QTest::keyClick(window, Qt::Key_Space);
        });
        QCOMPARE(log, QStringList{QStringLiteral("not saved button=boolean:false git_path=string:git")});
        run(0, QStringLiteral("dialogButton0"), [&] { // typed, then Return in the field saves
            controlItem(1)->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(window, Qt::Key_End);
            type(window, QStringLiteral("-2"));
            QTest::keyClick(window, Qt::Key_Return);
        });
        QCOMPARE(log, QStringList{QStringLiteral("saved button=string:Save git_path=string:git-2")});
        run(1, QStringLiteral("dialogButton0"), [&] { // SemiColorCalc: Esc
            QTest::keyClick(window, Qt::Key_Escape);
        });
        QCOMPARE(log.value(0).section(QLatin1Char(' '), 0, 1), QStringLiteral("calculated button=boolean:false"));
        run(4, QStringLiteral("dialogButton1"), [&] { // Toolbox feed trust: Return
            QTest::keyClick(window, Qt::Key_Return);
        });
        QCOMPARE(log, QStringList{QStringLiteral("feed button=string:Fetch once")});
        run(2, QStringLiteral("dialogButton0"), [&] { // Toolbox install: no buttons, OK
            QCOMPARE(controller->buttons(), (QStringList{QStringLiteral("OK"), QStringLiteral("Cancel")}));
            QTest::keyClick(window, Qt::Key_Return);
        });
        QCOMPARE(log, QStringList{QStringLiteral("install button=string: macro=string: module=string:")});
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
    // on all of them, so what is asserted comes from the legacy sources and
    // AD1: the Enter button has the focus when the dialog shows; Return
    // presses it, Escape ends the dialog with no button (false); Up on a
    // button does nothing (AD1-keyboard: legacy moved the focus to the field
    // above, where Return did nothing), so float-step-up and int-step-up
    // answer unchanged (f=1, i=1); typed text reaches no field (e=""); an id
    // naming no button is an error before any dialog shows, as in Aegisub
    // (AD1-button-ids).
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
        for (const char *name : {"defaults-ok", "buttons-return", "edit-typed", "defaults-escape", "buttons-escape",
                                 "button-id-unknown", "float-step-up", "int-step-up"})
            QVERIFY2(!byName.value(QLatin1String(name)).contains(QStringLiteral("dialog_open_after_keys")), name);
        const QJsonObject invalid = byName.value(QStringLiteral("button-id-invalid"));
        QVERIFY(!invalid.value(QStringLiteral("dialog_shown")).toBool());
        QCOMPARE(invalid.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool(true), false);
        QVERIFY2(invalid.value(QStringLiteral("result")).toObject().value(QStringLiteral("error")).toString().contains(
                     QStringLiteral("Invalid button for id ok")),
                 qPrintable(QJsonDocument(invalid).toJson()));
        const auto button = [&](const char *name) {
            return byName.value(QLatin1String(name)).value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("button")).toObject();
        };
        QCOMPARE(button("defaults-ok").value(QStringLiteral("value")).toString(), QString());
        QCOMPARE(button("defaults-escape").value(QStringLiteral("type")).toString(), QStringLiteral("boolean"));
        QCOMPARE(button("buttons-return").value(QStringLiteral("value")).toString(), QStringLiteral("Apply"));
        QCOMPARE(button("button-id-unknown").value(QStringLiteral("value")).toString(), QStringLiteral("A"));
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
