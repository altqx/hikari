pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ToolButton {
    id: button
    property string panelTitle
    property bool menuOpen: false
    objectName: "dockMenuButton"
    implicitWidth: 20
    implicitHeight: 20
    padding: 2
    display: AbstractButton.IconOnly
    checked: menuOpen
    text: qsTr("%1 options").arg(panelTitle)
    contentItem: Icon {
        iconRole: "panel-menu"
        size: 16
    }
    // Qt Quick gives a menu button role no press action.
    Accessible.role: Accessible.Button
    Accessible.name: text
    ToolTip.visible: hovered && !menuOpen
    ToolTip.text: text
    HoverHandler {
        cursorShape: Qt.ArrowCursor
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
}
