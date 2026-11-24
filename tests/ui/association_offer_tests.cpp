#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

namespace {

const QByteArray kQml = R"(
import QtQuick
import Hikari.Ui
Window {
    width: 1400; height: 600
    property alias panel: panel
    property alias video: video
    QtObject {
        id: video
        property bool offering: true
        property string offer: "Associated files:\nVideo: /tmp/hikari_ui_video_workflow-BKODqJ/ep1.mkv\n\nVideo from directory: ep1.mkv"
        property string offerAssociatedLabel: "Load associated"
        property string offerDirectoryLabel: "Load from directory"
        property bool offerApplyToAll: false
        property int loads: 0
        function loadAssociated() { ++loads }
        function loadFromDirectory() {}
        function dismissOffer() {}
    }
    Item {
        id: panel
        objectName: "panel"
        width: 560; height: 520
        clip: true
        AssociationOffer {
            video: video
            anchors { left: parent.left; right: parent.right; top: parent.top }
        }
    }
}
)";

QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = findItem(child, name))
            return found;
    return nullptr;
}

}

class AssociationOfferTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QQuickWindow> window;
    QQuickItem *panel = nullptr;

    QQuickItem *item(const char *name) const { return findItem(panel, QLatin1String(name)); }

    QRectF inPanel(const char *name) const
    {
        QQuickItem *control = item(name);
        return control->mapRectToItem(panel, QRectF(0, 0, control->width(), control->height()));
    }

    void showAt(qreal width)
    {
        panel->setWidth(width);
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QVERIFY(QQuickTest::qWaitForPolish(window.get()));
    }

private slots:
    void init()
    {
        engine = std::make_unique<QQmlEngine>();
        QQmlComponent component(engine.get());
        component.setData(kQml, QUrl(QStringLiteral("qrc:/association-offer-test.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        window.reset(qobject_cast<QQuickWindow *>(component.create()));
        QVERIFY(window);
        panel = window->property("panel").value<QQuickItem *>();
        window->show();
    }
    void cleanup()
    {
        window.reset();
        engine.reset();
    }

    void everyButtonIsInsideTheVideoPanel_data()
    {
        QTest::addColumn<qreal>("width");
        QTest::newRow("default layout at 1280") << 560.0;
        QTest::newRow("narrow") << 300.0;
        QTest::newRow("wide") << 1200.0;
    }
    void everyButtonIsInsideTheVideoPanel()
    {
        QFETCH(qreal, width);
        showAt(width);
        const QRectF bounds(0, 0, panel->width(), panel->height());
        for (const char *name : {"associationText", "associationApplyToAll", "loadAssociated", "loadFromDirectory",
                                 "dismissAssociation"}) {
            const QRectF rect = inPanel(name);
            QVERIFY2(rect.width() > 0 && bounds.contains(rect), qPrintable(QStringLiteral("%1 at %2,%3 %4x%5")
                                                                               .arg(QLatin1String(name))
                                                                               .arg(rect.x())
                                                                               .arg(rect.y())
                                                                               .arg(rect.width())
                                                                               .arg(rect.height())));
        }
        QVERIFY(inPanel("associationText").width() >= item("associationOffer")->property("minimumTextWidth").toReal());
    }

    void theButtonsGoBelowTheTextOnlyWhenItWouldBeTooNarrow()
    {
        showAt(1200);
        QVERIFY(!item("associationOffer")->property("stacked").toBool());
        QVERIFY(inPanel("loadAssociated").left() >= inPanel("associationText").right());
        showAt(300);
        QVERIFY(item("associationOffer")->property("stacked").toBool());
        QVERIFY(inPanel("loadAssociated").top() >= inPanel("associationText").bottom());
    }

    void clickingLoadAssociatedInANarrowPanelLoads()
    {
        showAt(560);
        QQuickItem *button = item("loadAssociated");
        const QPointF centre = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        QTest::mouseClick(window.get(), Qt::LeftButton, {}, centre.toPoint());
        QTRY_COMPARE(window->property("video").value<QObject *>()->property("loads").toInt(), 1);
    }
};

QTEST_MAIN(AssociationOfferTests)
#include "association_offer_tests.moc"
