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
    readonly property color pressColor: Theme.highContrast
        ? Theme.select : Color.tint(Theme.raised, Color.transparent(Theme.text, 0.06))
    implicitWidth: 46
    implicitHeight: 32
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
        color: windowButton.shownDown ? windowButton.pressColor : windowButton.shownHover ? Theme.raised : "transparent"
        border.width: Theme.highContrast && (windowButton.shownDown || windowButton.shownHover) ? 1 : 0
        border.color: Theme.line
    }
    Accessible.role: Accessible.Button
    Accessible.name: text
    ToolTip.visible: shownHover && !shownDown
    ToolTip.text: text
}
