import QtQuick
import Hikari.Ui

// SH1 (#220), DG1 (#222): the free parts of a drawn title bar (MainTitleBar,
// DialogTitleBar): a drag moves the window through the window system, a
// double-click maximises it (while it can be), a right click opens the
// window system's window menu. On Windows the window system takes the
// pointer here (WM_NCHITTEST says HTCAPTION, MainWindowFrame), so it never
// reaches this area.
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
        // Where the platform declines the move (X11 with no window
        // manager), the window follows the pointer; never a maximised one.
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
