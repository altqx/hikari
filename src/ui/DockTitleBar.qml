pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews
import Hikari.Ui

KDDWViews.TitleBarBase {
    id: root
    objectName: "dockTitleBar"

    color: Theme.field
    heightWhenVisible: 35

    // Some compositors frame the window although it asked for no frame; their title then replaces ours.
    readonly property bool systemTitle: Docking.windowTitleRevision >= 0 && Docking.windowSystemTitle(Window.window)

    Accessible.role: Accessible.TitleBar
    Accessible.name: root.title
    // KDDW loads a title bar into every group; only some show it.
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
        textFormat: Text.PlainText
        color: Theme.text
        font.bold: true
        elide: Text.ElideRight
        Accessible.ignored: true
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
            iconRole: "panel-dock"
            objectName: "dockWindowDock"
            text: qsTr("Dock")
            onTriggered: root.floatButtonClicked()
        }
        ShellMenuItem {
            iconRole: "tab-close"
            objectName: "dockWindowClose"
            text: qsTr("Close")
            enabled: root.closeButtonEnabled
            onTriggered: root.closeButtonClicked()
        }
    }

    // KDDW's drag area takes the presses, so the cursor comes from a HoverHandler.
    HoverHandler {
        cursorShape: Qt.SizeAllCursor
    }

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
