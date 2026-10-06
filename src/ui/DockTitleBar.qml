import QtQuick
import QtQuick.Controls
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews
import Hikari.Ui

// D3: the engine's title bar. With the adapter's flags a group never shows
// one (DockTabBar.qml is every group's header), so this is the bar of a
// floating window that holds several groups side by side: the window's
// title in bold and a "⋯" menu that docks or closes the whole window, in
// the header's look (docs/research/musescore-docking.md §1), with the move
// cursor. Dragging it moves the window and docks it where the highlight
// shows, double-clicking docks it. On Wayland, as on a panel's header
// (DockTabBar.qml), the title stays the engine's drag (it docks) and the
// rest of the bar moves the window through the compositor. Loaded through
// the adapter's view factory (ui/docking.cpp).
KDDWViews.TitleBarBase {
    id: root
    objectName: "dockTitleBar"

    color: Theme.field
    heightWhenVisible: 35

    // A compositor that frames the window although it asked for no frame
    // (Docking.windowSystemTitle) shows its title: the bar keeps the "⋯".
    readonly property bool systemTitle: Docking.windowTitleRevision >= 0 && Docking.windowSystemTitle(Window.window)

    Accessible.role: Accessible.TitleBar
    Accessible.name: root.title
    // Every group loads one; only a floating window of several groups shows it.
    Accessible.ignored: !root.visible

    Rectangle {
        width: parent.width
        height: 1
        y: parent.height - 1
        color: Theme.line
    }

    Text {
        id: titleText
        objectName: "dockTitleText"
        visible: !root.systemTitle
        x: 12
        width: Math.max(0, menuButton.x - x - 4)
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.text
        font.bold: true
        elide: Text.ElideRight
        Accessible.ignored: true // the title bar carries the name
    }

    DockMenuButton {
        id: menuButton
        z: 1
        x: root.width - 12 - width
        anchors.verticalCenter: parent.verticalCenter
        panelTitle: root.title
        menuOpen: windowMenu.visible
        Accessible.ignored: !root.visible
        activeFocusOnTab: true
        focusPolicy: Qt.TabFocus
        onClicked: windowMenu.popup(menuButton, 0, menuButton.height)
    }
    ShellMenu {
        id: windowMenu
        objectName: "dockWindowMenu"
        ShellMenuItem {
            objectName: "dockWindowDock"
            text: qsTr("Dock")
            onTriggered: root.floatButtonClicked()
        }
        ShellMenuItem {
            objectName: "dockWindowClose"
            text: qsTr("Close")
            enabled: root.closeButtonEnabled
            onTriggered: root.closeButtonClicked()
        }
    }

    // The move cursor over the bar (the engine's drag area takes the
    // presses).
    HoverHandler {
        cursorShape: Qt.SizeAllCursor
    }

    // Wayland: right of the title (keeping at least 48 pixels), the
    // compositor moves the window once the pointer has moved; a
    // double-click there docks it.
    MouseArea {
        id: systemMoveArea
        objectName: "dockSystemMoveArea"
        z: 1
        enabled: Docking.systemMove
        visible: enabled
        x: Math.min(titleText.x + titleText.contentWidth + 8, Math.max(0, menuButton.x - 2 - 48))
        width: Math.max(0, menuButton.x - 2 - x)
        height: parent.height
        cursorShape: Qt.SizeAllCursor
        property point pressedAt
        property bool moving: false
        onPressed: mouse => {
            pressedAt = Qt.point(mouse.x, mouse.y)
            moving = false
        }
        onPositionChanged: mouse => {
            if (moving || !pressed)
                return
            if (Math.abs(mouse.x - pressedAt.x) + Math.abs(mouse.y - pressedAt.y) >= Application.styleHints.startDragDistance) {
                moving = true
                Docking.startSystemMove(systemMoveArea)
            }
        }
        onReleased: moving = false
        onCanceled: moving = false
        onDoubleClicked: root.floatButtonClicked()
    }

    // K2: the focus ring of the window holding the keyboard focus.
    Rectangle {
        objectName: "focusRing"
        anchors.fill: parent
        anchors.margins: 1
        z: 2
        color: "transparent"
        border.width: 2
        border.color: Theme.focus
        visible: root.isFocused && !menuButton.activeFocus
    }
}
