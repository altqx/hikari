#include "shell_menus.h"

#include <QtTest>

using hikari::ui::ShellMenus;

class ShellMenuPlacementTest : public QObject {
    Q_OBJECT
    const QRectF screen{0, 0, 1280, 720};
    const QSizeF menu{200, 300};

private slots:
    void aMenuThatFitsStaysWhereItWasAsked()
    {
        QCOMPARE(ShellMenus::fitted(screen, {100, 100, 80, 20}, {100, 120}, menu), QPointF(100, 120));
    }
    void aMenuBelowAnAnchorAtTheBottomOpensAboveIt()
    {
        QCOMPARE(ShellMenus::fitted(screen, {100, 640, 400, 60}, {100, 700}, menu), QPointF(100, 340));
    }
    void aMenuAtAPointNearTheBottomEndsAtThePoint()
    {
        QCOMPARE(ShellMenus::fitted(screen, {0, 0, 1280, 720}, {50, 600}, menu), QPointF(50, 300));
    }
    void aMenuThatFitsNeitherWayStaysInsideTheScreen()
    {
        QCOMPARE(ShellMenus::fitted({0, 0, 1280, 400}, {0, 100, 80, 20}, {0, 120}, menu), QPointF(0, 100));
        QCOMPARE(ShellMenus::fitted({0, 0, 1280, 250}, {0, 100, 80, 20}, {0, 120}, menu), QPointF(0, 0));
    }
    void aMenuNearTheRightEdgeMovesLeft()
    {
        QCOMPARE(ShellMenus::fitted(screen, {1200, 100, 80, 20}, {1200, 120}, menu), QPointF(1080, 120));
    }
    void aSecondMonitorUsesItsOwnGeometry()
    {
        const QRectF second{1280, 0, 1920, 1080};
        QCOMPARE(ShellMenus::fitted(second, {1300, 1040, 400, 40}, {1300, 1080}, menu), QPointF(1300, 740));
        QCOMPARE(ShellMenus::fitted(second, {1290, 10, 10, 10}, {1200, 20}, menu), QPointF(1280, 20));
    }
};

QTEST_GUILESS_MAIN(ShellMenuPlacementTest)
#include "shell_menu_placement_tests.moc"
