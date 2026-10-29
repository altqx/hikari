#include "dialog_placement.h"
#include "settings_store.h"
#include "window_frame.h"

#include <QFontMetricsF>
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

const QByteArray kWrappingQml = R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui
Window {
    id: opener
    width: 600; height: 400
    property alias dialog: dialog
    property int columnWidth: 280
    DialogWindow {
        id: dialog
        title: "Wrapping"
        ColumnLayout {
            Layout.preferredWidth: opener.columnWidth
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Name"; Layout.preferredWidth: 60 }
                Label { objectName: "wrapped"; text: "A line of text long enough to wrap in the column of the dialog more than once at its width, "
                    + "and then some more of it, so that even a column as wide as a whole screen breaks it into lines"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
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

}

class DialogFrameTests : public QObject {
    Q_OBJECT

    struct Shown {
        std::unique_ptr<QQmlEngine> engine;
        std::unique_ptr<QQuickWindow> opener;
        QQuickWindow *dialog = nullptr;
        QQuickItem *item(const char *name) const { return findItem(dialog->contentItem(), QLatin1String(name)); }
    };

    std::unique_ptr<ui::SettingsStore> store;

    Shown show(bool openerShown = true, const QByteArray &qml = kQml)
    {
        Shown s;
        s.engine = std::make_unique<QQmlEngine>();
        QQmlComponent component(s.engine.get());
        component.setData(qml, QUrl(QStringLiteral("qrc:/dg1-test.qml")));
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
        QCOMPARE(dp::centred(QRect(100, 100, 800, 600), QSize(400, 300), screen), QRect(300, 270, 400, 300));
        QCOMPARE(dp::centred(QRect(1500, 800, 400, 240), QSize(500, 400), screen), QRect(1400, 620, 500, 400));
        QCOMPARE(dp::centred(QRect(-300, -200, 400, 240), QSize(500, 400), screen), QRect(20, 20, 500, 400));
        QCOMPARE(dp::centred(QRect(0, 0, 1920, 1040), QSize(3000, 2000), screen), QRect(20, 20, 1880, 1000));
        const QMargins frame(8, 31, 8, 8);
        QCOMPARE(dp::centred(QRect(100, 100, 800, 600), QSize(400, 300), screen, frame), QRect(300, 281, 400, 300));
        QCOMPARE(dp::centred(QRect(2000, 100, 800, 600), QSize(400, 300), QRect(1920, 0, 1920, 1040)),
                 QRect(2200, 270, 400, 300));
    }

    void rememberedPlacesStayOnAScreen()
    {
        const QList<QRect> screens{QRect(0, 0, 1920, 1040), QRect(1920, 0, 1280, 984)};
        QCOMPARE(dp::restored(QRect(500, 300, 999, 999), QSize(400, 300), QSize(400, 300), false, screens),
                 QRect(500, 300, 400, 300));
        QCOMPARE(dp::restored(QRect(500, 300, 700, 500), QSize(400, 300), QSize(400, 300), true, screens),
                 QRect(500, 300, 700, 500));
        QCOMPARE(dp::restored(QRect(500, 300, 200, 100), QSize(400, 300), QSize(400, 300), true, screens),
                 QRect(500, 300, 400, 300));
        QCOMPARE(dp::restored(QRect(0, 0, 3000, 2000), QSize(400, 300), QSize(400, 300), true, screens.mid(0, 1)),
                 QRect(0, 0, 1920, 1040));
        QCOMPARE(dp::restored(QRect(0, 0, 400, 300), QSize(2000, 1100), QSize(2000, 1100), false, screens.mid(0, 1)),
                 QRect(0, 0, 1880, 1000));
        QCOMPARE(dp::restored(QRect(1700, 900, 400, 300), QSize(400, 300), QSize(400, 300), false, screens.mid(0, 1)),
                 QRect(1520, 740, 400, 300));
        QCOMPARE(dp::restored(QRect(2500, 200, 400, 300), QSize(400, 300), QSize(400, 300), false, screens),
                 QRect(2500, 200, 400, 300));
        QCOMPARE(dp::restored(QRect(2500, 200, 400, 300), QSize(400, 300), QSize(400, 300), false, screens.mid(0, 1)),
                 std::nullopt);
        QCOMPARE(dp::restored(QRect(500, -400, 400, 300), QSize(400, 300), QSize(400, 300), false, screens), std::nullopt);
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
            QCOMPARE(s.dialog->minimumSize(), first.size());
            QCOMPARE(s.dialog->maximumSize(), first.size());
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
        const QRect kept = *dp::find(store->list(dp::kSetting), QStringLiteral("test"));
        {
            Shown s = show();
            QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
            QVERIFY(open(s));
            QCOMPARE(s.dialog->position(), kept.topLeft());
            QMetaObject::invokeMethod(s.dialog, "close");
            QTRY_VERIFY(!s.dialog->isVisible());
        }
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
        DialogPlacement::overrideWayland(true);
        store->setValue(QLatin1String(dp::kSetting),
                        QStringList{QStringLiteral("test\t%1,%2,%3,%4").arg(at.x() + 50).arg(at.y() + 50).arg(natural.width() + 10).arg(natural.height() + 10)});
        QVERIFY(open(s));
        QCOMPARE(s.dialog->size(), natural + QSize(10, 10));
        QVERIFY(s.dialog->position() != at + QPoint(50, 50));
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        store->setValue(QLatin1String(dp::kSetting), QStringList{QStringLiteral("test\t0,0,30000,20000")});
        QVERIFY(open(s));
        QCOMPARE(s.dialog->size(), s.dialog->screen()->availableGeometry().size());
    }

    void wrappedTextHasItsHeightOnTheFirstOpen()
    {
        Shown s = show(true, kWrappingQml);
        QVERIFY(s.dialog);
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        QMetaObject::invokeMethod(s.dialog, "open");
        const int opened = s.dialog->height();
        const qreal width = s.item("wrapped")->width();
        QVERIFY(QTest::qWaitForWindowExposed(s.dialog));
        QTest::qWait(50);
        QCOMPARE(s.item("wrapped")->width(), width);
        QVERIFY(s.item("wrapped")->property("lineCount").toInt() > 1);
        QCOMPARE(s.dialog->property("naturalHeight").toInt(), opened);
        QCOMPARE(s.dialog->height(), opened);
    }

    void wrappedTextHasItsHeightOnANarrowerScreen()
    {
        Shown s = show(true, kWrappingQml);
        QVERIFY(s.dialog);
        s.opener->setProperty("columnWidth", 3000);
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        QQuickItem *label = s.item("wrapped");
        const QFontMetricsF metrics(label->property("font").value<QFont>());
        const qreal wide = 3.0 * s.opener->screen()->availableGeometry().width();
        QString text;
        while (metrics.horizontalAdvance(text) < wide)
            text += QStringLiteral("wrapping words ");
        label->setProperty("text", text.trimmed());
        QMetaObject::invokeMethod(s.dialog, "open");
        const int opened = s.dialog->height();
        const qreal width = s.item("wrapped")->width();
        QVERIFY(QTest::qWaitForWindowExposed(s.dialog));
        QTest::qWait(50);
        const QRect area = s.dialog->screen()->availableGeometry().marginsRemoved(QMargins(20, 20, 20, 20));
        QCOMPARE(s.dialog->frameGeometry().width(), area.width());
        QVERIFY(s.item("wrapped")->property("lineCount").toInt() > 1);
        QCOMPARE(s.item("wrapped")->width(), width);
        QCOMPARE(s.dialog->height(), opened);
        QCOMPARE(s.dialog->property("naturalHeight").toInt(), opened);
    }

    void aDialogClosedInTheBackgroundTakesNoKeyboard()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowActive(s.opener.get()));
        QVERIFY(open(s));
        QVERIFY(QTest::qWaitForWindowActive(s.dialog));
        QQuickWindow other;
        other.resize(200, 100);
        other.show();
        other.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        QTest::qWait(50);
        QCOMPARE(QGuiApplication::focusWindow(), &other);
        QVERIFY(s.opener->property("opening").value<QQuickItem *>()->hasFocus());
    }

    void anAutomaticNoticeTakesNoKeyboard()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowActive(s.opener.get()));
        QQuickWindow other;
        other.resize(200, 100);
        other.show();
        other.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QVERIFY(QMetaObject::invokeMethod(s.dialog, "openQuietly"));
        QVERIFY(s.dialog->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(s.dialog));
        QTest::qWait(50);
        QVERIFY(s.dialog->property("_q_showWithoutActivating").toBool());
        // The offscreen platform activates every window it shows.
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QCOMPARE(QGuiApplication::focusWindow(), &other);
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
        QVERIFY(open(s));
        QVERIFY(!s.dialog->property("_q_showWithoutActivating").toBool());
        QVERIFY(QTest::qWaitForWindowActive(s.dialog));
    }

    void aMinimisedDialogComesBackWhenOpenedAgain()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        QVERIFY(open(s));
        s.dialog->showMinimized();
        QTRY_COMPARE(s.dialog->visibility(), QWindow::Minimized);
        QTest::qWait(50);
        QMetaObject::invokeMethod(s.dialog, "open");
        QTRY_COMPARE(s.dialog->visibility(), QWindow::Windowed);
    }

    void aShownDialogKeepsToTheScreenWhenItsFrameChanges()
    {
        namespace wf = ui::windowframe;
        // The offscreen platform has no desktop; Plasma's lets a dialog draw its own frame.
        wf::overrideDesktop(wf::Desktop::Kde);
        auto restore = qScopeGuard([] { wf::overrideDesktop(std::nullopt); });
        store->setValue(QStringLiteral("appearance.titleBar"), QStringLiteral("custom"));
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        s.item("spacer")->setProperty("implicitHeight", 5000);
        QVERIFY(open(s));
        QVERIFY(s.dialog->flags().testFlag(Qt::FramelessWindowHint));
        const QRect area = s.dialog->screen()->availableGeometry().marginsRemoved(QMargins(20, 20, 20, 20));
        QCOMPARE(s.dialog->frameGeometry().height(), area.height());
        // Offscreen keeps a shown window's frame margins, so the move off the screen stands in for a real frame's borders.
        const QRect screen = s.dialog->screen()->availableGeometry();
        s.dialog->setY(s.dialog->y() + 200);
        QTRY_VERIFY(!screen.contains(s.dialog->frameGeometry()));
        store->setValue(QStringLiteral("appearance.titleBar"), QStringLiteral("system"));
        QTRY_VERIFY(!s.dialog->flags().testFlag(Qt::FramelessWindowHint));
        QTRY_VERIFY(screen.contains(s.dialog->frameGeometry()));
        QVERIFY(s.dialog->frameGeometry().height() <= area.height());
    }

    void aDialogBiggerThanTheScreenFitsIt()
    {
        Shown s = show();
        QVERIFY(QTest::qWaitForWindowExposed(s.opener.get()));
        s.item("spacer")->setProperty("implicitHeight", 5000);
        QVERIFY(open(s));
        const int natural = s.dialog->property("naturalHeight").toInt();
        QVERIFY(natural > 5000);
        const QRect area = s.dialog->screen()->availableGeometry().marginsRemoved(QMargins(20, 20, 20, 20));
        QCOMPARE(s.dialog->frameGeometry().height(), area.height());
        QVERIFY(area.contains(s.dialog->frameGeometry()));
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
        // MUST KILL: fixed 50 ms wait for the window system's report of the maximised window
        QTest::qWait(50);
        QMetaObject::invokeMethod(s.dialog, "close");
        QTRY_VERIFY(!s.dialog->isVisible());
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
        QTRY_COMPARE(s.dialog->activeFocusItem(), s.item("ok"));
        QVERIFY(s.item("ok")->property("visualFocus").toBool());
        QTest::keyClick(s.dialog, Qt::Key_Escape);
        QTRY_VERIFY(!s.dialog->isVisible());
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(accepted.count(), 0);
        QTRY_VERIFY(s.opener->isActive());
        QTRY_VERIFY(opening->hasActiveFocus());
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
        QVERIFY(open(s));
        QVERIFY(QTest::qWaitForWindowActive(s.dialog));
        s.item("field")->forceActiveFocus();
        QTest::keyClick(s.dialog, Qt::Key_Return);
        QTRY_VERIFY(!s.dialog->isVisible());
        QCOMPARE(accepted.count(), 1);
        QVERIFY(open(s));
        QVERIFY(QTest::qWaitForWindowActive(s.dialog));
        s.item("notes")->forceActiveFocus();
        QTest::keyClick(s.dialog, Qt::Key_Return);
        QVERIFY(s.dialog->isVisible());
        QCOMPARE(s.item("notes")->property("lineCount").toInt(), 2);
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
        QCOMPARE(s.dialog->position(), at);
        QVERIFY(s.dialog->isVisible());
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
