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
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
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

    // S3/L2: the legacy capture probe's dialog cases (tools/legacy-capture
    // plan.json "automation") run through this dialog with the plan's keys,
    // as drive.py/drive_windows.py send them to the legacy app. Each case's
    // JSON line is the rewrite side of the legacy comparison
    // (artifacts/automation-capture-dialogs.json); only that every case
    // answers is asserted here (the legacy baseline hangs on all of them).
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
                QQuickItem *focus = window->activeFocusItem();
                record.insert(QStringLiteral("focus"), focus ? QString::fromLatin1(focus->metaObject()->className())
                                                             : QString());
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
            if (!QTest::qWaitFor([&] { return outcome.has_value(); }, 5000)) {
                // No keys answer it (the plan sends none): withdraw it, as a
                // user cancelling the run would.
                record.insert(QStringLiteral("dialog_open_after_keys"), window->isVisible());
                host.cancel();
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
        for (const auto &c : cases)
            QVERIFY2(c.toObject().value(QStringLiteral("status")).toString() == QStringLiteral("captured") ||
                         c.toObject().contains(QStringLiteral("dialog_open_after_keys")),
                     qPrintable(c.toObject().value(QStringLiteral("case")).toString()));
    }
};

QTEST_MAIN(AutomationDialogTests)
#include "automation_dialog_tests.moc"
