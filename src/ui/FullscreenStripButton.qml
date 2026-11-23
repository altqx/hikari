import QtQuick
import QtQuick.Controls
import Hikari.Ui

Button {
    id: button
    required property string iconRole
    property string tip: text
    property string label
    readonly property color ink: "#EEF0F2"
    display: label.length ? AbstractButton.TextBesideIcon : AbstractButton.IconOnly
    focusPolicy: Qt.TabFocus
    padding: 7
    implicitWidth: Math.max(34, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 34
    contentItem: Row {
        spacing: 6
        opacity: button.enabled ? 1 : 0.38
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            iconRole: button.iconRole
            size: 20
            color: button.ink
            accentColor: button.ink
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.label.length > 0
            text: button.label
            color: button.ink
        }
    }
    background: Rectangle {
        radius: 4
        color: button.hovered || button.down ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
        border.width: button.visualFocus ? 1 : 0
        border.color: button.ink
    }
    Accessible.name: text
    Accessible.description: tip.split("\n")[0]
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
}
