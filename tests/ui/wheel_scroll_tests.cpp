#include "wheel_scroll.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStyleHints>
#include <QWheelEvent>
#include <QtTest>

#include <cmath>
#include <memory>

using namespace hikari;

namespace {

const QByteArray kQml = R"(
import QtQuick
import QtQuick.Controls
Window {
    width: 400; height: 300
    ScrollView {
        id: outer
        objectName: "outer"
        anchors.fill: parent
        contentWidth: availableWidth
        Column {
            width: outer.availableWidth
            ListView {
                objectName: "inner"
                width: parent.width
                height: 100
                boundsBehavior: Flickable.StopAtBounds
                model: 20
                delegate: Text { text: "row " + index }
            }
            Repeater {
                model: 200
                Text { text: "line " + index }
            }
        }
    }
    Flickable {
        objectName: "still"
        x: 300; width: 100; height: 100
        interactive: false
        contentHeight: 1000
    }
}
)";

}

class WheelScrollTests : public QObject {
    Q_OBJECT

    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window;
    QQuickItem *outer = nullptr;
    QQuickItem *inner = nullptr;
    QQuickItem *still = nullptr;
    qreal notch = 0;

    bool wheel(QPointF at, QPoint pixels, QPoint angle, Qt::MouseEventSource source = Qt::MouseEventNotSynthesized,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QWheelEvent event(at, window->mapToGlobal(at), pixels, angle, Qt::NoButton, modifiers,
                          source == Qt::MouseEventNotSynthesized ? Qt::NoScrollPhase : Qt::ScrollUpdate, false,
                          source);
        QCoreApplication::sendEvent(window.get(), &event);
        return event.isAccepted();
    }

    static qreal contentY(const QQuickItem *flickable) { return flickable->property("contentY").toReal(); }
    static qreal aligned(qreal pixels) { return std::round(pixels); }

private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData(kQml, QUrl());
        window.reset(qobject_cast<QQuickWindow *>(component.create()));
        QVERIFY2(window, qPrintable(component.errorString()));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        outer = window->findChild<QQuickItem *>(QStringLiteral("outer"))->property("contentItem").value<QQuickItem *>();
        inner = window->findChild<QQuickItem *>(QStringLiteral("inner"));
        still = window->findChild<QQuickItem *>(QStringLiteral("still"));
        QVERIFY(outer && outer->inherits("QQuickFlickable") && inner && still);
        notch = QGuiApplication::styleHints()->wheelScrollLines() * QFontMetricsF(QGuiApplication::font()).lineSpacing();
    }

    void init()
    {
        outer->setProperty("contentY", 0);
        inner->setProperty("contentY", 0);
    }

    void aNotchScrollsTheSystemLinesOfText()
    {
        ui::WheelScroll scroll;
        QVERIFY(wheel({100, 200}, {}, {0, -120}));
        QCOMPARE(contentY(outer), aligned(notch));
        QVERIFY(wheel({100, 200}, {}, {0, 120}));
        QCOMPARE(contentY(outer), 0.0);
    }

    void highResolutionWheelsAddUpToTheNotch()
    {
        ui::WheelScroll scroll;
        for (int i = 0; i < 8; ++i)
            QVERIFY(wheel({100, 200}, {}, {0, -15}));
        QCOMPARE(contentY(outer), aligned(notch));
    }

    void touchpadsScrollTheirOwnDistance()
    {
        ui::WheelScroll scroll;
        QVERIFY(wheel({100, 200}, {0, -7}, {0, -56}, Qt::MouseEventSynthesizedBySystem));
        QCOMPARE(contentY(outer), 7.0);
    }

    void waylandWheelsScrollANotchDespiteTheirPixelDelta()
    {
        ui::WheelScroll scroll;
        QVERIFY(wheel({100, 200}, {0, -15}, {0, -120}));
        QCOMPARE(contentY(outer), aligned(notch));
    }

    void shiftTurnsASidewaysDeltaVertical()
    {
        ui::WheelScroll scroll;
        QVERIFY(wheel({100, 200}, {}, {-120, 0}, Qt::MouseEventNotSynthesized, Qt::ShiftModifier));
        QCOMPARE(contentY(outer), aligned(notch));
        wheel({100, 200}, {}, {-120, 0});
        QCOMPARE(contentY(outer), aligned(notch));
    }

    void theInnerListScrollsUntilItsEndThenTheView()
    {
        ui::WheelScroll scroll;
        QSignalSpy ended(inner, SIGNAL(movementEnded()));
        QVERIFY(wheel({100, 50}, {}, {0, -120}));
        QCOMPARE(contentY(inner), notch);
        QCOMPARE(contentY(outer), 0.0);
        QCOMPARE(ended.size(), 1);
        QVERIFY(wheel({100, 50}, {}, {0, -120 * 1000}));
        QVERIFY(inner->property("atYEnd").toBool());
        QCOMPARE(contentY(outer), 0.0);
        QVERIFY(wheel({100, 50}, {}, {0, -120}));
        QCOMPARE(contentY(outer), aligned(notch));
        QCOMPARE(ended.size(), 2);
    }

    void theViewStopsAtItsEnds()
    {
        ui::WheelScroll scroll;
        wheel({100, 200}, {}, {0, 120});
        QCOMPARE(contentY(outer), 0.0);
        QVERIFY(wheel({100, 200}, {}, {0, -120 * 1000}));
        const qreal end = outer->property("contentHeight").toReal() - outer->height();
        QCOMPARE(contentY(outer), aligned(end));
        wheel({100, 200}, {}, {0, -120});
        QCOMPARE(contentY(outer), aligned(end));
    }

    void partsOfAPixelAddUp()
    {
        ui::WheelScroll scroll;
        for (int i = 0; i < 40; ++i)
            QVERIFY(wheel({100, 200}, {}, {0, -1}));
        QCOMPARE(contentY(outer), aligned(notch / 3));
    }

    void partsOfAPixelAddUpPastAnInnerListAtItsEnd()
    {
        ui::WheelScroll scroll;
        QVERIFY(wheel({100, 50}, {}, {0, -120 * 1000}));
        for (int i = 0; i < 40; ++i)
            QVERIFY(wheel({100, 50}, {}, {0, -1}));
        QCOMPARE(contentY(outer), aligned(notch / 3));
    }

    void aFlickableThatIsNotInteractiveLetsTheListBelowScroll()
    {
        ui::WheelScroll scroll;
        QVERIFY(wheel({350, 50}, {}, {0, -120}));
        QCOMPARE(contentY(still), 0.0);
        QCOMPARE(contentY(inner), notch);
    }
};

QTEST_MAIN(WheelScrollTests)
#include "wheel_scroll_tests.moc"
