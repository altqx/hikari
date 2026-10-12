import QtQuick
import Hikari.Ui

// D3: the free part of a floating window's header (DockTabBar.qml,
// DockTitleBar.qml), where the window system moves the window
// (Docking.startSystemMove), so its snapping and tiling apply. The move
// starts once the pointer has moved, so a click or a double-click (which
// the header handles, to dock) never hands the pointer over. Where the
// platform declines the move (X11 with no window manager that moves
// windows), the window follows the pointer itself.
MouseArea {
    id: root
    objectName: "dockSystemMoveArea"
    cursorShape: Qt.SizeAllCursor

    property point pressedAt
    property point windowAt
    property bool moving: false
    property bool following: false

    function follow(at: point) {
        Window.window.x = Math.round(windowAt.x + at.x - pressedAt.x)
        Window.window.y = Math.round(windowAt.y + at.y - pressedAt.y)
    }

    onPressed: mouse => {
        pressedAt = mapToGlobal(mouse.x, mouse.y)
        moving = false
        following = false
    }
    onPositionChanged: mouse => {
        if (!pressed)
            return
        const at = mapToGlobal(mouse.x, mouse.y)
        if (following) {
            follow(at)
            return
        }
        if (moving || Math.abs(at.x - pressedAt.x) + Math.abs(at.y - pressedAt.y) < Application.styleHints.startDragDistance)
            return
        moving = true
        if (!Docking.startSystemMove(root)) {
            windowAt = Qt.point(Window.window.x, Window.window.y)
            following = true
            follow(at)
        }
    }
    onReleased: {
        moving = false
        following = false
    }
    onCanceled: {
        moving = false
        following = false
    }
}
