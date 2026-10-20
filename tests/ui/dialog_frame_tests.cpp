// DG1 (#222): the dialog frame (src/ui/DialogWindow.qml, dialog_placement.h,
// window_frame.h DialogFrame). The frame offers a dialog's window buttons
// (Close, Maximise only while resizable, never Minimise). The first open
// is centred on the opener, 20 lower, inside the screen's free area; later
// opens are where the dialog was left and as big, per dialog, back on a
// screen when the place is off every one, and only the size on Wayland.
// Esc closes (rejected), Enter presses the focused button or the default
// one, the keyboard goes back to the opener's control, a shown dialog is
// raised again rather than opened twice, modality is per dialog, and a
// dialog closes with the window it opened from. A dialog bigger than the
// screen fits it, and a maximised one keeps its normal place.

#include "dialog_placement.h"
#include "settings_store.h"
#include "window_frame.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::DialogFrame;
using ui::DialogPlacement;
namespace dp = ui::dialogplacement;

namespace {

const QByteArray kQml = R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui
Window {
    id: opener
    objectName: "opener"
    x: 40; y: 30
    width: 600; height: 400
    property alias dialog: dialog
    property alias opening: opening
    Button { id: opening; objectName: "opening"; text: "Open"; focus: true }
    DialogWindow {
        id: dialog
        objectName: "dialog"
        dialogId: "test"
        title: "Test"
        defaultButton: ok
        TextField { objectName: "field"; Layout.preferredWidth: 280 }
        TextArea { objectName: "notes"; Layout.preferredWidth: 280; Layout.preferredHeight: 60 }
        Item { objectName: "spacer"; implicitHeight: 0 }
        buttons: [
            PrimaryButton { id: ok; objectName: "ok"; text: "OK"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: dialog.accept() },
            Button { objectName: "cancel"; text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: dialog.reject() }
        ]
    }
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

} // namespace

class DialogFrameTests : public QObject {
    Q_OBJECT

    struct Shown {
        std::unique_ptr<QQmlEngine> engine;
        std::unique_ptr<QQuickWindow> opener;
        QQuickWindow *dialog = nullptr;
        QQuickItem *item(const char *name) const { return findItem(dialog->contentItem(), QLatin1String(name)); }
    };

    std::unique_ptr<ui::SettingsStore> store;

    Shown show(bool openerShown = true)
    {
        Shown s;
        s.engine = std::make_unique<QQmlEngine>();
        QQmlComponent component(s.engine.get());
        component.setData(kQml, QUrl(QStringLiteral("qrc:/dg1-test.qml")));
        if (!component.isReady())
            qWarning() << component.errors();
        s.opener.reset(qobject_cast<QQuickWindow *>(component.create()));
        if (!s.opener)
            return s;
        s.dialog = s.opener->property("dialog").value<QQuickWindow *>();
        s.dialog->setProperty("settings", QVariant::fromValue(store.get()));
        if (openerShown) {
            s.opener->show();
            s.opener->requestActivate();
        }
        return s;
    }

    static bool open(const Shown &s)
    {
        QMetaObject::invokeMethod(s.dialog, "open");
        return QTest::qWaitForWindowExposed(s.dialog);
    }

private slots:
    void init() { store = std::make_unique<ui::SettingsStore>(); }
    void cleanup()
    {
        DialogPlacement::overrideWayland(std::nullopt);
        store.reset();
    }

