import QtQuick
import Hikari.Ui

MouseArea {
    id: moveArea
    required property MainWindowFrame frame
    readonly property Window window: frame.window
    readonly property bool maximized: window !== null
        && (window.visibility === Window.Maximized || window.visibility === Window.FullScreen)
    enabled: frame.custom
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    property point pressedAt
    property point windowAt
    property bool moving: false
    property bool following: false
    function follow(at: point) {
        window.x = Math.round(windowAt.x + at.x - pressedAt.x)
        window.y = Math.round(windowAt.y + at.y - pressedAt.y)
    }
    onPressed: mouse => {
        moving = false
        following = false
        pressedAt = mapToGlobal(mouse.x, mouse.y)
        if (mouse.button === Qt.RightButton)
            frame.showWindowMenu(mapToItem(null, mouse.x, mouse.y))
    }
    onPositionChanged: mouse => {
        if (!(pressedButtons & Qt.LeftButton))
            return
        const at = mapToGlobal(mouse.x, mouse.y)
        if (following) {
            follow(at)
            return
        }
        if (moving || Math.abs(at.x - pressedAt.x) + Math.abs(at.y - pressedAt.y) < Application.styleHints.startDragDistance)
            return
        moving = true
        // startSystemMove fails on X11 without a window manager; then the window follows the pointer.
        if (!Docking.startSystemMove(moveArea) && !maximized) {
            windowAt = Qt.point(window.x, window.y)
            following = true
            follow(at)
        }
    }
    onDoubleClicked: mouse => {
        if (mouse.button === Qt.LeftButton && frame.resizable)
            frame.toggleMaximized()
    }
    onReleased: { moving = false; following = false }
    onCanceled: { moving = false; following = false }
}
