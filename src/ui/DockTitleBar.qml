import QtQuick
import QtQuick.Controls
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews
import Hikari.Ui

// D3: the engine's title bar. With the adapter's flags a group never shows
// one (DockTabBar.qml is every group's header), so this is the bar of a
// floating window that holds several groups side by side: the window's
// title in bold and a "⋯" menu that docks or closes the whole window, in
// the header's look (docs/research/musescore-docking.md §1). Dragging it
// moves the window (on Wayland through the compositor), double-clicking
// docks it. Loaded through the adapter's view factory (ui/docking.cpp).
KDDWViews.TitleBarBase {
    id: root
    objectName: "dockTitleBar"

    color: Theme.field
    heightWhenVisible: 35

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
        objectName: "dockTitleText"
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

    // Wayland: the compositor moves the window.
    MouseArea {
        z: 1
        enabled: Docking.systemMove
        visible: enabled
        width: Math.max(0, menuButton.x - 2)
        height: parent.height
        onPressed: Window.window.startSystemMove()
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
