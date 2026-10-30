#include "log_controller.h"
#include "settings_store.h"
#include "theme.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyleHints>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>
#include <QtTest>

#include <algorithm>
#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

const QByteArray kQml = R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui
Window {
    id: root
    width: 800; height: 600
    color: Theme.panel
    property var log: []
    property var settings: null
    property var logger: null
    function note(entry) { log = log.concat([entry]) }

    Button { id: anchorButton; objectName: "anchorButton"; x: 20; y: 20; text: "Open"; focus: true }
    Button { id: elsewhere; objectName: "elsewhere"; x: 600; y: 500; text: "Elsewhere" }

    MessageBox { id: box; settings: root.settings }
    MessageHost { id: host; settings: root.settings }

    Popover {
        id: popover
        objectName: "popover"
        title: "Test popover"
        onKept: root.note("kept")
        onReverted: root.note("reverted")
        TextField { objectName: "popoverField"; Layout.preferredWidth: 160 }
        ComboBox { objectName: "popoverChoice"; model: ["One", "Two", "Three"] }
    }

    SpinBox { objectName: "spin"; x: 300; y: 400 }
    TextField { objectName: "plainField"; x: 300; y: 450; width: 120 }
    DockMenuButton { objectName: "tipButton"; x: 500; y: 400; panelTitle: "Styles"; hoverEnabled: true }
    Button { id: toggle; objectName: "toggleButton"; x: 600; y: 300; text: "Toggle"; onClicked: popover.openAt(toggle) }

    ToastStack {
        id: toasts
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        log: root.logger
        settings: root.settings
        openLog: id => root.note("log " + id)
    }

    InlineBar {
        id: bar
        objectName: "bar"
        y: 60
        width: 400
        text: "An update is ready"
        onClosed: root.note("closed")
        Button { objectName: "barAction"; text: "Restart" }
    }

    ClashReview { id: clashes; settings: root.settings }

    ChooseStyles {
        id: styles
        onChanged: names => root.note("changed:" + names.join(","))
        onActed: names => root.note("acted:" + names.join(","))
    }

    HotkeyCapture {
        id: capture
        settings: root.settings
        // As hotkeys.capture reads keys: F1 refused, a modifier alone
        // waits, anything else taken.
        hotkeys: ({
            capture: (key, modifiers, window) => {
                if (key === Qt.Key_F1)
                    return { result: "refused", message: "F1 is Help" }
                if (key === Qt.Key_Control || key === Qt.Key_Shift)
                    return { result: "waiting" }
                if (key === Qt.Key_Tab || key === Qt.Key_Backtab)
                    return { result: "accepted", accel: "Tab", keys: "Tab" }
                return { result: "accepted", accel: "Ctrl-K", keys: "Ctrl+K" }
            }
        })
    }

    ListChooser { id: chooser; settings: root.settings }

    DatePicker { id: date; objectName: "date"; x: 20; y: 300; accessibleName: "Older than" }
}
)";

QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = findItem(child, name))
            return found;
    return nullptr;
}

QQuickItem *findIn(QQuickWindow *window, const char *name)
{
    return window ? findItem(window->contentItem(), QLatin1String(name)) : nullptr;
}

// QTest::keyClicks takes a QWidget only.
void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text)
        QTest::keyClick(window, c.toLatin1());
}

QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

// Qt Quick layouts move items only at the next polish.
void click(QQuickItem *item)
{
    QVERIFY(QQuickTest::qWaitForPolish(item->window()));
    QTest::mouseClick(item->window(), Qt::LeftButton, {}, centreOf(item));
}

bool anyMenuOpen(QObject *root)
{
    const QList<QObject *> all = root->findChildren<QObject *>();
    return std::ranges::any_of(all, [](QObject *o) { return o->inherits("QQuickMenu") && o->property("visible").toBool(); });
}

QString shortDate(const QDate &d)
{
    QString f = QLocale().dateFormat(QLocale::ShortFormat);
    if (!f.contains(QLatin1String("yyyy")))
        f.replace(QLatin1String("yy"), QLatin1String("yyyy"));
    return QLocale().toString(d, f);
}

QVariant plain(const QVariant &v)
{
    return v.canConvert<QJSValue>() && v.metaType() == QMetaType::fromType<QJSValue>()
               ? v.value<QJSValue>().toVariant()
               : v;
}

}

class PopupKitTests : public QObject {
    Q_OBJECT

    std::unique_ptr<ui::SettingsStore> store;
    std::unique_ptr<ui::LogController> logger;
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QQuickWindow> root;

