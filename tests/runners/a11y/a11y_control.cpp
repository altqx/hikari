#include "a11y_tree.h"

#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTest>

using namespace hikari::testing;

static const char *kScene = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 320; height: 160; visible: true
    Column {
        TextField { objectName: "text"; Accessible.name: "Line text"; focus: true }
        Button { text: "Save"; Accessible.name: "Save subtitles" }
        // A custom-drawn control must declare its own semantics.
        Rectangle {
            width: 40; height: 20; activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Custom toggle"
            Accessible.focusable: true
        }
    }
}
)";

class A11yControl : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { enableAccessibility(); }

    void rolesNamesAndTabOrder()
    {
        QQmlApplicationEngine engine;
        engine.loadData(kScene);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        const auto nodes = accessibleTree(window);
        const QString tree = describe(nodes);
        QVERIFY2(hasNode(nodes, QAccessible::EditableText, "Line text"), qPrintable(tree));
        QVERIFY2(hasNode(nodes, QAccessible::Button, "Save subtitles"), qPrintable(tree));
        QVERIFY2(hasNode(nodes, QAccessible::Button, "Custom toggle"), qPrintable(tree));
        if (HIKARI_A11Y_EXPECT_MISSING)
            QVERIFY2(hasNode(nodes, QAccessible::Button, "Not in the scene"), qPrintable(tree));

        const QStringList order = tabOrder(window);
        QCOMPARE(order, (QStringList{"Line text", "Save subtitles", "Custom toggle"}));
    }
};

QTEST_MAIN(A11yControl)
#include "a11y_control.moc"
