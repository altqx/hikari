pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews
import Hikari.Ui

// D3: the engine's title bar. With the adapter's flags a group never shows
// one (DockTabBar.qml is every group's header), so this is the bar of a
// floating window that holds several groups side by side: the window's
// title in bold and a "⋯" menu that docks or closes the whole window, in
// the header's look (docs/research/musescore-docking.md §1), with the move
// cursor. Dragging its title moves the window and docks it where the
// highlight shows, double-clicking docks it. As on a panel's header
// (DockTabBar.qml), on Wayland and X11 the rest of the bar moves the window
// through the window system; on Windows the whole bar is the drag. The bar draws no keyboard focus mark: the group holding
// the focus in the window shows the accent corner mark on its own header
// (K2, PanelFocusMark.qml), one mark for one focus. Loaded through the
// adapter's view factory (ui/docking.cpp).
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
        width: Math.max(0, (systemMoveArea.enabled ? systemMoveArea.x : menuButton.x) - x - 4)
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

    // Right of the title the window system moves the window; a
    // double-click there docks it. The title and that part keep 48 pixels
    // each, as on a panel's header.
    DockSystemMoveArea {
        id: systemMoveArea
        z: 1
        enabled: Docking.systemMove
        visible: enabled
        x: Math.max(titleText.x + 48, Math.min(titleText.x + titleText.implicitWidth + 8, menuButton.x - 2 - 48))
        width: Math.max(0, menuButton.x - 2 - x)
        height: parent.height
        onDoubleClicked: root.floatButtonClicked()
    }
}