    QVariant run(const QString &code)
    {
        QQmlExpression expression(qmlContext(root.get()), root.get(), code);
        const QVariant result = expression.evaluate();
        if (expression.hasError())
            qWarning() << code << expression.error();
        return plain(result);
    }
    QStringList notes() { return run(QStringLiteral("root.log")).toStringList(); }
    QQuickWindow *windowOf(const char *id) { return run(QLatin1String(id)).value<QQuickWindow *>(); }

private slots:
    void init()
    {
        store = std::make_unique<ui::SettingsStore>();
        logger = std::make_unique<ui::LogController>();
        engine = std::make_unique<QQmlEngine>();
        QQmlComponent component(engine.get());
        component.setData(kQml, QUrl(QStringLiteral("qrc:/pk1-test.qml")));
        if (!component.isReady())
            qWarning() << component.errors();
        root.reset(qobject_cast<QQuickWindow *>(component.create()));
        QVERIFY(root);
        root->setProperty("settings", QVariant::fromValue(store.get()));
        root->setProperty("logger", QVariant::fromValue(logger.get()));
        root->show();
        root->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(root.get()));
    }
    void cleanup()
    {
        root.reset();
        engine.reset();
        logger.reset();
        store.reset();
    }

    void messageBoxNamesItsButtonsAndNeverDefaultsToDestruction()
    {
        run(QStringLiteral(
            "box.ask({ title: 'Delete styles', text: 'Delete 3 styles?', detail: 'Lines keep their names.',"
            " severity: 'warning', dontAsk: true, details: 'trace line',"
            " choices: [{ id: 'delete', text: 'Delete', role: 'destructive', isDefault: true },"
            "           { id: 'keep', text: 'Keep them', role: 'reject' }] },"
            " (id, again) => root.note(id + ':' + again))"));
        QQuickWindow *box = windowOf("box");
        QVERIFY(QTest::qWaitForWindowFocused(box));
        QCOMPARE(box->title(), QStringLiteral("Delete styles"));
        QCOMPARE(findIn(box, "messageBoxIcon")->property("iconRole").toString(), QStringLiteral("notice-warning"));
        QQuickItem *destroy = findIn(box, "messageBoxChoice_delete");
        QQuickItem *keep = findIn(box, "messageBoxChoice_keep");
        QVERIFY(destroy && keep);
        QVERIFY(QString::fromLatin1(destroy->metaObject()->className()).startsWith(QLatin1String("DangerButton")));
        QCOMPARE(box->property("defaultButton").value<QQuickItem *>(), keep);
        QVERIFY(!findIn(box, "messageBoxDetails")->isVisible());
        click(findIn(box, "messageBoxDisclosure"));
        QTRY_VERIFY(findIn(box, "messageBoxDetails")->isVisible());
        click(findIn(box, "messageBoxDontAsk"));
        QTest::keyClick(box, Qt::Key_Return);
        QTRY_VERIFY(!box->isVisible());
        QCOMPARE(notes(), QStringList{QStringLiteral("keep:true")});

        run(QStringLiteral("box.ask({ title: 'Export', text: 'Export failed', severity: 'error',"
                           " choices: [{ id: 'retry', text: 'Try again', role: 'accept', isDefault: true },"
                           "           { id: 'stop', text: 'Stop', role: 'reject' }] },"
                           " (id, again) => root.note(id + ':' + again))"));
        QVERIFY(QTest::qWaitForWindowFocused(box));
        QCOMPARE(findIn(box, "messageBoxIcon")->property("iconRole").toString(), QStringLiteral("notice-error"));
        QVERIFY(!findIn(box, "messageBoxChoice_delete"));
        QVERIFY(!findIn(box, "messageBoxDontAsk")->isVisible());
        QTest::keyClick(box, Qt::Key_Escape);
        QTRY_VERIFY(!box->isVisible());
        QCOMPARE(notes().last(), QStringLiteral("stop:false"));
        run(QStringLiteral("box.ask({ title: 'Saved', text: 'Saved' }, (id) => root.note('ok:' + id))"));
        QVERIFY(QTest::qWaitForWindowFocused(box));
        QTest::keyClick(box, Qt::Key_Return);
        QTRY_COMPARE(notes().last(), QStringLiteral("ok:ok"));

        run(QStringLiteral("box.ask({ title: 'Crash', text: 'It stopped', severity: 'error',"
                           " details: Array.from({ length: 300 }, (_, i) => 'frame ' + i).join('\\n') }, null)"));
        QVERIFY(QTest::qWaitForWindowFocused(box));
        click(findIn(box, "messageBoxDisclosure"));
        QTRY_VERIFY(findIn(box, "messageBoxDetails")->isVisible());
        QVERIFY(QQuickTest::qWaitForPolish(box));
        QVERIFY(findIn(box, "messageBoxDetailsScroll")->height() <= 220);
        QQuickItem *ok = box->property("defaultButton").value<QQuickItem *>();
        QVERIFY(ok);
        QVERIFY(ok->mapToScene(QPointF(0, ok->height())).y() <= box->height());
        QTest::keyClick(box, Qt::Key_Escape);
        QTRY_VERIFY(!box->isVisible());
    }

    void messageHostAsksOneAtATime()
    {
        run(QStringLiteral(
            "host.ask({ title: 'A', text: 'first' }, id => { root.note('A'); host.ask({ title: 'C', text: 'third' }, () => root.note('C')) });"
            "host.ask({ title: 'B', text: 'second' }, () => root.note('B'))"));
        QQuickWindow *box = windowOf("host.box");
        QVERIFY(box);
        for (const QString title : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
            QTRY_VERIFY(box->isVisible());
            QVERIFY(QTest::qWaitForWindowFocused(box));
            QCOMPARE(box->title(), title);
            QTest::qWait(50);
            QCOMPARE(box->title(), title);
            QTest::keyClick(box, Qt::Key_Return);
            QTRY_COMPARE(notes().size(), title == QLatin1String("A") ? 1 : title == QLatin1String("B") ? 2 : 3);
        }
        QCOMPARE(notes(), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
    }

    void popoverKeepsOnEnterAndOutsideAndRevertsOnEsc()
    {
        QQuickItem *anchor = findIn(root.get(), "anchorButton");
        run(QStringLiteral("popover.openAt(anchorButton)"));
        QTRY_VERIFY(run(QStringLiteral("popover.opened")).toBool());
        const QPointF at = run(QStringLiteral("popover.contentItem.mapToItem(null, 0, 0)")).toPointF();
        QVERIFY(at.y() > anchor->mapToScene(QPointF(0, anchor->height())).y());
        QTest::keyClick(root.get(), Qt::Key_Return);
        QTRY_VERIFY(!run(QStringLiteral("popover.visible")).toBool());
        QCOMPARE(notes(), QStringList{QStringLiteral("kept")});
        QTRY_VERIFY(anchor->hasActiveFocus());

        run(QStringLiteral("popover.openAt(anchorButton)"));
        QTRY_VERIFY(run(QStringLiteral("popover.opened")).toBool());
        QTest::keyClick(root.get(), Qt::Key_Escape);
        QTRY_VERIFY(!run(QStringLiteral("popover.visible")).toBool());
        QCOMPARE(notes().last(), QStringLiteral("reverted"));

        run(QStringLiteral("popover.openAt(anchorButton)"));
        QTRY_VERIFY(run(QStringLiteral("popover.opened")).toBool());
        QTest::mouseClick(root.get(), Qt::LeftButton, {}, QPoint(700, 300));
        QTRY_VERIFY(!run(QStringLiteral("popover.visible")).toBool());
        QCOMPARE(notes().last(), QStringLiteral("kept"));
        QCOMPARE(notes().size(), 3);

        run(QStringLiteral("popover.openAt(anchorButton)"));
        run(QStringLiteral("popover.openAt(elsewhere)"));
        QCOMPARE(notes().last(), QStringLiteral("kept"));
        QCOMPARE(notes().size(), 4);
        run(QStringLiteral("popover.revert()"));
        run(QStringLiteral("popover.openAt(null, root.contentItem)"));
        QTRY_VERIFY(run(QStringLiteral("popover.opened")).toBool());
        const double x = run(QStringLiteral("popover.x")).toDouble();
        const double width = run(QStringLiteral("popover.implicitWidth")).toDouble();
        QVERIFY(qAbs(x + width / 2 - root->width() / 2.0) <= 1);
        run(QStringLiteral("popover.keep()"));
    }

    void popoverClosesWithItsOpenListOnAClickOutsideBoth()
    {
        run(QStringLiteral("popover.openAt(anchorButton)"));
        QTRY_VERIFY(run(QStringLiteral("popover.opened")).toBool());
        QQuickItem *choice = findIn(root.get(), "popoverChoice");
        QVERIFY(choice);
        click(choice);
        QTRY_VERIFY(choice->property("popup").value<QObject *>()->property("opened").toBool());
        QTest::mouseClick(root.get(), Qt::LeftButton, {}, QPoint(700, 300));
        QTRY_VERIFY(!choice->property("popup").value<QObject *>()->property("visible").toBool());
        QTRY_VERIFY(!run(QStringLiteral("popover.visible")).toBool());
        QCOMPARE(notes(), QStringList{QStringLiteral("kept")});
    }

    void clickingTheAnchorAgainClosesThePopover()
    {
        QQuickItem *toggle = findIn(root.get(), "toggleButton");
        click(toggle);
        QTRY_VERIFY(run(QStringLiteral("popover.opened")).toBool());
        click(toggle);
        QTRY_VERIFY(!run(QStringLiteral("popover.visible")).toBool());
        QTest::qWait(100);
        QVERIFY(!run(QStringLiteral("popover.visible")).toBool());
    }

    void clickingTheAnchorOfAWindowedPopoverClosesIt()
    {
        root->resize(150, 90);
        QTRY_COMPARE(root->height(), 90);
        run(QStringLiteral("toggle.x = 10; toggle.y = 10"));
        QQuickItem *toggle = findIn(root.get(), "toggleButton");
        click(toggle);
        QTRY_VERIFY(run(QStringLiteral("popover.opened")).toBool());
        QVERIFY(run(QStringLiteral("popover.popupType === Popup.Window")).toBool());
        click(toggle);
        QTRY_VERIFY(!run(QStringLiteral("popover.visible")).toBool());
        QTest::qWait(300);
        QVERIFY(!run(QStringLiteral("popover.visible")).toBool());
        QCOMPARE(notes(), QStringList{QStringLiteral("kept")});
    }

    void spinBoxesOpenNoContextMenu()
    {
        QVERIFY(QQuickTest::qWaitForPolish(root.get()));
        QTest::mouseClick(root.get(), Qt::RightButton, {}, centreOf(findIn(root.get(), "plainField")));
        QTRY_VERIFY(anyMenuOpen(root.get()));
        QTest::keyClick(root.get(), Qt::Key_Escape);
        QTRY_VERIFY(!anyMenuOpen(root.get()));
        QQuickItem *spin = findIn(root.get(), "spin");
        QTest::mouseClick(root.get(), Qt::RightButton, {}, centreOf(spin->property("contentItem").value<QQuickItem *>()));
        QCoreApplication::processEvents();
        QVERIFY(!anyMenuOpen(root.get()));
    }

    void toolTipsTakeThePlatformTiming()
    {
        QQuickItem *button = findIn(root.get(), "tipButton");
        QVERIFY(button);
        auto *tip = QQmlProperty(button, QStringLiteral("ToolTip.toolTip"), qmlContext(button)).read().value<QObject *>();
        QVERIFY(tip);
        QTest::mouseMove(root.get(), centreOf(button));
        QTRY_VERIFY(tip->property("visible").toBool());
        QCOMPARE(tip->property("delay").toInt(), int(QGuiApplication::styleHints()->toolTipWakeUpDelay().count()));
        QVERIFY(tip->property("timeout").toInt() >= 10000);
    }

    void popoverOpensAboveWhenThereIsNoRoomBelow()
    {
        run(QStringLiteral("date.x = 20; date.y = root.height - date.height - 4"));
        run(QStringLiteral("date.openCalendar()"));
        QTRY_VERIFY(run(QStringLiteral("date.popover.opened")).toBool());
        QVERIFY(run(QStringLiteral("date.popover.y")).toDouble() < 0);
        QTest::keyClick(root.get(), Qt::Key_Escape);
        QTRY_VERIFY(!run(QStringLiteral("date.popover.visible")).toBool());
        root->resize(200, 160);
        QTRY_COMPARE(root->height(), 160);
        run(QStringLiteral("date.x = 0; date.y = 0"));
        run(QStringLiteral("date.openCalendar()"));
        QCOMPARE(run(QStringLiteral("date.popover.popupType === Popup.Window")).toBool(), true);
        run(QStringLiteral("date.popover.revert()"));
    }

    void toastsStackThreeAndFoldTheRest()
    {
        const auto post = [this](const char *severity, const char *title) {
            return run(QStringLiteral("toasts.post('%1', '%2', 'text of %2')")
                           .arg(QLatin1String(severity), QLatin1String(title)))
                .toInt();
        };
        run(QStringLiteral("toasts.fadeAfter = 60000"));
        const int first = post("info", "One");
        post("warning", "Two");
        post("info", "Three");
        QCOMPARE(run(QStringLiteral("toasts.entries.length")).toInt(), 3);
        post("warning", "Four");
        QCOMPARE(run(QStringLiteral("toasts.entries.map(e => e.title).join(',')")).toString(),
                 QStringLiteral("Four,Three,Two"));
        QVERIFY(!findIn(root.get(), QStringLiteral("toast_%1").arg(first).toLatin1().constData()));
        post("error", "Five");
        QCOMPARE(run(QStringLiteral("toasts.entries.map(e => e.title).join(',')")).toString(),
                 QStringLiteral("Five,Four,Two"));
        post("warning", "Six");
        QCOMPARE(run(QStringLiteral("toasts.entries.map(e => e.title).join(',')")).toString(),
                 QStringLiteral("Six,Five,Four"));
        QCOMPARE(run(QStringLiteral("toasts.folded")).toInt(), 1);
        QTRY_VERIFY(findIn(root.get(), "toastMore")->isVisible());
        click(findIn(root.get(), "toastMoreOpenLog"));
        QCOMPARE(notes(), QStringList{QStringLiteral("log 0")});
        QCOMPARE(run(QStringLiteral("toasts.folded")).toInt(), 0);
        QVERIFY(logger->history().contains(QLatin1String("One: text of One")));
        QCOMPARE(logger->warningCount(), 3);
        QCOMPARE(logger->errorCount(), 1);
        const int six = run(QStringLiteral("toasts.entries[0].id")).toInt();
        QQuickItem *toast = findIn(root.get(), QStringLiteral("toast_%1").arg(six).toLatin1().constData());
        QVERIFY(toast);
        click(findItem(toast, QStringLiteral("toastDismiss")));
        QCOMPARE(run(QStringLiteral("toasts.entries.length")).toInt(), 2);
        run(QStringLiteral("toasts.clear()"));
        for (const char *title : {"W1", "W2", "W3", "W4"})
            post("warning", title);
        QCOMPARE(run(QStringLiteral("toasts.folded")).toInt(), 1);
        run(QStringLiteral("toasts.entries.map(e => e.id).forEach(id => toasts.dismiss(id))"));
        QCOMPARE(run(QStringLiteral("toasts.entries.length")).toInt(), 0);
        QCOMPARE(run(QStringLiteral("toasts.folded")).toInt(), 1);
        QTRY_VERIFY(findIn(root.get(), "toastMore")->isVisible());
        run(QStringLiteral("toasts.clear()"));
        logger->notice(ui::LogController::Warning, QStringLiteral("Audio"), QStringLiteral("from C++"));
        QCOMPARE(run(QStringLiteral("toasts.entries[0].text")).toString(), QStringLiteral("from C++"));
        QCOMPARE(run(QStringLiteral("toasts.entries[0].severity")).toString(), QStringLiteral("warning"));
        const int detailed = run(QStringLiteral("toasts.post('error', 'Failed', 'Could not save', { details: 'disk full' })")).toInt();
        QQuickItem *details = nullptr;
        QTRY_VERIFY((details = findItem(findIn(root.get(), QStringLiteral("toast_%1").arg(detailed).toLatin1().constData()),
                                        QStringLiteral("toastDetails"))));
        click(details);
        QCOMPARE(notes().last(), QStringLiteral("log %1").arg(logger->entries()->rowCount()));
    }

    void informationToastsFadeAndWarningsStay()
    {
        run(QStringLiteral("toasts.fadeAfter = 50"));
        run(QStringLiteral("toasts.post('info', 'Saved', '')"));
        run(QStringLiteral("toasts.post('warning', 'Read-only', '')"));
        QTRY_COMPARE(run(QStringLiteral("toasts.entries.map(e => e.title).join(',')")).toString(),
                     QStringLiteral("Read-only"));
        QTest::qWait(200);
        QCOMPARE(run(QStringLiteral("toasts.entries.length")).toInt(), 1);
        store->setValue(QStringLiteral("notices.toasts"), false);
        QCOMPARE(run(QStringLiteral("toasts.entries.length")).toInt(), 0);
        QCOMPARE(run(QStringLiteral("toasts.post('warning', 'Quiet', 'only logged')")).toInt(), 0);
        QVERIFY(logger->history().contains(QLatin1String("only logged")));
    }

    void inlineBarClosesWhenItCan()
    {
        QQuickItem *bar = findIn(root.get(), "bar");
        QCOMPARE(findItem(bar, QStringLiteral("inlineBarIcon"))->property("iconRole").toString(), QStringLiteral("about"));
        QVERIFY(findItem(bar, QStringLiteral("barAction")));
        run(QStringLiteral("bar.severity = 'error'"));
        QCOMPARE(findItem(bar, QStringLiteral("inlineBarIcon"))->property("iconRole").toString(), QStringLiteral("notice-error"));
        click(findItem(bar, QStringLiteral("inlineBarClose")));
        QCOMPARE(notes(), QStringList{QStringLiteral("closed")});
        run(QStringLiteral("bar.closable = false"));
        QVERIFY(!findItem(bar, QStringLiteral("inlineBarClose"))->isVisible());
    }

    void clashReviewSetsAnActionPerRow()
    {
        run(QStringLiteral(
            "clashes.review({ title: 'Copy styles', text: '2 styles are already in the file',"
            " rows: [{ name: 'Default', difference: 'Font', suggestion: 'Default (2)' },"
            "        { name: 'Sign', difference: 'Colours', suggestion: 'Sign (2)' }], added: 1,"
            " acceptText: n => 'Write ' + n },"
            " results => root.note(results === null ? 'cancel'"
            "     : results.map(r => r.name + '=' + r.action + ':' + r.newName).join(';')))"));
        QQuickWindow *review = windowOf("clashes");
        QVERIFY(QTest::qWaitForWindowExposed(review));
        QCOMPARE(run(QStringLiteral("clashes.written")).toInt(), 3);
        click(findIn(review, "clashReviewAll_skip"));
        QCOMPARE(run(QStringLiteral("clashes.written")).toInt(), 1);
        click(findIn(review, "clashAction1_rename"));
        QTRY_VERIFY(findIn(review, "clashName1")->isVisible());
        QCOMPARE(findIn(review, "clashName1")->property("text").toString(), QStringLiteral("Sign (2)"));
        run(QStringLiteral("clashes.setName(1, ' ')"));
        QVERIFY(!run(QStringLiteral("clashes.complete")).toBool());
        QVERIFY(!findIn(review, "clashReviewApply")->isEnabled());
        run(QStringLiteral("clashes.setName(1, 'Sign alt')"));
        QVERIFY(findIn(review, "clashReviewApply")->isEnabled());
        findIn(review, "clashName1")->forceActiveFocus();
        QTest::keyClick(review, Qt::Key_End);
        typeText(review, QStringLiteral(",2"));
        QCOMPARE(findIn(review, "clashName1")->property("text").toString(), QStringLiteral("Sign alt2"));
        QTest::keyClick(review, Qt::Key_Return);
        QTRY_VERIFY(!review->isVisible());
        QCOMPARE(notes(), QStringList{QStringLiteral("Default=skip:Default (2);Sign=rename:Sign alt2")});
        run(QStringLiteral("clashes.review({ title: 'Copy', rows: [{ name: 'A' }] },"
                           " results => root.note(results === null ? 'cancel' : 'written'))"));
        QVERIFY(QTest::qWaitForWindowFocused(review));
        QTest::keyClick(review, Qt::Key_Escape);
        QTRY_VERIFY(!review->isVisible());
        QCOMPARE(notes().last(), QStringLiteral("cancel"));
        run(QStringLiteral("clashes.review({ title: 'Copy', canRename: false, rows: [{ name: 'A' }] }, null)"));
        QVERIFY(QTest::qWaitForWindowExposed(review));
        QVERIFY(!findIn(review, "clashReviewAll_rename"));
        QVERIFY(findIn(review, "clashReviewAll_skip"));
        run(QStringLiteral("clashes.finish(false)"));
    }

    void chooseStylesAppliesLiveAndRevertsOnEsc()
    {
        run(QStringLiteral("styles.openFor(anchorButton, ['Default', 'Sign', 'Top'], ['Sign'],"
                           " { counts: { Default: 12, Sign: 3 } })"));
        QTRY_VERIFY(run(QStringLiteral("styles.opened")).toBool());
        QQuickItem *count = findIn(root.get(), "chooseStylesCount");
        QCOMPARE(count->property("text").toString(), QStringLiteral("1 of 3 selected"));
        QTRY_VERIFY(findIn(root.get(), "chooseStylesSearch")->hasActiveFocus());
        typeText(root.get(), QStringLiteral("to"));
        QCOMPARE(run(QStringLiteral("styles.shown.join(',')")).toString(), QStringLiteral("Top"));
        click(findIn(root.get(), "chooseStylesAll"));
        QCOMPARE(notes(), QStringList{QStringLiteral("changed:Sign,Top")});
        findIn(root.get(), "chooseStylesSearch")->setProperty("text", QString());
        QTRY_VERIFY(findIn(root.get(), "chooseStylesRow_Default"));
        click(findIn(root.get(), "chooseStylesRow_Default"));
        QCOMPARE(notes().last(), QStringLiteral("changed:Default,Sign,Top"));
        QCOMPARE(count->property("text").toString(), QStringLiteral("3 of 3 selected"));
        QTest::keyClick(root.get(), Qt::Key_Escape);
        QTRY_VERIFY(!run(QStringLiteral("styles.visible")).toBool());
        QCOMPARE(notes().last(), QStringLiteral("changed:Sign"));

        run(QStringLiteral("styles.openFor(anchorButton, ['Default', 'Sign'], ['Sign'])"));
        QTRY_VERIFY(run(QStringLiteral("styles.opened")).toBool());
        QTRY_VERIFY(findIn(root.get(), "chooseStylesSearch")->hasActiveFocus());
        QTest::keyClick(root.get(), Qt::Key_Return);
        QTRY_VERIFY(!run(QStringLiteral("styles.visible")).toBool());
        root->setProperty("log", QVariantList());
        run(QStringLiteral("styles.openFor(anchorButton, ['Default', 'Sign'], [], { actionText: 'Load' })"));
        QTRY_VERIFY(run(QStringLiteral("styles.opened")).toBool());
        QQuickItem *act = findIn(root.get(), "chooseStylesAct");
        QVERIFY(!act->isEnabled());
        click(findIn(root.get(), "chooseStylesCheck_Sign"));
        QVERIFY(notes().isEmpty());
        QVERIFY(act->isEnabled());
        click(act);
        QTRY_VERIFY(!run(QStringLiteral("styles.visible")).toBool());
        QCOMPARE(notes(), QStringList{QStringLiteral("acted:Sign")});
        run(QStringLiteral("styles.openFor(anchorButton, ['Default', 'Sign'], ['Default'], { actionText: 'Load' })"));
        QTRY_VERIFY(run(QStringLiteral("styles.opened")).toBool());
        QTRY_VERIFY(findIn(root.get(), "chooseStylesSearch")->hasActiveFocus());
        QTest::keyClick(root.get(), Qt::Key_Return);
        QTRY_VERIFY(!run(QStringLiteral("styles.visible")).toBool());
        QCOMPARE(notes().last(), QStringLiteral("acted:Default"));
        run(QStringLiteral("styles.openFor(anchorButton, ['Default', 'Sign'], [], { actionText: 'Load' })"));
        QTRY_VERIFY(run(QStringLiteral("styles.opened")).toBool());
        QTRY_VERIFY(findIn(root.get(), "chooseStylesSearch")->hasActiveFocus());
        QTest::keyClick(root.get(), Qt::Key_Down);
        QTest::keyClick(root.get(), Qt::Key_Space);
        QTest::keyClick(root.get(), Qt::Key_Return);
        QTRY_VERIFY(!run(QStringLiteral("styles.visible")).toBool());
        QCOMPARE(notes().last(), QStringLiteral("acted:Default"));
        run(QStringLiteral("styles.openFor(anchorButton, Array.from({ length: 50000 }, (_, i) => 'S' + i),"
                           " Array.from({ length: 50000 }, (_, i) => 'S' + i))"));
        QTRY_VERIFY(run(QStringLiteral("styles.opened")).toBool());
        QElapsedTimer many;
        many.start();
        click(findIn(root.get(), "chooseStylesNone"));
        QCOMPARE(run(QStringLiteral("styles.checkedNames.length")).toInt(), 0);
        click(findIn(root.get(), "chooseStylesAll"));
        QCOMPARE(run(QStringLiteral("styles.checkedNames.length")).toInt(), 50000);
        QVERIFY2(many.elapsed() < 5000, QByteArray::number(many.elapsed()).constData());
        run(QStringLiteral("styles.revert()"));
        QTRY_VERIFY(!run(QStringLiteral("styles.visible")).toBool());
    }

    void hotkeyCaptureSaysWhyAndAsksOnTheForm()
    {
        QCOMPARE(run(QStringLiteral("capture.keyParts('Ctrl++')")).toStringList(), (QStringList{"Ctrl", "+"}));
        QCOMPARE(run(QStringLiteral("capture.keyParts('Num++')")).toStringList(), (QStringList{"Num", "+"}));
        QCOMPARE(run(QStringLiteral("capture.keyParts('Ctrl+Shift+Num++')")).toStringList(),
                 (QStringList{"Ctrl", "Shift", "Num", "+"}));
        QCOMPARE(run(QStringLiteral("capture.keyParts('Ctrl+Shift+J')")).toStringList(), (QStringList{"Ctrl", "Shift", "J"}));
        QCOMPARE(run(QStringLiteral("capture.keyParts('+')")).toStringList(), QStringList{"+"});
        run(QStringLiteral("capture.capture('Save', 0, true, (accel, window) => {"
                           "    root.note(accel + '@' + window);"
                           "    capture.ask({ message: 'taken', canSwitch: true, canSetAnyway: true, commands: ['Open'] },"
                           "                a => root.note('answer:' + a))"
                           "}, { anchor: anchorButton })"));
        QTRY_VERIFY(run(QStringLiteral("capture.opened")).toBool());
        QTRY_VERIFY(findIn(root.get(), "hotkeyMappingKeys")->hasActiveFocus());
        QTest::keyClick(root.get(), Qt::Key_F1);
        QTRY_VERIFY(findIn(root.get(), "hotkeyRefusalText")->isVisible());
        QCOMPARE(findIn(root.get(), "hotkeyRefusalText")->property("text").toString(), QStringLiteral("F1 is Help"));
        QVERIFY(notes().isEmpty());
        QTest::keyClick(root.get(), Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(notes(), QStringList{QStringLiteral("Ctrl-K@0")});
        QVERIFY(run(QStringLiteral("capture.opened")).toBool());
        QTRY_VERIFY(findIn(root.get(), "hotkeyQuestionText")->isVisible());
        QCOMPARE(findIn(root.get(), "hotkeyQuestionText")->property("text").toString(),
                 QStringLiteral("Ctrl+K is already used by Open."));
        QCOMPARE(findIn(root.get(), "hotkeySwitch")->property("text").toString(), QStringLiteral("Swap with Open"));
        QCOMPARE(findIn(root.get(), "hotkeyDelete")->property("text").toString(), QStringLiteral("Take it from Open"));
        QCOMPARE(findIn(root.get(), "hotkeySetAnyway")->property("text").toString(), QStringLiteral("Keep both"));
        QTest::keyClick(root.get(), Qt::Key_Tab);
        QTRY_VERIFY(!findIn(root.get(), "hotkeyMappingKeys")->hasActiveFocus());
        QVERIFY(run(QStringLiteral("capture.conflict !== null")).toBool());
        click(findIn(root.get(), "hotkeySwitch"));
        QCOMPARE(notes().last(), QStringLiteral("answer:switch"));
        QTRY_VERIFY(!run(QStringLiteral("capture.opened")).toBool());

        root->setProperty("log", QVariantList());
        run(QStringLiteral("capture.capture('Save', 2, false, (accel, window) => root.note(accel + '@' + window))"));
        QQuickWindow *compact = nullptr;
        QTRY_VERIFY((compact = run(QStringLiteral("capture.compactWindow"))
                               .value<QQuickWindow *>()) != nullptr);
        QVERIFY(QTest::qWaitForWindowFocused(compact));
        QVERIFY(!findIn(compact, "hotkeyWindowChoice")->isVisible());
        QTest::keyClick(compact, Qt::Key_Escape);
        QTRY_VERIFY(!compact->isVisible());
        QVERIFY(notes().isEmpty());
        run(QStringLiteral("capture.capture('Save', 2, false, (accel, window) => root.note(accel + '@' + window))"));
        QVERIFY(QTest::qWaitForWindowFocused(compact));
        QTest::keyClick(compact, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(!compact->isVisible());
        QCOMPARE(notes(), QStringList{QStringLiteral("Ctrl-K@2")});
        run(QStringLiteral("capture.capture('Save', 2, false, (accel, window) => root.note(accel + '@' + window))"));
        QVERIFY(QTest::qWaitForWindowFocused(compact));
        QTest::keyClick(compact, Qt::Key_Tab);
        QTRY_VERIFY(!compact->isVisible());
        QCOMPARE(notes().last(), QStringLiteral("Tab@2"));
    }

    void listChooserTakesARowOrNone()
    {
        run(QStringLiteral("chooser.choose({ title: 'Suggestions', rows: ['their', 'there', 'they\\'re'],"
                           " anchor: anchorButton }, row => root.note('row:' + row))"));
        QTRY_VERIFY(run(QStringLiteral("chooser.inPopover")).toBool());
        QQuickItem *second = nullptr;
        QTRY_VERIFY((second = findIn(root.get(), "listChooserRow1")) != nullptr);
        click(second);
        QCOMPARE(notes(), QStringList{QStringLiteral("row:1")});
        QTRY_VERIFY(!run(QStringLiteral("chooser.opened")).toBool());
        run(QStringLiteral("chooser.choose({ title: 'Suggestions', rows: ['a', 'b', 'c'], anchor: anchorButton },"
                           " row => root.note('row:' + row))"));
        QTRY_VERIFY(run(QStringLiteral("chooser.inPopover")).toBool());
        QTest::keyClick(root.get(), Qt::Key_Down);
        QTest::keyClick(root.get(), Qt::Key_Down);
        QTest::keyClick(root.get(), Qt::Key_Return);
        QCOMPARE(notes().last(), QStringLiteral("row:2"));
        run(QStringLiteral("chooser.choose({ title: 'Suggestions', rows: ['a'], anchor: anchorButton },"
                           " row => root.note('row:' + row))"));
        QTRY_VERIFY(run(QStringLiteral("chooser.inPopover")).toBool());
        QTest::keyClick(root.get(), Qt::Key_Escape);
        QCOMPARE(notes().last(), QStringLiteral("row:-1"));
        QTRY_VERIFY(!run(QStringLiteral("chooser.opened")).toBool());

        root->setProperty("log", QVariantList());
        run(QStringLiteral("chooser.choose({ title: 'Choose the audio track', text: 'The file has 2 audio tracks',"
                           " rows: [{ text: 'Track 1', detail: 'Japanese' }, { text: 'Track 2', detail: 'English' }],"
                           " acceptText: 'Open' }, row => root.note('row:' + row))"));
        QQuickWindow *compact = nullptr;
        QTRY_VERIFY((compact = run(QStringLiteral("chooser.compactWindow"))
                               .value<QQuickWindow *>()) != nullptr);
        QVERIFY(QTest::qWaitForWindowFocused(compact));
        QCOMPARE(findIn(compact, "listChooserTake")->property("text").toString(), QStringLiteral("Open"));
        QCOMPARE(findIn(compact, "listChooserText")->property("text").toString(), QStringLiteral("The file has 2 audio tracks"));
        QVERIFY(QQuickTest::qWaitForPolish(compact));
        auto *list = findIn(compact, "listChooserList");
        QVERIFY(list->height() >= list->property("contentHeight").toReal());
        auto *bar = findIn(compact, "listChooserScrollBar");
        QVERIFY(bar);
        QVERIFY(!bar->isVisible() || bar->opacity() == 0);
        QTest::keyClick(compact, Qt::Key_Down);
        QTest::keyClick(compact, Qt::Key_Return);
        QTRY_VERIFY(!compact->isVisible());
        QCOMPARE(notes(), QStringList{QStringLiteral("row:1")});
        run(QStringLiteral("chooser.choose({ title: 'Choose', rows: ['1', '2', '3', '4', '5', '6', '7', '8', '9', '10'] },"
                           " row => root.note('row:' + row))"));
        QVERIFY(QTest::qWaitForWindowFocused(compact));
        QVERIFY(QQuickTest::qWaitForPolish(compact));
        QVERIFY(bar->isVisible());
        QVERIFY(list->height() < list->property("contentHeight").toReal());
        QVERIFY(findIn(compact, "listChooserRow0")->width() + bar->width() <= list->width());
        QTest::keyClick(compact, Qt::Key_Escape);
        QTRY_VERIFY(!compact->isVisible());
        run(QStringLiteral("chooser.choose({ title: 'Choose', rows: ['x'] }, row => root.note('row:' + row))"));
        QVERIFY(QTest::qWaitForWindowFocused(compact));
        QTest::keyClick(compact, Qt::Key_Escape);
        QTRY_VERIFY(!compact->isVisible());
        QCOMPARE(notes().last(), QStringLiteral("row:-1"));
    }

    void datePickerTypesAndPicks()
    {
        QQuickItem *field = findIn(root.get(), "datePickerField");
        run(QStringLiteral("date.setDate(new Date(2026, 9, 21))"));
        QCOMPARE(field->property("text").toString(),
                 shortDate(QDate(2026, 10, 21)));
        field->forceActiveFocus();
        QTest::keyClick(root.get(), Qt::Key_Down, Qt::AltModifier);
        QTRY_VERIFY(run(QStringLiteral("date.popover.opened")).toBool());
        QTest::keyClick(root.get(), Qt::Key_Right);
        QTest::keyClick(root.get(), Qt::Key_Down);
        QTest::keyClick(root.get(), Qt::Key_PageDown);
        QCOMPARE(run(QStringLiteral("date.pending.getMonth() + '/' + date.pending.getDate()")).toString(),
                 QStringLiteral("10/29"));
        QTest::keyClick(root.get(), Qt::Key_Return);
        QTRY_VERIFY(!run(QStringLiteral("date.popover.visible")).toBool());
        QCOMPARE(run(QStringLiteral("date.date.getMonth() + '/' + date.date.getDate()")).toString(), QStringLiteral("10/29"));
        run(QStringLiteral("date.openCalendar()"));
        QTRY_VERIFY(run(QStringLiteral("date.popover.opened")).toBool());
        QTest::keyClick(root.get(), Qt::Key_Left);
        QTest::keyClick(root.get(), Qt::Key_Escape);
        QTRY_VERIFY(!run(QStringLiteral("date.popover.visible")).toBool());
        QCOMPARE(run(QStringLiteral("date.date.getDate()")).toInt(), 29);
        run(QStringLiteral("date.openCalendar()"));
        QTRY_VERIFY(run(QStringLiteral("date.popover.opened")).toBool());
        QQuickItem *week = nullptr;
        QTRY_VERIFY((week = findIn(root.get(), "datePickerShortcut0")) != nullptr);
        QCOMPARE(week->property("text").toString(), QStringLiteral("A week ago"));
        click(week);
        QTRY_VERIFY(!run(QStringLiteral("date.popover.visible")).toBool());
        QCOMPARE(run(QStringLiteral("date.date.getTime()")).toDouble(),
                 run(QStringLiteral("date.back({ days: 7 }).getTime()")).toDouble());
        field->forceActiveFocus();
        QTest::keyClick(root.get(), Qt::Key_A, Qt::ControlModifier);
        typeText(root.get(), shortDate(QDate(2025, 3, 4)));
        QTest::keyClick(root.get(), Qt::Key_Return);
        QCOMPARE(run(QStringLiteral("date.date.getFullYear() * 10000 + (date.date.getMonth() + 1) * 100 + date.date.getDate()"))
                     .toInt(),
                 20250304);
        QTest::keyClick(root.get(), Qt::Key_A, Qt::ControlModifier);
        typeText(root.get(), QStringLiteral("not a date"));
        QTest::keyClick(root.get(), Qt::Key_Return);
        QCOMPARE(field->property("text").toString(), shortDate(QDate(2025, 3, 4)));
        QTest::keyClick(root.get(), Qt::Key_A, Qt::ControlModifier);
        typeText(root.get(), shortDate(QDate(2024, 6, 10)));
        QTest::keyClick(root.get(), Qt::Key_Down, Qt::AltModifier);
        QTRY_VERIFY(run(QStringLiteral("date.popover.opened")).toBool());
        QCOMPARE(run(QStringLiteral("date.shownYear * 100 + date.shownMonth")).toInt(), 202405);
        QTest::keyClick(root.get(), Qt::Key_Escape);
        QTRY_VERIFY(!run(QStringLiteral("date.popover.visible")).toBool());
        QCOMPARE(run(QStringLiteral("date.date.getFullYear() * 10000 + (date.date.getMonth() + 1) * 100 + date.date.getDate()"))
                     .toInt(),
                 20240610);
        const auto ymd = [&](const char *expression) { return run(QString::fromLatin1(expression)).toInt(); };
        QCOMPARE(ymd("(d => d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate())"
                     "(date.back({ months: 1 }, new Date(2026, 2, 31)))"),
                 20260228);
        QCOMPARE(ymd("(d => d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate())"
                     "(date.back({ months: 1 }, new Date(2026, 4, 15)))"),
                 20260415);
        run(QStringLiteral("date.openCalendar(); date.show(new Date(2026, 0, 15))"));
        QTRY_VERIFY(findIn(root.get(), "datePickerMonth"));
        QCOMPARE(findIn(root.get(), "datePickerMonth")->property("text").toString(),
                 QLocale().standaloneMonthName(1) + QStringLiteral(" 2026"));
        QTest::keyClick(root.get(), Qt::Key_Escape);
    }

    void kitScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        ui::theme::useSettings(store.get());
        auto restore = qScopeGuard([] {
            ui::theme::endPreview();
            ui::theme::useSettings(nullptr);
        });
        const auto around = [](QQuickItem *item, int margin = 24) {
            QQuickTest::qWaitForPolish(item->window());
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            return r.toAlignedRect().adjusted(-margin, -margin, margin, margin) & QRect(QPoint(), item->window()->size());
        };
        for (const auto code : ui::theme::kCodes) {
            const QString theme = ui::theme::codeName(code);
            ui::theme::preview({{QStringLiteral("appearance.theme"), theme}, {QStringLiteral("appearance.followSystem"), false}});
            const auto save = [&](const QString &part, const QImage &image) {
                QVERIFY(image.save(out + QStringLiteral("/popup-") + part + QLatin1Char('-') + theme + QStringLiteral(".png")));
            };
            const auto shotOf = [&](QQuickWindow *window, const QRect &part = {}) {
                QQuickTest::qWaitForPolish(window);
                QTest::qWait(200);
                const QImage image = window->grabWindow();
                return part.isNull() ? image : image.copy(part);
            };
            run(QStringLiteral(
                "box.ask({ title: 'Delete styles', text: 'Delete the 3 selected styles?',"
                " detail: '12 lines use them; they keep the names.', severity: 'warning', dontAsk: true,"
                " details: 'Default, Sign, Top', choices: [{ id: 'delete', text: 'Delete styles', role: 'destructive' },"
                " { id: 'keep', text: 'Keep them', role: 'reject', isDefault: true }] }, null)"));
            QQuickWindow *box = windowOf("box");
            QVERIFY(QTest::qWaitForWindowExposed(box));
            run(QStringLiteral("box.showDetails = true"));
            save(QStringLiteral("message-box"), shotOf(box));
            run(QStringLiteral("box.answer('keep')"));
            QTRY_VERIFY(!box->isVisible());
            run(QStringLiteral("styles.openFor(anchorButton, ['Default', 'Sign', 'Top', 'Karaoke'], ['Sign'],"
                               " { counts: { Default: 412, Sign: 23, Top: 4, Karaoke: 0 } })"));
            QTRY_VERIFY(run(QStringLiteral("styles.opened")).toBool());
            save(QStringLiteral("choose-styles"),
                 shotOf(root.get(), around(run(QStringLiteral("styles.contentItem")).value<QQuickItem *>(), 40)));
            run(QStringLiteral("styles.keep()"));
            run(QStringLiteral("capture.capture('Save', 0, true, (a, w) => capture.ask({ canSwitch: true, canSetAnyway: false,"
                               " commands: ['Open subtitles'] }, null), { anchor: anchorButton })"));
            QTRY_VERIFY(run(QStringLiteral("capture.opened")).toBool());
            QTest::keyClick(root.get(), Qt::Key_K, Qt::ControlModifier);
            QTRY_VERIFY(findIn(root.get(), "hotkeyQuestionText")->isVisible());
            save(QStringLiteral("hotkey-capture"),
                 shotOf(root.get(), around(findIn(root.get(), "hotkeyMappingKeys")->parentItem(), 40)));
            run(QStringLiteral("capture.cancel(false)"));
            run(QStringLiteral("chooser.choose({ title: 'Suggestions', rows: ['their', 'there', 'they\\'re'],"
                               " anchor: anchorButton }, null)"));
            QTRY_VERIFY(run(QStringLiteral("chooser.inPopover")).toBool());
            save(QStringLiteral("list-popover"), shotOf(root.get(), around(findIn(root.get(), "listChooserList"), 40)));
            run(QStringLiteral("chooser.none(false)"));
            run(QStringLiteral("chooser.choose({ title: 'Choose the audio track', text: 'The file has 2 audio tracks',"
                               " rows: [{ text: '1: Main', detail: 'eng, aac' }, { text: '2: Commentary', detail: 'jpn, aac' }],"
                               " acceptText: 'Open' }, null)"));
            QQuickWindow *compact = run(QStringLiteral("chooser.compactWindow")).value<QQuickWindow *>();
            QVERIFY(QTest::qWaitForWindowExposed(compact));
            save(QStringLiteral("list-window"), shotOf(compact));
            run(QStringLiteral("chooser.none(false)"));
            QTRY_VERIFY(!compact->isVisible());
            run(QStringLiteral("date.setDate(new Date(2026, 9, 21)); date.openCalendar()"));
            QTRY_VERIFY(run(QStringLiteral("date.popover.opened")).toBool());
            save(QStringLiteral("date-picker"),
                 shotOf(root.get(), around(run(QStringLiteral("date.popover.contentItem")).value<QQuickItem *>(), 40)));
            run(QStringLiteral("date.popover.revert()"));
            run(QStringLiteral(
                "clashes.review({ title: 'Copy styles to the file', text: '3 styles are already in the file',"
                " detail: '1 more is the same in both and is left out.',"
                " rows: [{ name: 'Default', difference: 'Font, size', suggestion: 'Default (2)' },"
                "        { name: 'Sign', difference: 'Colours', suggestion: 'Sign (2)' },"
                "        { name: 'Top', difference: 'Margins', suggestion: 'Top (2)' }], added: 2,"
                " acceptText: n => 'Write ' + n + ' styles' }, null)"));
            QQuickWindow *review = windowOf("clashes");
            QVERIFY(QTest::qWaitForWindowExposed(review));
            run(QStringLiteral("clashes.setAction(1, 'rename'); clashes.setAction(2, 'skip')"));
            save(QStringLiteral("clash-review"), shotOf(review));
            run(QStringLiteral("clashes.finish(false)"));
            QTRY_VERIFY(!review->isVisible());
            run(QStringLiteral("toasts.fadeAfter = 600000; toasts.clear()"));
            run(QStringLiteral("toasts.post('info', 'Saved', 'episode 01.ass')"));
            run(QStringLiteral("toasts.post('warning', 'The video has no keyframes', 'Timing snaps to frames only.')"));
            run(QStringLiteral("toasts.post('error', 'Could not save', 'The folder is read-only.', { details: 'EACCES' })"));
            save(QStringLiteral("toasts"), shotOf(root.get(), around(findIn(root.get(), "toastStack"), 8)));
            run(QStringLiteral("toasts.clear()"));
            run(QStringLiteral("bar.severity = 'warning'; bar.text = 'This file is read-only: changes are saved elsewhere.'"));
            save(QStringLiteral("inline-bar"), shotOf(root.get(), around(findIn(root.get(), "bar"), 8)));
        }
    }
};

QTEST_MAIN(PopupKitTests)
#include "popup_kit_tests.moc"
