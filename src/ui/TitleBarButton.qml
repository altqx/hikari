import QtQuick
import QtQuick.Controls
import Hikari.Ui

AbstractButton {
    id: windowButton
    required property string iconRole
    property bool active: true
    // Windows tracks the Maximise button's hover and press itself (snap layouts).
    property bool shownHover: hovered
    property bool shownDown: down
    implicitWidth: 46
    implicitHeight: Theme.titleBarHeight
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            iconRole: windowButton.iconRole
            color: windowButton.active ? IconTheme.normal : IconTheme.disabled
        }
    }
    background: Rectangle {
        color: !windowButton.shownDown && !windowButton.shownHover ? "transparent"
            : Theme.highContrast ? Theme.select : Theme.raised
        border.width: Theme.highContrast && (windowButton.shownDown || windowButton.shownHover) ? 1 : 0
        border.color: Theme.line
        Rectangle {
            anchors.fill: parent
            visible: windowButton.shownDown && !Theme.highContrast
            color: Theme.pressed
        }
    }
    Accessible.role: Accessible.Button
    Accessible.name: text
    ToolTip.visible: shownHover && !shownDown
    ToolTip.text: text
}