    void dialogFrameOffersADialogsButtons()
    {
        QQuickWindow window;
        window.setFlags(Qt::Dialog);
        window.resize(300, 200);
        DialogFrame frame;
        QVERIFY(frame.dialog());
        frame.setWindow(&window);
        frame.setResizable(false);
        frame.setCustom(true);
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(window.flags().testFlag(Qt::WindowCloseButtonHint));
        QVERIFY(!window.flags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(!window.flags().testFlag(Qt::WindowMaximizeButtonHint));
        frame.setResizable(true);
        QVERIFY(window.flags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(!window.flags().testFlag(Qt::WindowMinimizeButtonHint));
        frame.setCustom(false);
        QVERIFY(!window.flags().testFlag(Qt::FramelessWindowHint));
        // With the window system's frame from the start, the same buttons.
        QQuickWindow system;
        system.setFlags(Qt::Dialog);
        DialogFrame systemFrame;
        systemFrame.setResizable(true);
        systemFrame.setWindow(&system);
        QVERIFY(!system.flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(system.flags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(!system.flags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(system.flags().testFlag(Qt::WindowCloseButtonHint));
        systemFrame.setResizable(false);
        QVERIFY(!system.flags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(!ui::WindowFrame::remaking(&system));
        // The main window's frame keeps all three.
        QQuickWindow main;
        ui::MainWindowFrame mainFrame;
        QVERIFY(!mainFrame.dialog());
        mainFrame.setWindow(&main);
        mainFrame.setCustom(true);
        QVERIFY(main.flags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(main.flags().testFlag(Qt::WindowMaximizeButtonHint));
    }

    void firstOpenIsCentredOnTheOpener()
    {
        const QRect screen(0, 0, 1920, 1040);
        // Centred on the opener, 20 lower.
        QCOMPARE(dp::centred(QRect(100, 100, 800, 600), QSize(400, 300), screen), QRect(300, 270, 400, 300));
        // Kept 20 inside the free area at its edges.
        QCOMPARE(dp::centred(QRect(1500, 800, 400, 240), QSize(500, 400), screen), QRect(1400, 620, 500, 400));
        QCOMPARE(dp::centred(QRect(-300, -200, 400, 240), QSize(500, 400), screen), QRect(20, 20, 500, 400));
        // Shrunk to the free area less the margins.
        QCOMPARE(dp::centred(QRect(0, 0, 1920, 1040), QSize(3000, 2000), screen), QRect(20, 20, 1880, 1000));
        // The window system's frame counts: the window (without it) sits inside.
        const QMargins frame(8, 31, 8, 8);
        QCOMPARE(dp::centred(QRect(100, 100, 800, 600), QSize(400, 300), screen, frame), QRect(300, 281, 400, 300));
        // On a second screen.
        QCOMPARE(dp::centred(QRect(2000, 100, 800, 600), QSize(400, 300), QRect(1920, 0, 1920, 1040)),
                 QRect(2200, 270, 400, 300));
    }

    void rememberedPlacesStayOnAScreen()
    {
        const QList<QRect> screens{QRect(0, 0, 1920, 1040), QRect(1920, 0, 1280, 984)};
        // Where it was left, at its fixed size.
        QCOMPARE(dp::restored(QRect(500, 300, 999, 999), QSize(400, 300), QSize(400, 300), false, screens),
                 QRect(500, 300, 400, 300));
        // A resizable dialog as big as it was left, never under its smallest.
        QCOMPARE(dp::restored(QRect(500, 300, 700, 500), QSize(400, 300), QSize(400, 300), true, screens),
                 QRect(500, 300, 700, 500));
        QCOMPARE(dp::restored(QRect(500, 300, 200, 100), QSize(400, 300), QSize(400, 300), true, screens),
                 QRect(500, 300, 400, 300));
        // Bigger than the screen: a resizable one fills it, a fixed one keeps
        // the first open's 20 clear at each side (the same size either way).
        QCOMPARE(dp::restored(QRect(0, 0, 3000, 2000), QSize(400, 300), QSize(400, 300), true, screens.mid(0, 1)),
                 QRect(0, 0, 1920, 1040));
        QCOMPARE(dp::restored(QRect(0, 0, 400, 300), QSize(2000, 1100), QSize(2000, 1100), false, screens.mid(0, 1)),
                 QRect(0, 0, 1880, 1000));
        // Partly off its screen: moved whole onto it.
        QCOMPARE(dp::restored(QRect(1700, 900, 400, 300), QSize(400, 300), QSize(400, 300), false, screens.mid(0, 1)),
                 QRect(1520, 740, 400, 300));
        // On the second screen, kept there.
        QCOMPARE(dp::restored(QRect(2500, 200, 400, 300), QSize(400, 300), QSize(400, 300), false, screens),
                 QRect(2500, 200, 400, 300));
        // The second screen unplugged: no place (the first-open rule).
        QCOMPARE(dp::restored(QRect(2500, 200, 400, 300), QSize(400, 300), QSize(400, 300), false, screens.mid(0, 1)),
                 std::nullopt);
        // Its top edge above every screen: none either.
        QCOMPARE(dp::restored(QRect(500, -400, 400, 300), QSize(400, 300), QSize(400, 300), false, screens), std::nullopt);
        // Only a sliver of the top edge on a screen is not enough to move it by.
        QCOMPARE(dp::restored(QRect(-380, 100, 400, 300), QSize(400, 300), QSize(400, 300), false, screens), std::nullopt);
        QCOMPARE(dp::restored(QRect(-300, 100, 400, 300), QSize(400, 300), QSize(400, 300), false, screens),
                 QRect(0, 100, 400, 300));
    }

    void rowsKeepOnePlacePerDialog()
    {
        QStringList rows;
        rows = dp::replaced(rows, QStringLiteral("about"), QRect(1, 2, 300, 200));
        rows = dp::replaced(rows, QStringLiteral("credits"), QRect(-5, 6, 400, 500));
        rows = dp::replaced(rows, QStringLiteral("about"), QRect(10, 20, 300, 200));
        QCOMPARE(rows, (QStringList{QStringLiteral("about\t10,20,300,200"), QStringLiteral("credits\t-5,6,400,500")}));
        QCOMPARE(dp::find(rows, QStringLiteral("about")), QRect(10, 20, 300, 200));
        QCOMPARE(dp::find(rows, QStringLiteral("credits")), QRect(-5, 6, 400, 500));
        QCOMPARE(dp::find(rows, QStringLiteral("abou")), std::nullopt);
        QCOMPARE(dp::find(QStringList{QStringLiteral("about\tbroken")}, QStringLiteral("about")), std::nullopt);
        QCOMPARE(dp::find(QStringList{QStringLiteral("about\t1,2,0,5")}, QStringLiteral("about")), std::nullopt);
    }

    void dialogOpensCentredThenWhereItWasLeft()
    {
        {
            Shown s = show();
            QVERIFY(s.dialog);
            QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
            QVERIFY(open(s));
            const QRect opener = s.opener->frameGeometry();
            const QRect first = s.dialog->geometry();
            QCOMPARE(s.dialog->transientParent(), s.opener.get());
            QCOMPARE(s.dialog->modality(), Qt::NonModal);
            QCOMPARE(first.size(), QSize(s.dialog->property("naturalWidth").toInt(), s.dialog->property("naturalHeight").toInt()));
            const QRect area = s.dialog->screen()->availableGeometry().marginsRemoved(QMargins(20, 20, 20, 20));
            QRect expected(QPoint(), s.dialog->frameGeometry().size());
            expected.moveCenter(opener.center() + QPoint(0, 20));
            if (area.contains(expected))
                QCOMPARE(s.dialog->frameGeometry().topLeft(), expected.topLeft());
            // Fixed size: the content's.
            QCOMPARE(s.dialog->minimumSize(), first.size());
            QCOMPARE(s.dialog->maximumSize(), first.size());
            // Moved, closed, opened again: where it was left.
            s.dialog->setPosition(first.topLeft() + QPoint(37, 11));
            QTRY_COMPARE(s.dialog->position(), first.topLeft() + QPoint(37, 11));
            QMetaObject::invokeMethod(s.dialog, "close");
            QTRY_VERIFY(!s.dialog->isVisible());
            QCOMPARE(dp::find(store->list(dp::kSetting), QStringLiteral("test")),
                     QRect(first.topLeft() + QPoint(37, 11), first.size()));
            QVERIFY(open(s));
            QCOMPARE(s.dialog->position(), first.topLeft() + QPoint(37, 11));
            QMetaObject::invokeMethod(s.dialog, "close");
            QTRY_VERIFY(!s.dialog->isVisible());
        }
        // A new start (a new window, the same settings): still there.
        const QRect kept = *dp::find(store->list(dp::kSetting), QStringLiteral("test"));
        {
            Shown s = show();
            QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
            QVERIFY(open(s));
            QCOMPARE(s.dialog->position(), kept.topLeft());
            QMetaObject::invokeMethod(s.dialog, "close");
            QTRY_VERIFY(!s.dialog->isVisible());
        }
        // A place off every screen: centred on the opener again.
        store->setValue(QLatin1String(dp::kSetting), QStringList{QStringLiteral("test\t-30000,-30000,10,10")});
        {
            Shown s = show();
            QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
            QVERIFY(open(s));
            QVERIFY(s.dialog->screen()->availableGeometry().contains(s.dialog->geometry()));
            QVERIFY(s.dialog->size() != QSize(10, 10));
        }
    }

    void resizableDialogKeepsItsSizeAndWaylandOnlyThat()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        s.dialog->setProperty("resizable", true);
        QVERIFY(open(s));
        const QSize natural = s.dialog->size();
        QCOMPARE(s.dialog->minimumSize(), natural);
        QVERIFY(s.dialog->maximumSize().width() > 10000);
        s.dialog->resize(natural + QSize(120, 80));
        QTRY_COMPARE(s.dialog->size(), natural + QSize(120, 80));
        const QPoint at = s.dialog->position();
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        QVERIFY(open(s));
        QCOMPARE(s.dialog->size(), natural + QSize(120, 80));
        QCOMPARE(s.dialog->position(), at);
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        // Wayland: the size, and the place is the compositor's (left as it is).
        DialogPlacement::overrideWayland(true);
        store->setValue(QLatin1String(dp::kSetting),
                        QStringList{QStringLiteral("test\t%1,%2,%3,%4").arg(at.x() + 50).arg(at.y() + 50).arg(natural.width() + 10).arg(natural.height() + 10)});
        QVERIFY(open(s));
        QCOMPARE(s.dialog->size(), natural + QSize(10, 10));
        QVERIFY(s.dialog->position() != at + QPoint(50, 50));
    }

    void aDialogBiggerThanTheScreenFitsIt()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        s.item("spacer")->setProperty("implicitHeight", 5000);
        QVERIFY(open(s));
        const int natural = s.dialog->property("naturalHeight").toInt();
        QVERIFY(natural > 5000);
        // The window with its frame (none, or the platform's) fills the room.
        const QRect area = s.dialog->screen()->availableGeometry().marginsRemoved(QMargins(20, 20, 20, 20));
        QCOMPARE(s.dialog->frameGeometry().height(), area.height());
        QVERIFY(area.contains(s.dialog->frameGeometry()));
        // Its smallest and (fixed) biggest are the screen's room, not the content's.
        QCOMPARE(s.dialog->minimumHeight(), s.dialog->height());
        QCOMPARE(s.dialog->maximumHeight(), s.dialog->height());
    }

    void aMaximisedDialogKeepsItsNormalPlace()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        s.dialog->setProperty("resizable", true);
        QVERIFY(open(s));
        const QRect normal = s.dialog->geometry();
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        QCOMPARE(dp::find(store->list(dp::kSetting), QStringLiteral("test")), normal);
        QVERIFY(open(s));
        s.dialog->showMaximized();
        QTRY_COMPARE(s.dialog->visibility(), QWindow::Maximized);
        // The window system's own report of it, before closing.
        QTest::qWait(50);
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        // The place before maximising is kept, and it opens there, not maximised.
        QCOMPARE(dp::find(store->list(dp::kSetting), QStringLiteral("test")), normal);
        QVERIFY(open(s));
        QCOMPARE(s.dialog->visibility(), QWindow::Windowed);
        QCOMPARE(s.dialog->geometry(), normal);
    }

    void escapeClosesAndTheKeyboardGoesBack()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowActive(s.opener.get()));
        auto *opening = s.opener->property("opening").value<QQuickItem *>();
        opening->forceActiveFocus();
        QVERIFY(opening->hasActiveFocus());
        QSignalSpy rejected(s.dialog, SIGNAL(rejected()));
        QSignalSpy accepted(s.dialog, SIGNAL(accepted()));
        QVERIFY(open(s));
        QVERIFY(QTest::qWaitForWindowActive(s.dialog));
        // The keyboard starts on the default button, ringed.
        QTRY_COMPARE(s.dialog->activeFocusItem(), s.item("ok"));
        QVERIFY(s.item("ok")->property("visualFocus").toBool());
        QTest::keyClick(s.dialog, Qt::Key_Escape);
        QTRY_VERIFY(!s.dialog->isVisible());
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(accepted.count(), 0);
        QTRY_VERIFY(s.opener->isActive());
        QTRY_VERIFY(opening->hasActiveFocus());
        // Close in the title bar (window close) is a rejection too.
        QVERIFY(open(s));
        QVERIFY(s.dialog->close());
        QTRY_VERIFY(!s.dialog->isVisible());
        QCOMPARE(rejected.count(), 2);
    }

    void enterPressesTheFocusedOrTheDefaultButton()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowActive(s.opener.get()));
        QSignalSpy rejected(s.dialog, SIGNAL(rejected()));
        QSignalSpy accepted(s.dialog, SIGNAL(accepted()));
        // From a single-line field: the default button.
        QVERIFY(open(s));
        QVERIFY(QTest::qWaitForWindowActive(s.dialog));
        s.item("field")->forceActiveFocus();
        QTest::keyClick(s.dialog, Qt::Key_Return);
        QTRY_VERIFY(!s.dialog->isVisible());
        QCOMPARE(accepted.count(), 1);
        // A multiline field keeps its Enter.
        QVERIFY(open(s));
        QVERIFY(QTest::qWaitForWindowActive(s.dialog));
        s.item("notes")->forceActiveFocus();
        QTest::keyClick(s.dialog, Qt::Key_Return);
        QVERIFY(s.dialog->isVisible());
        QCOMPARE(s.item("notes")->property("lineCount").toInt(), 2);
        // On another button: that button.
        s.item("cancel")->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(s.dialog, Qt::Key_Enter);
        QTRY_VERIFY(!s.dialog->isVisible());
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(rejected.count(), 1);
    }

    void aShownDialogIsRaisedNotOpenedAgain()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        QVERIFY(open(s));
        const QPoint at = s.dialog->position() + QPoint(15, 15);
        s.dialog->setPosition(at);
        QTRY_COMPARE(s.dialog->position(), at);
        QMetaObject::invokeMethod(s.dialog, "open");
        QCOMPARE(s.dialog->position(), at); // not placed again
        QVERIFY(s.dialog->isVisible());
        // Modal to its opener only, when a dialog asks it.
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        s.dialog->setProperty("modal", true);
        QCOMPARE(s.dialog->modality(), Qt::WindowModal);
    }

    void dialogClosesWithItsOpener()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        QVERIFY(open(s));
        s.opener->hide();
        QTRY_VERIFY(!s.dialog->isVisible());
    }
};

QTEST_MAIN(DialogFrameTests)
#include "dialog_frame_tests.moc"
