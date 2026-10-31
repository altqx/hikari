pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ToolButton {
    id: button
    property string panelTitle
    property bool menuOpen: false
    property bool openedFromKeyboard: false
    objectName: "dockMenuButton"
    implicitWidth: 20
    implicitHeight: 20
    padding: 2
    display: AbstractButton.IconOnly
    down: pressed || menuOpen
    text: qsTr("%1 options").arg(panelTitle)
    contentItem: Icon {
        iconRole: "panel-menu"
        size: 16
    }
    // Qt Quick gives a menu button role no press action.
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.expandable: true
    Accessible.expanded: menuOpen
    ToolTip.visible: hovered && !menuOpen
    ToolTip.text: text
    HoverHandler {
        cursorShape: Qt.ArrowCursor
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    onClicked: openedFromKeyboard = visualFocus
    // A closed menu gives the focus back with Qt.OtherFocusReason, which Qt Quick Controls don't count as visualFocus.
    onFocusReasonChanged: {
        if (activeFocus && openedFromKeyboard && !menuOpen && !visualFocus) {
            openedFromKeyboard = false
            focusReason = Qt.TabFocusReason
        }
    }
}
